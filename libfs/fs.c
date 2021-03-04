#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "disk.h"
#include "fs.h"

/* TODO: Phase 1 */

#define FAT_EOC 0xFFFF
const char SIG[9] = "ECS150FS\0";

typedef struct __attribute__((__packed__)) superblock
{
    uint8_t signature[8];        // signature: always == "ESC150FS"
    uint16_t num_blocks;         // Total amount of blocks of virtual disk
    uint16_t root_index;         // Root directory block index
    uint16_t data_index;         // Data block start index
    uint16_t num_data;           // Amount of data blocks
    uint8_t num_fat;             // Number of blocks for FAT
    uint8_t padding[4079];       // Unused/Padding
} superblock;

typedef struct __attribute__((__packed__)) root_directory
{
    uint8_t filename[FS_FILENAME_LEN];      // Filename (including NULL character)
    uint32_t size;                          // Size of the file (in bytes)
    uint16_t data_index;                    // Index of the first data block
    uint8_t padding[10];                    // Unused/Padding
} root_directory;

typedef struct open_file_dir
{
    size_t offset;
    int8_t root_index;
} open_file_dir;

uint16_t *FAT;
static superblock *SB;
static root_directory *root_dir;
static open_file_dir *open_files;

int8_t num_open = 0;

int init_fat()
{
    //printf("Init_fat\n");
    //printf("num_fat == %d\n", (int) SB->num_fat);
    FAT = (uint16_t*) malloc(SB->num_fat * BLOCK_SIZE);      // 2 byte width per entry
    //printf("Malloc\n");
    FAT[0] = FAT_EOC;
    //printf("Assign\n");
    int num[5];
    num[0] = FAT;
    for (int i = 0; i < SB->num_fat; ++i) {
        //printf("BLK SIZE = %d\n", (int)BLOCK_SIZE * i);
        num[i+1] = FAT + (BLOCK_SIZE * i)/2;
        //printf("Reading: %d, %d\n", i, FAT + (BLOCK_SIZE * i)/2);
        if (block_read(i + 1, (void*) (FAT + (BLOCK_SIZE * i )/2)) == -1) {
            return -1;
        } 
        //printf("%d - %d = %d\n", num[i], num[i+1], num[i+1]-num[i]);
    }
    return 0;
}

int init_superblock()
{
    //printf("Init_superblock\n"); 
    SB = (superblock*) malloc(BLOCK_SIZE);
    //printf("Malloced\n");
    if (block_read(0, SB) == -1) {
        return -1;
    }
    //printf("read\n");
    char tmpStr[8]; //= (char*) malloc(20 * sizeof(char));
    //printf("malloc2\n");
    strncpy(tmpStr, (char*) SB->signature, 8);
    //printf("strcpy'd\n");
    if (strcmp(tmpStr, SIG) != 0) {
        //free(tmpStr);
        //printf("tmpStr = %s != %s = SIG\n", tmpStr, SIG);
        return -1;
    } else if(SB->num_blocks != block_disk_count()) {
       // free(tmpStr);
        //printf("wrong blks wtf\n");
        return -1;
    } 
    //printf("numfat == %d\n", SB->num_fat);
    //free(tmpStr);
    return 0;
}

int init_root_dir() 
{
    //printf("Init_root_dir\n");
    root_dir = (root_directory*) malloc(32 * FS_FILE_MAX_COUNT); 
    if (block_read(SB->root_index, (void*) root_dir) == -1) {
        return -1;
    }
    return 0; 
}

int init_file_dir()
{
    //printf("Init_file_dir\n");
    open_files = (open_file_dir*) malloc(FS_OPEN_MAX_COUNT * sizeof(open_file_dir));
    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
        open_files[i].root_index = -1;
    }
    return 0;
}

// Returns index of filename in root_dir
// Otherwise returns -1
int fs_contains(const char *filename) 
{
    //printf("fs_contains\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        ////printf("Root dir blk # = %d, str = %s\n", i, root_dir[i].filename);
        if (strcmp(root_dir[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

// Returns the index of the next opening in the root directory
// -1 if root directory is full
int fs_next_opening()
{
    //printf("fs_next_opening\n");
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        if (strcmp(root_dir[i].filename, "") == 0) {
            // Opening found!
            return i;
        }
    }
    //printf("Not found:(\n");
    return -1;
}

// Return index of opening in FAT
// -1 if there are no openings
int fs_fat_opening()
{
    //printf("fs_fat_opening\n");
    for (int i = 0; i < SB->num_blocks; ++i) {
        //printf("FAT[%d] == %d\n", i, FAT[i]);
        if (FAT[i] == 0) {
            return i;
        }
    }
    return -1;
}

int fs_mount(const char *diskname)
{
    //printf("fs_mount\n");

    if (block_disk_open(diskname) == -1) {
        return -1;
    }
    // Initialize Superblock, FAT, & Root Directory
    if (init_superblock() == -1 || init_fat() == -1 || init_root_dir() == -1 || init_file_dir() == -1) {
        // Initialization failure
        return -1;
    }
    return 0;


}

int fs_umount(void)
{
    //printf("fs_umount\n");

    if (SB == NULL || FAT == NULL || root_dir == NULL) {
        // Nothing currently mounted
        return -1;
    }
    //printf("SB write\n");
    if (block_write(0, (void*) SB) == -1) {
        return -1;
    }
    //printf("root dir write\n");
    if (block_write(SB->root_index, (void*) root_dir) == -1) {
        return -1;
    }
    //printf("FAT write\n");
    for (int i = 0; i < SB->num_fat; ++i) {
        ////printf("Writing: %d, %d\n", i, FAT + ((BLOCK_SIZE) * i)/2);
        if (block_write(i + 1, (void*) (FAT + (BLOCK_SIZE * i)/2)) == -1) {
            return -1;
        }
    }
    //printf("free\n");
    //printf("SB = %d\n", SB);
    //printf("FAT = %d\n", FAT);
    //printf("root_dir = %d\n", root_dir);
    free(SB);
    free(FAT);
    free(root_dir);
    free(open_files);

    //printf("freed\n");
    return 0;
    
}

int fs_info(void)
{
    //printf("fs_info\n");

    if (SB == NULL || FAT == NULL) {
        return -1;
    }

    int free_fat_blk = 0;
    for (int i = 0; i < SB->num_data; ++i) {
        if (FAT[i] == 0) {
            free_fat_blk++;
        }
    }

    int rdir_free = 0;
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        if (root_dir[i].data_index == 0) {
            rdir_free++;
        }
    }

    printf("FS Info:\n");
    printf("total_blk_count=%d\n", SB->num_blocks);
    printf("fat_blk_count=%d\n", SB->num_fat);
    printf("rdir_blk=%d\n", SB->root_index);
    printf("data_blk=%d\n", SB->data_index);
    printf("data_blk_count=%d\n", SB->num_data);
    printf("fat_free_ratio=%d/%d\n", free_fat_blk, SB->num_data);
    printf("rdir_free_ratio=%d/%d\n", rdir_free, FS_FILE_MAX_COUNT);
    return 0;
}

int fs_create(const char *filename)
{
    //printf("\nfs_create\n");
    //printf("%d\n", block_disk_count());
    if (strlen(filename) > FS_FILENAME_LEN) {
        // Invalid filename
        //printf("Invalid name\n");
        return -1;
    }

    if (SB == NULL || FAT == NULL || root_dir == NULL) {
        // Nothing currently mounted
        //printf("Nothing mounted\n");
        return -1;
    }

    // check if filesystem already has filename
    if (fs_contains(filename) != -1) {
        //printf("Filename already exists\n");
        return -1;
    }
    int next_opening = fs_next_opening();
    int fat_opening = fs_fat_opening();
    if (next_opening == -1 || fat_opening == -1) {
        // no openings in root directory or FAT
        //printf("no openings?\n");
        return -1;
    }
    //printf("Opening index in rtdir = %d\n", next_opening);
    strcpy(root_dir[next_opening].filename, filename);
    // root_dir[next_opening].size = 38;                        // Moving to fs_write
    root_dir[next_opening].data_index = fat_opening;
    //printf("Opening index in FAT = %d\n", fat_opening);

    return 0;
}

int fs_delete(const char *filename)
{

    // Start delete process
    int i;
    for (i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        // Check if root_dir[i] contains a file before strcmp
        if (root_dir[i].filename[0] != '\0')
        {
            // Let's check if this is the file we want
            if (strcmp(root_dir[i].filename, filename) == 0)
            {
                // "Delete" the file
                root_dir[i].filename[0] = '\0';

                // Clear FAT
                uint16_t current_FAT_index = root_dir[i].data_index;
                uint16_t temp_index;
                while(current_FAT_index != FAT_EOC)
                {
                    temp_index = FAT[current_FAT_index];
                    FAT[current_FAT_index] = 0;
                    current_FAT_index = temp_index;
                }
                break;
            }
        }
    }

    /* Success */
    return 0;
}

int fs_ls(void)
{
    //printf("fs_ls\n");

    if (SB == NULL || FAT == NULL || root_dir == NULL) {
        //printf("Not even mounted smh\n");
        return -1;
    }
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        if(root_dir[i].filename[0] != '\0') { //changed this line from Andy
            // There exists a file at index i
            printf("file: %s, size: %d, data_blk: %d\n", root_dir[i].filename, root_dir[i].size, root_dir[i].data_index);
        }
    }

    return 0;
}

int fs_open(const char *filename)
{
    //printf("fs_open\n");

    if (SB == NULL || FAT == NULL || root_dir == NULL || open_files == NULL) {
        //printf("Not even mounted smh\n");
        return -1;
    }
    if (strlen(filename) > FS_FILENAME_LEN) {
        return -1;
    }
    int index = fs_contains(filename);
    if (index == -1) {
        // no filename in fs
        return -1;
    }
    int i;
    for (i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
        if (open_files[i].root_index == -1) {
            // file descriptor not yet set AKA available
            open_files[i].offset = 0;
            open_files[i].root_index = index;
            num_open++;
            break;
        }
    }
    return i;
}

int fs_close(int fd)
{    
    //printf("fs_close fd=%d\n", fd);
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
        // Invalid fd does not exist in open_files
        return -1;
    }

    open_files[fd].root_index = -1;          // reset root_index to default value
    num_open--;
    //printf("Closed!\n");
    return 0;
}

int fs_stat(int fd)
{
    //printf("fs_stat\n");
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
        // Invalid fd does not exist in open_files
        return -1;
    }
    if (open_files[fd].root_index == -1) {
        // The file (descriptor fd) is not open
        return -1;
    }

    if (SB == NULL || FAT == NULL || root_dir == NULL || open_files == NULL) {
        //printf("Not even mounted smh\n");
        return -1;
    }

    return root_dir[open_files[fd].root_index].size;
}

int fs_lseek(int fd, size_t offset)
{
    //printf("fs_lseek\n");
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
        // Invalid fd does not exist in open_files
        return -1;
    }

    if (SB == NULL || FAT == NULL || root_dir == NULL || open_files == NULL) {
        //printf("Not even mounted smh\n");
        return -1;
    }

    int root_index = open_files[fd].root_index;

    if(root_dir[root_index].size < offset) {
        // Offset is larger than file size =>
        return -1;
    }

    // Set the offset
    open_files[fd].offset = offset;
    return 0;
}

int fs_write(int fd, void *buf, size_t count) 
{
    printf("fs_write\n");

    if (SB == NULL || root_dir == NULL || open_files == NULL || FAT == NULL) {
        return -1;
    }
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || open_files[fd].root_index == -1 || buf == NULL) {
        // Invalid fd does not exist in open_files
        return -1;
    }


    printf("size = %d\n", count);

    int num_blocks = ceil(count / BLOCK_SIZE);
    int current_blocks = 0;
    int8_t *prev_block = (int8_t*) malloc(BLOCK_SIZE * sizeof(int8_t));
    int index = root_dir[open_files[fd].root_index].data_index;
    root_dir[open_files[fd].root_index].size = count;
    ////printf("414\n");
    printf("Index = %d\n", index);
     for (int i = 0; i < 10; ++i) {
        printf("FAT[%d] = %d\n", i, FAT[i]);
    }
    int temp_indx = index;
    int offset = open_files[fd].offset;
    int bytes_to_read = count;

    if (num_blocks == 0)
        ++num_blocks;

    if (count == 0) {
        // Nothing to allocate
        // return size 0
        root_dir[open_files[fd].root_index].data_index = FAT_EOC;
        return 0;
    }
    //if (root_dir[open_files[fd].root_index].size < count + open_files[fd].offset) {
        // Add more fat blocks
        //printf("NEED MORE SPACE IN FAT\n");
        //printf("num_blocks = %d\n", num_blocks);
        //printf("index = %d\n", index);
        printf("\nnum_blokcs = %d\n", num_blocks);
        printf("filename= %s\n", root_dir[open_files[fd].root_index].filename);
        for (int i = 0; i < num_blocks; ++i) { // not sure if it correctly handles multiblock files
            //printf("Allocated FAT[%d] blk #%d\n", temp_indx, i);
            FAT[temp_indx] = fs_fat_opening();
            temp_indx = FAT[temp_indx];
        }
        FAT[temp_indx] = FAT_EOC;
    //}

    

    while (offset > BLOCK_SIZE) {
            offset -= BLOCK_SIZE;
            index = FAT[index];
    }

    ////printf("index = %d\n", index);

    for (int i = 0; i < 10; ++i) {
        //printf("FAT[%d] = %d\n", i, FAT[i]);
    }

    //printf("SB->data_indx = %d\n", SB->data_index);
    //block_read(index + SB->data_index, prev_block);
    //memcpy(prev_block + offset, buf, BLOCK_SIZE - offset);
    block_write(index + SB->data_index, buf);
    bytes_to_read -= BLOCK_SIZE;
    bytes_to_read += offset;

    

    buf += BLOCK_SIZE - offset;
    index = FAT[index];

    for (int i = 0; i < num_blocks - 2; ++i) {
        ////printf("middle\n");
        block_write(index + SB->data_index, buf);
        buf += BLOCK_SIZE;
        index = FAT[index];
        bytes_to_read -= BLOCK_SIZE;
    }

    if (current_blocks + 1 < num_blocks) {
        ////printf("last\n");
        block_read(index + SB->data_index, prev_block);
        memcpy(prev_block, buf, bytes_to_read);
        block_write(index + SB->data_index, prev_block);
    }
    //open_files[fd].offset += count;
    free(prev_block);
    return count;   // change to number of bytes actually written, not # suppoesed to be written
}

int fs_read(int fd, void *buf, size_t count) {
    ////printf("fs_read\n");
   //      printf("count = %d\n", count);
    if (SB == NULL || root_dir == NULL || open_files == NULL || FAT == NULL) {
        return -1;
    }
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || open_files[fd].root_index == -1 || buf == NULL) {
        // Invalid fd does not exist in open_files
        return -1;
    }
    int num_blocks = ceil(BLOCK_SIZE / count);
    int8_t *bounce_buf = (int8_t*) malloc(BLOCK_SIZE* sizeof(int8_t) * num_blocks);
    int index = root_dir[open_files[fd].root_index].data_index;
    //printf("Index = %d\n", index);
    int offset = open_files[fd].offset;
    int bytes_read = 0;

    while (offset > BLOCK_SIZE) {
      //  printf("Should even be here?\n");
        index = FAT[index];
        offset -= BLOCK_SIZE;
    }
    printf("after while, offset: %d\n", offset);

    int i = 0;
    //printf("FAT[index] = %d\n", FAT[index]);
    //printf("block_read(%d, bounce_buf, %d)\n", index + SB->data_index, bounce_buf + BLOCK_SIZE*i);
    block_read(index + SB->data_index, bounce_buf + BLOCK_SIZE*i);
    bytes_read += strlen(bounce_buf) - offset;
    //printf("FAT[%d] = %d\n", index, FAT[index]);
    while (FAT[index] != FAT_EOC) {
        index = FAT[index];
        ++i;
        block_read(index + SB->data_index, bounce_buf + BLOCK_SIZE*i);
        bytes_read += strlen(bounce_buf);
    }
    printf("Filename: %s\n", root_dir[open_files[fd].root_index].filename);

    memcpy((int8_t*) buf, bounce_buf, count);
 //   open_files[fd].offset += strlen(buf);
    //printf("%d\n", strlen(buf));
    free(bounce_buf);
    return bytes_read;
}
