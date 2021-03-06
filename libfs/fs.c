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
    char filename[FS_FILENAME_LEN];
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
    FAT = (uint16_t*) malloc((SB->num_data + 4) * BLOCK_SIZE);      // 2 byte width per entry
    //printf("Malloc\n");
    FAT[0] = FAT_EOC;
    //printf("Assign\n");

    for (int i = 0; i < SB->num_fat; ++i) {
        //printf("BLK SIZE = %d\n", (int)BLOCK_SIZE * i);
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
        if (strcmp((char*)root_dir[i].filename, filename) == 0) {
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
        if (strcmp((char*)root_dir[i].filename, "") == 0) {

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
    for (int i = 0; i < SB->num_data; ++i) {
        //printf("FAT[%d] == %d\n", i, FAT[i]);
        if (FAT[i] == 0) {
            return i;
        }
    }
    return -1;
}

int fs_mount(const char *diskname)
{

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
   // printf("Opening index in rtdir = %d\n", next_opening);
    strcpy((char*)root_dir[next_opening].filename, filename);
    // root_dir[next_opening].size = 38;                        // Moving to fs_write
    root_dir[next_opening].data_index = fat_opening;
    //printf("FATOPENING = %d\n", fat_opening);
  //  printf("Opening index in FAT = %d\n", fat_opening);

    return 0;
}

int fs_delete(const char *filename)
{

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

    int i;
    for (i = 0; i < FS_FILE_MAX_COUNT; i++)
    {
        if (root_dir[i].filename[0] != '\0')
        {
            if (strcmp((char*)root_dir[i].filename, filename) == 0)
            {
                root_dir[i].filename[0] = '\0';
                uint16_t temp_I;
                uint16_t current_I = root_dir[i].data_index;
                while(current_I != FAT_EOC)
                {
                    temp_I = FAT[current_I];
                    FAT[current_I] = 0;
                    current_I = temp_I;
                }
                break;
            }
        }
    }
    return 0;
}
int fs_ls(void)
{
    printf("FS Ls:\n");

    if (SB == NULL || FAT == NULL || root_dir == NULL) {
        //printf("Not even mounted smh\n");
        return -1;
    }
    for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
        if (root_dir[i].filename[0] != '\0') {
            // There exists a file at index i
            printf("file: %s, size: %d, data_blk: %d\n", root_dir[i].filename, root_dir[i].size, root_dir[i].data_index);
        }

    }

    return 0;
}

int fs_open(const char *filename)
{
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
            strncpy(open_files[i].filename, filename, FS_FILENAME_LEN);
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
    strcpy(open_files[fd].filename, "");
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

int fs_write(int fd, void *buf, size_t count) {
    if (SB == NULL || root_dir == NULL || open_files == NULL || FAT == NULL) {
        return -1;
    }
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || open_files[fd].root_index == -1 || buf == NULL) {
        // Invalid fd does not exist in open_files
        return -1;
    }
    int index = root_dir[open_files[fd].root_index].data_index;

    if (count == 0) {
        root_dir[open_files[fd].root_index].data_index = FAT_EOC;
        return 0;
    }

    int num_fat = 0;
    // count currently allocated FAT blocks
    while (FAT[index] != FAT_EOC) {
        num_fat++;
        index = FAT[index];
    }
    root_dir[open_files[fd].root_index].size = count;
    int num_blocks = ceil(count/BLOCK_SIZE);


    index = root_dir[open_files[fd].root_index].data_index;

    while (num_fat < num_blocks*2) {
        FAT[index] = fs_fat_opening();
        index = FAT[index];
        num_fat++;
    }
    
    FAT[index] = FAT_EOC;
    
    int offset = open_files[fd].offset;
    int8_t *prev_block = (int8_t*) malloc(BLOCK_SIZE * sizeof(int8_t));

    while (offset > BLOCK_SIZE) {
            offset -= BLOCK_SIZE;
    }

    index = root_dir[open_files[fd].root_index].data_index;
    block_read(index + SB->data_index, (void*)prev_block);
    memcpy(prev_block + offset, buf, BLOCK_SIZE - offset);
    block_write(index + SB->data_index, (void*)prev_block);
    int bytes_left = count;
    bytes_left += offset;
    bytes_left -= BLOCK_SIZE;

    //printf("blockwrite(%d + %d, %p)", index, SB->data_index, prev_block);
    //printf("%s\n", (char*)buf);
    buf += BLOCK_SIZE - offset;
    index = FAT[index];
    //printf("%d\n", num_blocks);
    int i;
    for (i = 0; i < num_blocks - 2; ++i) {
        if (block_write(index + SB->data_index, buf) == -1)
            return -1;
        buf += BLOCK_SIZE;
        index = FAT[index];
        bytes_left -= BLOCK_SIZE;
    }
    if (i == num_blocks - 1) {
        if (block_read(index + SB->data_index, prev_block) == -1)
            return -1;
        memcpy(prev_block, buf, bytes_left);
        if (block_write(index + SB->data_index, prev_block) == -1)
            return -1;
    }
    open_files[fd].offset += count;
    free(prev_block);
    return count;
}

int fs_read(int fd, void *buf, size_t count) {
    size_t offset = open_files[fd].offset;

    int bytes = 0;
    int index = -1;
    int i = 0;

    while (index == -1 && i < FS_FILE_MAX_COUNT) {
        if (strncmp(open_files[fd].filename, (char*) root_dir[i].filename, FS_FILENAME_LEN)) {
            index = i;
        }
        ++i;
    }
    if (index == -1) {
        return -1;
    }

    while (offset > BLOCK_SIZE) {
        offset -= BLOCK_SIZE;
    }

    size_t block_avail = BLOCK_SIZE - offset;

    //printf("num_open = %d\n", num_open);
    int8_t *bounce_buf = (int8_t*)malloc(BLOCK_SIZE * sizeof(int8_t*));
    int8_t *ptr = bounce_buf;
    for (int index = root_dir[open_files[fd].root_index].data_index; index != FAT_EOC; index = FAT[index]) {
        if (block_read(SB->data_index + index, bounce_buf) == -1)
            return -1;
        bounce_buf += offset;
        if (count <= block_avail) {
            bytes += strlen((char*)bounce_buf);
            open_files[fd].offset += count;
            memcpy(buf, bounce_buf, count); 
            bytes = count;
            break;
        } else {
            memcpy(buf, bounce_buf, BLOCK_SIZE - offset);
            count -= block_avail;
            bytes += strlen((char*) bounce_buf);
            buf = (char*) buf;
            buf += block_avail;
            open_files[fd].offset += block_avail;
            offset = 0;
        }
    }
    free(ptr);
    return bytes;
}
