
#pragma once

#include <errno.h>
#include <unordered_map>
#include "refcounted.hpp"
#include "bits.h"

struct Dentry;

struct Inode : public Refcounted<Inode> {
	mode_t mode;
	off_t size;
	Inode() : mode(0), size(0) {}
	virtual ~Inode() {}
	virtual _ssize_t pread(void */*ptr*/, size_t /*len*/, off_t /*offset*/) {
		errno = EINVAL;
		return -1;
	}
	virtual _ssize_t pwrite(const void */*ptr*/, size_t /*len*/, off_t /*offset*/) {
		errno = EINVAL;
		return -1;
	}
	virtual _ssize_t pread_nb(PReadCommand * /*command*/) {
		errno = EINVAL;
		return -1;
	}
	virtual _ssize_t pwrite_nb(PWriteCommand * /*command*/) {
		errno = EINVAL;
		return -1;
	}
	virtual int fstat(struct stat *st) {
		memset(st, 0, sizeof(*st));
		st->st_mode = mode;
		st->st_size = size;
		return 0;
	}
	virtual int mkdir(RefPtr<Dentry> /*dent*/, mode_t /*mode*/) {
		errno = ENOTDIR;
		return -1;
	}
	virtual int lookup(RefPtr<Dentry> /*dent*/) {
		errno = ENOTDIR;
		return -1;
	}
	virtual int mknod(RefPtr<Dentry> /*dent*/, mode_t /*mode*/,
			  RefPtr<Inode> /*ino*/) {
		errno = ENOTDIR;
		return -1;
	}
	virtual int create(RefPtr<Dentry> /*dent*/, mode_t /*mode*/) {
		errno = ENOTDIR;
		return -1;
	}
	virtual bool readdir(off_t &/*d_off*/, std::string &/*name*/) {
		return false;
	}
};

struct Dentry : public Refcounted<Dentry>  {
	std::string name;
	RefPtr<Inode> inode;
	RefPtr<Dentry> parent;
	std::unordered_map<std::string, RefPtr<Dentry> > children;
	bool fully_populated;
	Dentry(std::string const &name, RefPtr<Dentry> parent)
		: name(name)
		, parent(parent)
		, fully_populated(false)
		{}
	void insertChild(RefPtr<Dentry> const &dent) {
		ISR_Guard g;
		children.insert(std::make_pair(dent->name,dent));
	}
	RefPtr<Dentry> lookup(std::string const &name) {
		ISR_Guard g;
		auto it = children.find(name);
		if (it == children.end())
			return RefPtr<Dentry>();
		return it->second;
	}
};

void VFS_Setup();
int VFS_Mount(const char *mountpoint, RefPtr<Inode> ino);
int VFS_Unmount(const char *mountpoint);

/* to be implemented by main */
void VFS_RegisterFilesystem(const char *type, RefPtr<Inode> ino);
void VFS_UnregisterFilesystem(RefPtr<Inode> ino);
