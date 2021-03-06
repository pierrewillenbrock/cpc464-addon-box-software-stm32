
#pragma once

#include <errno.h>
#include <unordered_map>
#include "refcounted.hpp"
#include "bits.h"

/** \brief Virtual File System implementation
 */
namespace vfs {

	struct Dentry;

	struct Inode : public Refcounted<Inode> {
		mode_t mode;
		off_t size;
		Inode() : mode(0), size(0) {}
		virtual ~Inode() {}
		static void aiocompleter(int result, int _errno, volatile int *done, volatile int *aiores, volatile int *aioerrno) {
			*aiores = result;
			*aioerrno = _errno;
			swbarrier();
			*done = 1;
		}
		virtual _ssize_t pread(void *ptr, size_t len, off_t offset) {
			aio::PReadCommand cmd;
			cmd.len = len;
			cmd.offset = offset;
			cmd.ptr = ptr;
			volatile int d = 0;
			volatile int aiores = 0;
			volatile int aioerrno = 0;
			cmd.slot = sigc::bind(sigc::ptr_fun(aiocompleter), &d, &aiores, &aioerrno);
			int res = pread(&cmd);
			if (res != 0)
				return res;
			while(!d)
				sched_yield();
			errno = aioerrno;
			return aiores;
		}
		virtual _ssize_t pwrite(const void *ptr, size_t len, off_t offset) {
			aio::PWriteCommand cmd;
			cmd.len = len;
			cmd.offset = offset;
			cmd.ptr = ptr;
			volatile int d = 0;
			volatile int aiores = 0;
			volatile int aioerrno = 0;
			cmd.slot = sigc::bind(sigc::ptr_fun(aiocompleter), &d, &aiores, &aioerrno);
			int res = pwrite(&cmd);
			if (res != 0)
				return res;
			while(!d)
				sched_yield();
			errno = aioerrno;
			return aiores;
		}
		virtual _ssize_t pread(aio::PReadCommand * /*command*/) {
			errno = EINVAL;
			return -1;
		}
		virtual _ssize_t pwrite(aio::PWriteCommand * /*command*/) {
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

	void Setup();
	int Mount(const char *mountpoint, RefPtr<Inode> ino);
	int Unmount(const char *mountpoint);

	/* to be implemented by main */
	void RegisterFilesystem(const char *type, RefPtr<Inode> ino);
	void UnregisterFilesystem(RefPtr<Inode> ino);
}
