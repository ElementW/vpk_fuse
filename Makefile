CFLAGS += -std=gnu99 -g
CFLAGS += $(shell pkg-config --cflags fuse)
LDFLAGS += $(shell pkg-config --libs fuse)

all: vpk_fuse

vpk_fuse: vpk_fuse.c
	$(CC) $(CFLAGS) -o vpk_fuse vpk_fuse.c $(LDFLAGS)