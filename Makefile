CFLAGS += -Wall -Wextra -pedantic -Werror -std=c99 -O2
CFLAGS += $(shell pkg-config --cflags fuse)
LDFLAGS += $(shell pkg-config --libs fuse)

all: vpk_fuse

vpk_fuse: vpk_fuse.c
	$(CC) $(CFLAGS) -o vpk_fuse vpk_fuse.c $(LDFLAGS)
