objs := disk.o fs.o
CC := gcc
CFLAGS := -Wall -Werror -Wextra -O2
CFLAGS += -g

libfs.a: $(objs)
	ar rcs $@ $^

%.o: %.c
	@echo "CC $@"
	$(CC) $^ -c -o $@ $(CFLAGS)

clean:
	@echo "CLEAN"
	rm -rf $(objs) libfs.a

