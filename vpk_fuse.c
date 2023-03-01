/*  vpk_fuse - FUSE filesystem to read VPK files
    Copyright (C) 2019  Celeste "ElementW" Wouters

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

Version history:
1.0: initial release
1.01: fixed bug where vpk_fuse would hang if read offset > file size
1.02: fixed opening single-VPK archives ("addon vpks")
1.03: use realpath of archive and "vpk" as FUSE fsname and subtype, respectively
*/
#define VERSION_STRING "1.03"

#define _XOPEN_SOURCE 600
#define FUSE_USE_VERSION 31
#include <errno.h>
#include <fuse.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define nullptr ((void*)0)

typedef uint64_t uint64;

#define LogE(...) {printf("\033[31mError:\033[39m ");printf(__VA_ARGS__);puts("");}
#define LogW(...) {printf("\033[33mWarning:\033[39m ");printf(__VA_ARGS__);puts("");}
#define LogD(...) {printf(__VA_ARGS__);puts("");}

typedef struct VPKHeader {
	unsigned int Signature;
	unsigned int Version;
	unsigned int TreeLength; // The length of the directory
} VPKHeader;

typedef struct VPK2Header {
	int Unknown1; // 0 in CSGO
	unsigned int FooterLength;
	int Unknown3; // 48 in CSGO
	int Unknown4; // 0 in CSGO
} VPK2Header;

typedef struct VPKDirectoryEntry {
	unsigned int CRC; // A 32bit CRC of the file's data.
	unsigned short PreloadBytes; // The number of bytes contained in the index file.

	// A zero based index of the archive this file's data is contained in.
	// If 0x7fff, the data follows the directory.
	unsigned short ArchiveIndex;

	// If ArchiveIndex is 0x7fff, the offset of the file data relative to the end of the directory (see the header for more details).
	// Otherwise, the offset of the data from the start of the specified archive.
	unsigned int EntryOffset;

	// If zero, the entire file is stored in the preload data.
	// Otherwise, the number of bytes stored starting at EntryOffset.
	unsigned int EntryLength;

	unsigned short Terminator;
} VPKDirectoryEntry;

typedef struct VPK {
	char *Path, *FileName;
	int PathLen, FileNameLen;
	int FD;
	bool SwapEndian; // Maybe someday
	VPKHeader Header;
	VPK2Header HeaderV2;
	unsigned short ArchiveFDCount;
	int *ArchiveFDs;
	uint64 DataOffset;
} VPK;

static uint64 IDCount = 0;
typedef struct DirectoryEntry {
	bool IsDirectory;
	char *Name;
	uint64 ID;
	void *Data;
} DirectoryEntry;

typedef struct Directory {
	unsigned int EntryCount, EntryCapacity;
	DirectoryEntry *Entries;
} Directory;

typedef struct File {
	uint64 Size;
	unsigned int PreloadSize;
	uint64 PreloadOffset;
	unsigned short ArchiveIndex;
	unsigned int DataSize;
	uint64 DataOffset;
} File;



const unsigned int VPKSig = 0x55aa1234;
VPK vpk;
DirectoryEntry rootEntry;
Directory root;

DirectoryEntry* GetEntryIn(const Directory* const dir, const char *const name) {
	for (unsigned int i = 0; i < dir->EntryCount; i++) {
		if (strcmp(dir->Entries[i].Name, name) == 0)
			return &dir->Entries[i];
	}
	return nullptr;
}

#define freenull(x) {free(x); x = nullptr;}

DirectoryEntry* AddDirectoryTo(Directory *const dir, const char *const name);
DirectoryEntry* GetEntryMkdirs(const char *const path, const bool mkdirs) {
	int len = strlen(path), pos = 0; char *name = nullptr;
	if (path == nullptr || (path[0] == '/' && path[1] == 0) || path[0] == 0)
		return &rootEntry;
	if (path[0] == '/')
		pos = 1;
	DirectoryEntry *ent = &rootEntry, *lastEnt;
	while (true) {
		freenull(name); // make sure no memory is wasted
		if (pos >= len) // Are we at the end (or past it)?
			break; // If yes, get out, nothing more to process
		int s = pos; for (; pos < len && path[pos] != '/' && path[pos] != 0; pos++); // split to next /
		bool last = (pos == len);
		/*printf("%s%s\n", path, last?" LAST":"");
		for (int i=0; i < s; i++) putc(' ', stdout);
		for (int i=s; i < pos; i++) putc('-', stdout); putc('\n', stdout);*/
		if (pos == s) { // 0-width 'name', meaning "...//...", so we skip
			pos++; continue;
		}
		name = malloc(pos-s+1); memcpy(name, &path[s], pos-s); name[pos-s] = 0;
		lastEnt = ent;
		ent = GetEntryIn(ent->Data, name);
		if (ent == nullptr) {
			if (mkdirs) {
				//printf(" doesn't exist, creating\n");
				ent = AddDirectoryTo(lastEnt->Data, name);
			} else {
				//printf(" doesn't exist\n");
				ent = nullptr;
				break;
			}
		} else {
			//printf(" exists\n");
			if (!last && !ent->IsDirectory) { // existed before, but was a file? errawr
				ent = nullptr;
				break;
			} else {
				if (last)
					break;
			}
		}
		pos++;
	}
	free(name);
	return ent;
}
DirectoryEntry* GetEntry(const char *const path) {
	return GetEntryMkdirs(path, false);
}

DirectoryEntry* AddEntryTo(Directory *const dir, const DirectoryEntry *const entry) {
	dir->EntryCount++;
	if (dir->EntryCount > dir->EntryCapacity) {
		dir->EntryCapacity++;
		dir->Entries = realloc(dir->Entries, dir->EntryCapacity*sizeof(DirectoryEntry));
	}
	memcpy(&dir->Entries[dir->EntryCount-1], entry, sizeof(DirectoryEntry));
	return &dir->Entries[dir->EntryCount-1];
}
DirectoryEntry* AddEntry(const char *const path, const DirectoryEntry *const entry) {
	DirectoryEntry* ent = GetEntryMkdirs(path, true);
	if (ent == nullptr) {
		//TODO: OH SHIT CRAP
	}
	if (!ent->IsDirectory) {
		printf("%s is a file!", path);
	}
	return AddEntryTo(ent->Data, entry);
}

DirectoryEntry* AddFile(const char *const path, const char *const name, File *const file) {
	DirectoryEntry ent;
	ent.ID = (++IDCount);
	ent.Name = strdup(name);
	ent.IsDirectory = false;
	ent.Data = malloc(sizeof(File));
	memcpy(ent.Data, file, sizeof(File));
	return AddEntry(path, &ent);
}
DirectoryEntry* AddDirectoryTo(Directory *const dir, const char *const name) {
	DirectoryEntry ent;
	ent.ID = (++IDCount);
	ent.Name = strdup(name);
	ent.IsDirectory = true;
	ent.Data = calloc(1, sizeof(Directory));
	return AddEntryTo(dir, &ent);
}
DirectoryEntry* AddDirectory(const char *const path, const char *const name) {
	DirectoryEntry ent;
	ent.ID = (++IDCount);
	ent.Name = strdup(name);
	ent.IsDirectory = true;
	ent.Data = calloc(1, sizeof(Directory));
	return AddEntry(path, &ent);
}

void InitFileSystem(void) {
	rootEntry.IsDirectory = true;
	rootEntry.Name = strdup("/");
	rootEntry.Data = &root;
	
	root.EntryCount = root.EntryCapacity = 0;
	root.Entries = nullptr;
	
	/*AddDirectory("/one/two/three//////////", "four");
	AddDirectory("/tes/t/ing/a/thing/", "potato");
	AddDirectory("/hi/", "i");
	AddDirectory("/hi/i", "i");
	File f;
	AddFile("/one/two/three//////////four/", "test", &f);*/
}

void DestructFileEntry(const DirectoryEntry *const ent) {
	free(ent->Name);
	free(ent->Data);
}

void DestructDirectoryEntry(const DirectoryEntry *const ent, bool notroot) {
	free(ent->Name);
	Directory *dir = ent->Data;
	for (unsigned int i = 0; i < dir->EntryCount; i++) {
		if (dir->Entries[i].IsDirectory)
			DestructDirectoryEntry(&dir->Entries[i], true);
		else
			DestructFileEntry(&dir->Entries[i]);
	}
	free(dir->Entries);
	if (notroot)
		free(dir);
}

void DestructFileSystem(void) {
	DestructDirectoryEntry(&rootEntry, false);
}

char* ReadString(int fd) {
	static char buf[512];
	char c = 0; int count = 0;
	while (true) {
		read(fd, &c, 1);
		if (c == 0)
			break;
		if (count < 512) // Discard the rest TODO: do better
			buf[count++] = c;
	}
	char* str = malloc(count+1);
	memcpy(str, buf, count);
	str[count] = 0;
	return str;
}

int ReadInt(int fd) {
	int i; read(fd, &i, sizeof(int));
	return i;
}
unsigned int ReadUInt(int fd) {
	unsigned int i; read(fd, &i, sizeof(unsigned int));
	return i;
}
unsigned short ReadUShort(int fd) {
	unsigned short i; read(fd, &i, sizeof(unsigned short));
	return i;
}

void ReadVPKHeader(int fd, VPKHeader *hdr) {
	hdr->Signature = ReadUInt(fd);
	hdr->Version = ReadUInt(fd);
	hdr->TreeLength = ReadUInt(fd);
}

void ReadVPK2Header(int fd, VPK2Header *hdr) {
	hdr->Unknown1 = ReadInt(fd);
	hdr->FooterLength = ReadUInt(fd);
	hdr->Unknown3 = ReadInt(fd);
	hdr->Unknown4 = ReadInt(fd);
}

void ReadDirectoryEntry(int fd, VPKDirectoryEntry *ent) {
	ent->CRC = ReadUInt(fd);
	ent->PreloadBytes = ReadUShort(fd);
	ent->ArchiveIndex = ReadUShort(fd);
	ent->EntryOffset = ReadUInt(fd);
	ent->EntryLength = ReadUInt(fd);
	ent->Terminator = ReadUShort(fd);
}

int OpenVPKArchive(VPK *const vpk, int id) {
	const char *dirloc = strstr(vpk->FileName, "dir");
	int diroff = dirloc-vpk->FileName, fd = -1;
	if (dirloc == nullptr) {
		LogE("Could not find archive name");
		return -1;
	} else {
		char *prefix = malloc(diroff+1);
		const char *suffix = vpk->FileName+diroff+3;
		memcpy(prefix, vpk->FileName, diroff); prefix[diroff] = 0;
		size_t prefLen = strlen(prefix), suffLen = strlen(suffix);
		size_t fnameLen = vpk->PathLen + prefLen + 3 + suffLen + 1;
		char *fname = malloc(fnameLen);

		// Suppress weird GCC warning that is treated as an error
		#ifdef __GNUC__
		#ifndef __clang__
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wformat-truncation"
		#endif
		#endif
		
		// 3-len id
		snprintf(fname, fnameLen, "%s%s%03d%s", vpk->Path, prefix, id % 1000u, suffix);
		if ((fd = open(fname, O_RDONLY)) != -1) goto OpenVPKArchiveEndTry;
		
		// 2-len id
		snprintf(fname, fnameLen, "%s%s%02d%s", vpk->Path, prefix, id % 100u, suffix);
		if ((fd = open(fname, O_RDONLY)) != -1) goto OpenVPKArchiveEndTry;

		#ifdef __GNUC__
		#ifndef __clang__
		#pragma GCC diagnostic pop
		#endif
		#endif
		
		// any-required-len id
		snprintf(fname, fnameLen, "%s%s%d%s", vpk->Path, prefix, id, suffix);
		if ((fd = open(fname, O_RDONLY)) != -1) goto OpenVPKArchiveEndTry;
		
		LogE("Could not find a suitable file for archive #%d", id);
		return -1;
		
OpenVPKArchiveEndTry:
		LogD("Opening VPK archive '%s' (%d)", fname, fd);
		free(fname);
		free(prefix);
	}
	return fd;
}

int OpenAllVPKArchives(VPK *const vpk) {
	int c = vpk->ArchiveFDCount;
	vpk->ArchiveFDs = malloc(c*sizeof(int));
	for (int i=0; i < c; i++) {
		vpk->ArchiveFDs[i] = OpenVPKArchive(vpk, i);
		if (vpk->ArchiveFDs[i] == -1)
			return i;
	}
	return 0x7fff;
}

int GetVPKArchive(VPK *const vpk, int id) {
	if (id == 0x7fff) return vpk->FD;
	int fd = vpk->ArchiveFDs[id];
	return fd;
}

void AddVPKFile(const char *const path, const char *const fname,
	const char *const ext, const VPKDirectoryEntry *const dirEnt, uint64 off) {
	File f;
	f.Size = dirEnt->PreloadBytes + dirEnt->EntryLength;
	f.PreloadSize = dirEnt->PreloadBytes;
	f.PreloadOffset = (f.PreloadSize > 0) ? off : 0;
	f.ArchiveIndex = dirEnt->ArchiveIndex;
	if (f.ArchiveIndex != 32767 && f.ArchiveIndex >= vpk.ArchiveFDCount) vpk.ArchiveFDCount = f.ArchiveIndex+1;
	f.DataSize = dirEnt->EntryLength;
	f.DataOffset = (f.DataSize) ? (((f.ArchiveIndex == 0x7fff) ? vpk.DataOffset : 0) + dirEnt->EntryOffset) : 0;
	char fnbuf[2048];
	const char *fPath, *fFname, *fExt, *dot;
	fPath = ((path[0] == ' ' && path[1] == 0)?"/":path);
	fFname = ((fname[0] == ' ' && fname[1] == 0)?"":fname);
	fExt = ((ext[0] == ' ' && ext[1] == 0)?"":ext);
	dot = ((fExt==0)?"":".");
	snprintf(fnbuf, 2048, "%s%s%s", fFname, dot, fExt);
	//printf("AddFile(\"%s\", \"%s%s%s\", 0x%X)\n", fPath, fFname, dot, fExt, &f);
	AddFile(fPath, fnbuf, &f);
}

void ReadDirectory(int fd) {
	while (true) {
		char *extension = ReadString(fd);
		if (extension[0] == 0) {
			free(extension);
			break;
		}
		while (true) {
			char *path = ReadString(fd);
			if (path[0] == 0) {
				free(path);
				break;
			}
			while (true) {
				char *fname = ReadString(fd);
				if (fname[0] == 0) {
					free(fname);
					break;
				}
				VPKDirectoryEntry dirEnt; ReadDirectoryEntry(fd, &dirEnt);
				AddVPKFile(path, fname, extension, &dirEnt, lseek(fd, 0, SEEK_CUR));
				// lseek() here is like ftell(), returns cursor position
				if (dirEnt.PreloadBytes > 0)
					lseek(fd, dirEnt.PreloadBytes, SEEK_CUR);
				free(fname);
			}
			free(path);
		}
		free(extension);
	}
}

static int vpk_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));
	stbuf->st_nlink = 1;
	DirectoryEntry *ent = GetEntry(path);
	if (ent == nullptr) {
		return -ENOENT;
	}
	
	stbuf->st_ino = ent->ID;
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	if (ent->IsDirectory) {
		Directory *dir = ((Directory*)ent->Data);
		stbuf->st_mode = S_IFDIR | 0555;
		stbuf->st_size = dir->EntryCount;
		return 0;
	} else {
		File *f = ((File*)ent->Data);
		stbuf->st_mode = S_IFREG | 0555;
		stbuf->st_size = f->Size;
		return 0;
	}
	return -EIO;
}

static int vpk_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	DirectoryEntry *ent = GetEntry(path);
	if (ent == nullptr)
		return -ENOENT;
	if (!ent->IsDirectory)
		return -ENOTDIR;
	Directory *dir = ((Directory*)ent->Data);

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	struct stat stat; memset(&stat, 0, sizeof(struct stat));
	for (unsigned int i = 0; i < dir->EntryCount; i++) {
		stat.st_ino = dir->Entries[i].ID;
		stat.st_mode = 555 | (dir->Entries[i].IsDirectory ? S_IFDIR : S_IFREG);
		filler(buf, dir->Entries[i].Name, &stat, 0);
	}

	return 0;
}

static int vpk_open(const char *path, struct fuse_file_info *fi) {
	DirectoryEntry *ent = GetEntry(path);
	if (ent == nullptr)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EROFS;
	
	return 0;
}

#define min(x, y) (x<y?x:y)

static int vpk_read(const char *path, char *buf, size_t size, off_t offset,
			struct fuse_file_info *fi) {
	(void) fi;
	DirectoryEntry *ent = GetEntry(path);
	if (ent == nullptr)
		return -ENOENT;
	if (ent->IsDirectory)
		return -EISDIR;
	File *f = ent->Data;
	if ((uint64) offset >= f->Size) {
		LogW("Read offset %"PRIu64" exceeds file size %"PRIu64" on \"%s\"", (uint64) offset, f->Size, path);
		return 0;
	}
	size_t pos = offset, end = min(offset+size, f->Size);

	if (f->PreloadSize > 0 && pos < f->PreloadSize) {
		int readsize = min(end-pos, f->PreloadSize);
		int read = pread(vpk.FD, buf, readsize, f->PreloadOffset+pos);
		if (read != readsize) {
			LogE("%s: preload read failed: %d/%d", path, read, readsize);
			return -EIO;
		}
		//LogD("Preread %d/%d out of %d total", read, readsize, size);
		pos += read;
	}
	
	if (end-pos > 0 && f->DataSize > 0) {
		int readsize = min(end-pos, f->DataSize);
		int fd = GetVPKArchive(&vpk, f->ArchiveIndex);
		if (fd == -1) {
			LogE("No FD for archive #%d (%s)", f->ArchiveIndex, path);
			return -EIO;
		}
		int read = pread(fd, buf+(pos-offset), readsize, f->DataOffset+pos);
		if (read != readsize) {
			LogE("%s: data read failed: %d/%d", path, read, readsize);
			return -EIO;
		}
		//LogD("Read %d/%d out of %d total (%d, %d)", read, readsize, size, end, pos);
		pos += read;
	}
	
	if (pos != end) {
		LogE("%s: read failed: %"PRIu64"/%"PRIu64" (A#%d, FD%d)", path, pos-offset, size, f->ArchiveIndex, GetVPKArchive(&vpk, f->ArchiveIndex));
		return -EIO;
	}
	
	return pos-offset;
}

static struct fuse_operations vpk_operations = {
	.getattr	= vpk_getattr,
	.readdir	= vpk_readdir,
	.open		= vpk_open,
	.read		= vpk_read,
};

int main(int argc, char *argv[]) {
	LogD("vpk_fuse v%s", VERSION_STRING);
	if (argc < 2) {
		printf("Usage: %s <filename> [FUSE flags] <FUSE mountpoint>\n", argv[0]);
		return 1;
	}
	char* path = realpath(argv[1], nullptr);
	{
		
		if (path == nullptr) {
			LogE("Could not open '%s': %s", argv[1], strerror(errno));
			free(path);
			return 100;
		}
		LogD("Opening '%s'", path);
		if ((vpk.FD = open(path, O_RDONLY)) == -1) {
			LogE("Could not open '%s': %s", path, strerror(errno));
			free(path);
			return 101;
		}
		int pathlen = strlen(path), pos = pathlen;
		for (; pos > 0 && (pos-1>=0&&path[pos-1] != '/'); pos--);
		vpk.Path = malloc(pos+1);
		memcpy(vpk.Path, path, pos); vpk.Path[pos] = 0;
		vpk.PathLen = pos;
		vpk.FileName = strdup(path+pos);
		vpk.FileNameLen = strlen(vpk.FileName);
	}
	ReadVPKHeader(vpk.FD, &vpk.Header);
	if (vpk.Header.Signature != VPKSig) {
		LogE("Invalid VPK sigature (0x%X)", vpk.Header.Signature);
		free(path);
		return 200;
	}
	if (vpk.Header.Version < 1 || vpk.Header.Version > 2) {
		LogE("Unsupported VPK version %d", vpk.Header.Version);
		free(path);
		return 201;
	}
	if (vpk.Header.Version == 2) {
		ReadVPK2Header(vpk.FD, &vpk.HeaderV2);
	}
	vpk.DataOffset = sizeof(VPKHeader) + ((vpk.Header.Version == 2)?sizeof(VPK2Header):0) + vpk.Header.TreeLength;

	InitFileSystem();
	ReadDirectory(vpk.FD);
	printf("%d\n",vpk.ArchiveFDCount);
	int arch = OpenAllVPKArchives(&vpk);
	if (arch != 0x7fff) {
		LogE("Failed opening archive #%d", arch);
		free(path);
		return 300;
	}
	if (vpk.ArchiveFDCount > 0)
		LogD("Opened all archives successfully");

	char **argvFuse = malloc(sizeof(char*) * argc);
	for (int i=1; i < argc; ++i) {
		argvFuse[i - 1] = argv[i];
	}
	const char *addArgsFmt = "-osubtype=vpk,noatime,ro,fsname=%s";
	argvFuse[argc - 1] = malloc(sizeof(char) * (strlen(addArgsFmt) + strlen(path)));
	sprintf(argvFuse[argc - 1], addArgsFmt, path);
	free(path);
	int fuseRet = fuse_main(argc, argvFuse, &vpk_operations, NULL);
	free(argvFuse[argc - 1]);
	free(argvFuse);
	close(vpk.FD);
	for (int i=0; i < vpk.ArchiveFDCount; i++) {
		close(vpk.ArchiveFDs[i]);
		LogD("Closing VPK archive #%d", vpk.ArchiveFDs[i]);
	}
	free(vpk.ArchiveFDs);
	free(vpk.FileName);
	free(vpk.Path);
	DestructFileSystem();
	return fuseRet;
}

