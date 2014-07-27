all: vpk_fuse

vpk_fuse: vpk_fuse.c
	$(CC) `pkg-config --cflags --libs fuse` -std=gnu99 -g -o vpk_fuse vpk_fuse.c
	#$(CC) `pkg-config --cflags --libs fuse` -std=gnu99 -g -c vpk_fuse.c