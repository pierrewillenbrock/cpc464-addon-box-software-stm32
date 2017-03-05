
#define __LINUX_ERRNO_EXTENSIONS__
#include <stdlib.h>
#include <reent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <string>
#include <assert.h>
#include <dirent.h>
#include <irq.h>
#include <fs/vfs.hpp>
#include <bits.h>

struct File : public Refcounted<File>  {
	RefPtr<Dentry> dentry;
	int openflags;
	size_t offset;
	File(RefPtr<Dentry> dentry, int openflags)
		: dentry(dentry)
		, openflags(openflags)
		, offset(0)
		{}
};
/* logical structuring of file access primitives:
   FileDescriptors can point to the same data, but don't have to
   FileDescriptors can share offsets, but don't have to
   FileDescriptors can point to the same Filename, but don't have to

   Inode          => A particular ordered collection of bytes on storage.
                     Properties: data, size, permissions, ... (see stat)
   Dentry         => A filename pointing to an Inode. Multiple Files may point
                     to the same Inode.
                     Key: name
                     Properties: pointer to Inode
   File           => A pointer into an Inode, used for accessing data.
                     Properties: position, pointer to Inode
   FileDescriptor => A number used to access and OpenInode.
                     Key: number
                     Properties: pointer to an OpenInode
 */

static std::vector<RefPtr<File> > fds;

static RefPtr<Dentry> rootDentry;

extern "C" {
	_ssize_t _read_r(struct _reent *r, int file, void *ptr, size_t len);
	_ssize_t _write_r (struct _reent *r, int file, const void *ptr, size_t len);
	int _open_r(struct _reent *r, const char *file, int flags, int mode );
	int _close_r(struct _reent *r, int file);
	_off_t _lseek_r(struct _reent *r, int file, _off_t ptr, int dir);
	int _fstat_r(struct _reent *r, int file, struct stat *st);
	int mkdir(const char *path, mode_t mode);
	int access(const char *path, int mode);
}

static volatile char stdout_buffer[256];

struct StdOutInode : public Inode {
	StdOutInode() {
		mode = S_IFCHR | S_IWUSR | S_IWGRP | S_IWOTH |
			S_IRUSR | S_IRGRP | S_IROTH;
	}
	virtual _ssize_t pread(void */*ptr*/, size_t /*len*/, off_t /*offset*/) {
		return 0;
	}
	virtual _ssize_t pwrite(const void *ptr, size_t len, off_t /*offset*/) {
		//ignores offset
		if (len > sizeof(stdout_buffer)) {
			memcpy((void*)stdout_buffer,
			       ((char*)ptr)+len-sizeof(stdout_buffer),
			       sizeof(stdout_buffer));
			return len;
		}
		memmove((void*)stdout_buffer,
			(void*)(stdout_buffer+len),
			sizeof(stdout_buffer)-len);
		memcpy((void*)(stdout_buffer+sizeof(stdout_buffer)-len), ptr, len);
		return len;
	}
};

struct NullInode : public Inode {
	NullInode() {
		mode = S_IFCHR | S_IWUSR | S_IWGRP | S_IWOTH |
			S_IRUSR | S_IRGRP | S_IROTH;
	}
	virtual _ssize_t pread(void */*ptr*/, size_t /*len*/, off_t /*offset*/) {
		return 0;
	}
	virtual _ssize_t pwrite(const void */*ptr*/, size_t len, off_t /*offset*/) {
		return len;
	}
};

struct VFSDirInode : public Inode {
	std::unordered_map<std::string, RefPtr<Inode> > children;
	virtual int mkdir(RefPtr<Dentry> dent, mode_t mode) {
		ISR_Guard g;
		assert(!dent->inode);
		assert(dent->parent);
		if (children.find(dent->name) != children.end()) {
			errno = EEXIST;
			return -1;
		}
		RefPtr<Inode> ino(new VFSDirInode());
		ino->mode = mode | S_IFDIR;
		children.insert(std::make_pair
				(dent->name, ino));
		dent->inode = ino;
		return 0;
	}
	virtual int lookup(RefPtr<Dentry> dent) {
		ISR_Guard g;
		assert(!dent->inode);
		assert(dent->parent);
		auto it = children.find(dent->name);
		if (it == children.end()) {
			errno = ENOENT;
			return -1;
		}
		dent->inode = it->second;
		return 0;
	}
	virtual int mknod(RefPtr<Dentry> dent, mode_t mode,
			  RefPtr<Inode> ino) {
		ISR_Guard g;
		assert(!dent->inode);
		assert(dent->parent);
		if (children.find(dent->name) != children.end()) {
			errno = EEXIST;
			return -1;
		}
		ino->mode = mode;
		children.insert(std::make_pair
				(dent->name, ino));
		dent->inode = ino;
		return 0;
	}
	virtual int create(RefPtr<Dentry> /*dent*/, mode_t /*mode*/) {
		//if we want to support files in memory, this is the place
		//to implement them.
		//if a file exists, and its inode is known at this point,
		//the directory inode could fully initialize the dent and
		//insert it as well.
		errno = ENOTDIR;
		return -1;
	}
	//first d_off is -1, -2 and -3 are reserved, rest is free for use.
	virtual bool readdir(off_t &d_off, std::string &name) {
		ISR_Guard g;
		int i = 0;
		for(auto it = children.begin(); it != children.end(); it++, i++) {
			if (i == d_off+1) {
				d_off++;
				name = it->first;
				return true;
			}
		}
		return false;
	}
};

void VFS_Setup() {
	fds.resize(3); //space for stdin, stdout and stderr
	rootDentry = new Dentry("", NULL);
	rootDentry->inode = new VFSDirInode();
	//this starts out empty, so we can mark it complete.
	rootDentry->fully_populated = true;
	rootDentry->parent = rootDentry;
	RefPtr<Dentry> devDentry = new Dentry("dev", rootDentry);
	rootDentry->inode->mkdir(devDentry, S_IRWXU | S_IRWXG | S_IRWXO);
	rootDentry->insertChild(devDentry);
	devDentry->fully_populated = true;
	RefPtr<Inode> null = new NullInode();
	RefPtr<Inode> stdout = new StdOutInode();
	RefPtr<Dentry> d = new Dentry("null", devDentry);
	devDentry->inode->mknod(d, S_IRWXU | S_IRWXG | S_IRWXO, null);
	devDentry->insertChild(d);
	fds[0] = new File(d, O_RDONLY);
	d = new Dentry("stdout", devDentry);
	devDentry->inode->mknod(d, S_IRWXU | S_IRWXG | S_IRWXO, stdout);
	devDentry->insertChild(d);
	fds[2] = fds[1] = new File(d, O_WRONLY);
	RefPtr<Dentry> mediaDentry = new Dentry("media", rootDentry);
	rootDentry->inode->mkdir(mediaDentry, S_IRWXU | S_IRWXG | S_IRWXO);
	rootDentry->insertChild(mediaDentry);
	mediaDentry->fully_populated = true;
}

_ssize_t _read_r(struct _reent */*r*/, int file, void *ptr, size_t len) {
	RefPtr<File> fp;
	{
		ISR_Guard isrguard;

		if (file == -1 || (unsigned)file >= fds.size()) {
			errno = EBADF;
			return -1;
		}
		if (!fds[file] ||
		    ((fds[file]->openflags & O_ACCMODE) != O_RDONLY &&
		     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
			errno = EBADF;
			return -1;
		}
		fp = fds[file];
	}
	_ssize_t res = fp->dentry->inode->pread(ptr, len, fp->offset);
	if (res != -1)
		fp->offset += (unsigned)res;
	return res;
}

_ssize_t _write_r (struct _reent */*r*/, int file, const void *ptr, size_t len) {
	RefPtr<File> fp;
	{
		ISR_Guard isrguard;

		if (file == -1 || (unsigned)file >= fds.size()) {
			errno = EBADF;
			return -1;
		}
		if (!fds[file] ||
		    ((fds[file]->openflags & O_ACCMODE) != O_WRONLY &&
		     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
			errno = EBADF;
			return -1;
		}
		fp = fds[file];
	}
	_ssize_t res = fp->dentry->inode->pwrite(ptr, len, fp->offset);
	if (res != -1)
		fp->offset += (unsigned)res;
	return res;
}

static RefPtr<Dentry> findParentDentry(const char *file,
					const char *&basename) {
	RefPtr<Dentry> dp = rootDentry;
	const char *fp = file;
	if (*fp == 0)
		return RefPtr<Dentry>();
	assert(*fp == '/'); //we don't do relative paths yet. have no notion of current working directory.
	fp++;//skip initial '/', we have it evaluated already.
	while(*fp) {
		const char *nfp = strchr(fp,'/');
		if (!nfp)
			break;
		std::string nm(fp, nfp-fp);
		RefPtr<Dentry> dc = dp->lookup(nm);
		if (!dc) {
			//create a new one, try the lookup
			dc = new Dentry(nm, dp);
			dp->inode->lookup(dc);
			//insert it even if there is no inode. that just means
			//its a negative entry, as opposed to a positive entry
			//with a valid inode.
			{
				ISR_Guard g;
				//check again if it is known.
				RefPtr<Dentry> dc2 = dp->lookup(nm);
				if (!dc2)
					dp->insertChild(dc);
			}
		}
		if (!dc->inode)
			return RefPtr<Dentry>();
		fp = nfp+1;//skip over '/'
		dp = dc;
	}
	basename = fp;
	return dp;
}

static RefPtr<Dentry> findDentry(const char *file) {
	const char*basename;
	RefPtr<Dentry> dp = findParentDentry(file, basename);
	if (!dp->inode)
		return RefPtr<Dentry>();
	if (!*basename) //trailing / or root directory
		return dp;
	std::string nm(basename);
	RefPtr<Dentry> dc = dp->lookup(nm);
	if (!dc) {
		//create a new one, try the lookup
		dc = new Dentry(nm, dp);
		dp->inode->lookup(dc);
		//insert it even if there is no inode. that just means
		//its a negative entry, as opposed to a positive entry
		//with a valid inode.
		{
			ISR_Guard g;
			//check again if it is known.
			RefPtr<Dentry> dc2 = dp->lookup(nm);
			if (!dc2)
				dp->insertChild(dc);
		}
	}
	return dc;
}

int _open_r(struct _reent */*r*/, const char *file, int flags, int mode ) {
	RefPtr<Dentry> dc;
	if (flags & O_CREAT) {
		const char *basename;
		RefPtr<Dentry> dp = findParentDentry(file, basename);
		if (!dp->inode) {
			errno = ENOENT;
			return -1;
		}
		{
			ISR_Guard g;
			dc = dp->lookup(basename);
			if (!dc) {
				//no entry in DentryCache? means this name has
				//never been encountered before. create a new
				//one. it may be discarded if there already
				//exists a file of that name, or the fs code
				//could populate the dentry cache using the
				//parent pointer.
				dc = new Dentry(basename, dp);
				dp->insertChild(dc);
			} else {
				if (dc->inode) {
					errno = EEXIST;
					return -1;
				}
			}
		}
		if (dp->inode->create(dc, mode) != 0)
			return -1;
	} else {
		dc = findDentry(file);
		if (!dc->inode) {
			errno = ENOENT;
			return -1;
		}
	}

	unsigned fd = -1;
	{
	  ISR_Guard isrguard;
	  for(fd = 0; fd < fds.size(); fd++) {
	    if (!fds[fd])
	      break;
	  }
	  if (fd == fds.size())  //no need to undo this, the entry in fds is marked as empty by default.
	    fds.resize(fd+1);
	}
	fds[fd] = new File(dc, flags);
	return fd;
}

int _close_r(struct _reent */*r*/, int file) {
	ISR_Guard isrguard;
	if (file == -1 || (unsigned)file >= fds.size()) {
		errno = EBADF;
		return -1;
	}
	if (!fds[file] ||
	    ((fds[file]->openflags & O_ACCMODE) != O_WRONLY &&
	     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
		errno = EBADF;
		return -1;
	}
	fds[file] = NULL;
        return 0;
}

_off_t _lseek_r(struct _reent */*r*/, int file, _off_t ptr, int dir) {
	ISR_Guard isrguard;
	if (file == -1 || (unsigned)file >= fds.size()) {
		errno = EBADF;
		return -1;
	}
	if (!fds[file]) {
		errno = EBADF;
		return -1;
	}
	if (!S_ISREG(fds[file]->dentry->inode->mode) &&
	    !S_ISBLK(fds[file]->dentry->inode->mode)) {
		errno = EINVAL;
		return -1;
	}
	_off_t off = fds[file]->offset;
	switch(dir) {
	case SEEK_SET: off = ptr; break;
	case SEEK_CUR: off += ptr; break;
	case SEEK_END: off = fds[file]->dentry->inode->size + ptr; break;
	default:
		errno = EINVAL;
		return -1;
	}
	if ((S_ISBLK(fds[file]->dentry->inode->mode) &&
	     off > fds[file]->dentry->inode->size) ||
		off < 0) {
		errno = EINVAL;
		return -1;
	}
	fds[file]->offset = off;
	return 0;
}


int _fstat_r(struct _reent */*r*/, int file, struct stat *st) {
	ISR_Guard isrguard;
	if (file == -1 || (unsigned)file >= fds.size()) {
		errno = EBADF;
		return -1;
	}
	if (!fds[file] ||
	    ((fds[file]->openflags & O_ACCMODE) != O_RDONLY &&
	     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
		errno = EBADF;
		return -1;
	}

	return fds[file]->dentry->inode->fstat(st);
}

int pread_nb(int file, struct PReadCommand *command) {
	ISR_Guard isrguard;
	if (file == -1 || (unsigned)file >= fds.size()) {
		errno = EBADF;
		return -1;
	}
	if (!fds[file] ||
	    ((fds[file]->openflags & O_ACCMODE) != O_RDONLY &&
	     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
		errno = EBADF;
		return -1;
	}

	return fds[file]->dentry->inode->pread_nb(command);
}

int pwrite_nb(int file, struct PWriteCommand *command) {
	ISR_Guard isrguard;
	if (file == -1 || (unsigned)file >= fds.size()) {
		errno = EBADF;
		return -1;
	}
	if (!fds[file] ||
	    ((fds[file]->openflags & O_ACCMODE) != O_WRONLY &&
	     (fds[file]->openflags & O_ACCMODE) != O_RDWR)) {
		errno = EBADF;
		return -1;
	}

	return fds[file]->dentry->inode->pwrite_nb(command);
}

struct RealDIR {
	RefPtr<Dentry> dir;
	std::vector<char> dentstore;
};

DIR *opendir (const char *name) {
	ISR_Guard isrguard;
	RefPtr<Dentry> dc = findDentry(name);
	if (!dc->inode) {
		errno = ENOENT;
		return NULL;
	}
	RealDIR *dirp = new RealDIR();
	dirp->dir = dc;

	return (DIR*)dirp;
}

struct dirent *readdir (DIR *__dirp) {
	RealDIR *dirp = (RealDIR*)__dirp;
	struct dirent *dent = NULL;
	if(dirp->dentstore.size() == 0) {
		dirp->dentstore.resize(sizeof(struct dirent)+2);
		dent = (struct dirent*)dirp->dentstore.data();
		//first entry: '.'
		dent->d_ino = 0; //yeah, we don't really implement that...
		dent->d_off = -2;
		dent->d_type = DT_DIR;
		memcpy(dent->d_name,".",2);
		return dent;
	}
	dent = (struct dirent*)dirp->dentstore.data();
	if (dent->d_off == -3) {
		//fully listed
		return NULL;
	}
	if (dent->d_off == -2) {
		dirp->dentstore.resize(sizeof(struct dirent)+3);
		dent = (struct dirent*)dirp->dentstore.data();
		//second entry: '..'
		dent->d_off = -1;
		memcpy(dent->d_name,"..",3);
		return dent;
	}
	{
		ISR_Guard isrguard;
		if (dirp->dir->fully_populated) {
			//we do it ourselves.
			int i = 0;
			for(auto it = dirp->dir->children.begin();
			    it != dirp->dir->children.end(); it++,i++) {
				if (i == dent->d_off + 1) {
					dirp->dentstore.resize(sizeof(struct dirent)+it->first.size()+1);
					dent = (struct dirent*)dirp->dentstore.data();
					dent->d_off++;
					if (S_ISBLK(it->second->inode->mode))
						dent->d_type = DT_BLK;
					if (S_ISCHR(it->second->inode->mode))
						dent->d_type = DT_CHR;
					if (S_ISDIR(it->second->inode->mode))
						dent->d_type = DT_DIR;
					if (S_ISFIFO(it->second->inode->mode))
						dent->d_type = DT_FIFO;
					if (S_ISLNK(it->second->inode->mode))
						dent->d_type = DT_LNK;
					if (S_ISREG(it->second->inode->mode))
						dent->d_type = DT_REG;
					if (S_ISSOCK(it->second->inode->mode))
						dent->d_type = DT_SOCK;
					memcpy(dent->d_name,
					       it->first.c_str(),
					       it->first.size()+1);
					return dent;
				}
			}
			return NULL;
		}
	}

	//need to ask the inode for help.
	std::string name;
	//the inode inserts the name and modifies d_off so it can find
	//the next name.
	if (!dirp->dir->inode->readdir(dent->d_off, name)) {
		//we are done, no further entries
		dirp->dir->fully_populated = true;
		dent->d_off = -3;
		return NULL;
	}
	//got the name, check if we know it already
	RefPtr<Dentry> d = dirp->dir->lookup(name);
	if (!d) {
		d = new Dentry(name, dirp->dir);
		dirp->dir->inode->lookup(d);
		{
			ISR_Guard isrguard;
			if (!dirp->dir->lookup(name))
				dirp->dir->insertChild(d);
			else
				d = NULL;
		}
	}
	dirp->dentstore.resize(sizeof(struct dirent)+name.size()+1);
	dent = (struct dirent*)dirp->dentstore.data();
	if (S_ISBLK(d->inode->mode))
		dent->d_type = DT_BLK;
	if (S_ISCHR(d->inode->mode))
		dent->d_type = DT_CHR;
	if (S_ISDIR(d->inode->mode))
		dent->d_type = DT_DIR;
	if (S_ISFIFO(d->inode->mode))
		dent->d_type = DT_FIFO;
	if (S_ISLNK(d->inode->mode))
		dent->d_type = DT_LNK;
	if (S_ISREG(d->inode->mode))
		dent->d_type = DT_REG;
	if (S_ISSOCK(d->inode->mode))
		dent->d_type = DT_SOCK;
	memcpy(dent->d_name,
	       name.c_str(),
	       name.size()+1);
	return dent;
}

int closedir (DIR *__dirp) {
	ISR_Guard isrguard;
	RealDIR *dirp = (RealDIR*)__dirp;
	delete dirp;
	return 0;
}

int VFS_Mount(const char *mountpoint, RefPtr<Inode> ino) {
	ISR_Guard isrguard;
	RefPtr<Dentry> dc = findDentry(mountpoint);
	if (!dc->inode) {
		errno = ENOENT;
		return -1;
	}
	if (!S_ISDIR(dc->inode->mode)) {
		errno = ENOTDIR;
		return -1;
	}
	//and now just replace the ino of dc.
	dc->inode = ino;
	dc->fully_populated = false;
	return 0;
}

int VFS_Unmount(const char *mountpoint) {
	ISR_Guard isrguard;
	RefPtr<Dentry> dc = findDentry(mountpoint);
	if (!dc->inode) {
		errno = ENOENT;
		return -1;
	}
	if (!S_ISDIR(dc->inode->mode)) {
		errno = ENOTDIR;
		return -1;
	}
	//just drop the dentry from its parent.
	dc->parent->children.erase(dc->name);
	dc->parent->fully_populated = false;
	dc->parent = RefPtr<Dentry>();
	for(RefPtr<File> &file : fds) {
		RefPtr<Dentry> d = file->dentry;
		while(d && d != dc && d->parent != d) {
			d = d->parent;
		}
		if (d == dc)
			file = RefPtr<File>();
	}
	return 0;
}

int mkdir(const char *path, mode_t mode) {
	const char *basename;
	RefPtr<Dentry> dp = findParentDentry(path, basename);
	if (!dp->inode) {
		errno = ENOENT;
		return -1;
	}
	RefPtr<Dentry> dc;
	{
		ISR_Guard isrguard;
		dc = dp->lookup(basename);
		if (!dc) {
			//no entry in DentryCache? means this name has never
			//been encountered before. create a new one.
			//it may be discarded if there already exists a file
			//of that name, or the fs code could populate the
			//dentry cache using the parent pointer.
			dc = new Dentry(basename, dp);
			dp->insertChild(dc);
		} else {
			if (dc->inode) {
				errno = EEXIST;
				return -1;
			}
		}
	}
	if (dp->inode->mkdir(dc, mode) != 0)
		return -1;
	return 0;
}

int access(const char *path, int mode) {
	const char*basename;
	RefPtr<Dentry> dp = findParentDentry(path, basename);
	if (!dp->inode) {
		errno = ENOENT;
		return -1;
	}
	std::string nm(basename);
	RefPtr<Dentry> dc = dp->lookup(nm);
	if (!dc) {
		//create a new one, try the lookup
		dc = new Dentry(nm, dp);
		dp->inode->lookup(dc);
		//insert it even if there is no inode. that just means
		//its a negative entry, as opposed to a positive entry
		//with a valid inode.
		{
			ISR_Guard g;
			//check again if it is known.
			RefPtr<Dentry> dc2 = dp->lookup(nm);
			if (!dc2)
				dp->insertChild(dc);
		}
	}

	if (!dc->inode) {
		errno = EACCES;
		return -1;
	}

	if (mode & R_OK) {
		if (!(dc->inode->mode & (S_IRUSR | S_IRGRP | S_IROTH))) {
			errno = EACCES;
			return -1;
		}
	}

	if (mode & W_OK) {
		if (!(dc->inode->mode & (S_IWUSR | S_IWGRP | S_IWOTH))) {
			errno = EACCES;
			return -1;
		}
	}

	if (mode & X_OK) {
		if (!(dc->inode->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
			errno = EACCES;
			return -1;
		}
	}

	return 0;
}

