
#include <reent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

_ssize_t _read_r(struct _reent *r, int file, void *ptr, size_t len)
{
	return -1;
}

static volatile char stdout_buffer[256];

_ssize_t _write_r (
	struct _reent *r,
	int file,
	const void *ptr,
	size_t len)
{
	if (len > sizeof(stdout_buffer)) {
		memcpy((void*)stdout_buffer,
		       ((char*)ptr)+len-sizeof(stdout_buffer),
		       sizeof(stdout_buffer));
		return len;
	}
	memmove((void*)stdout_buffer,
		(void*)(stdout_buffer+sizeof(stdout_buffer)-len),
		sizeof(stdout_buffer)-len);
	memcpy((void*)(stdout_buffer+sizeof(stdout_buffer)-len), ptr, len);
	return len;
}

int _open_r(struct _reent *r,
	    const char *file, int flags, int mode )
{
	//we could implement a filesystem here. fd #0, 1, 2 are fixed and will not
	//be opened
	r->_errno = ENODEV;
	return -1;
}

int _close_r(
	struct _reent *r,
	int file)
{
        return 0;
}

_off_t _lseek_r(
	struct _reent *r,
	int file,
	_off_t ptr,
	int dir)
{
        return (_off_t)0;       /*  Always indicate we are at file beginning.   */
}


int _fstat_r(
	struct _reent *r,
	int file,
	struct stat *st)
{
        /*  Always set as character device.                             */
        st->st_mode = 0;/* don't set S_IFCHR, else newlib tries to test for
                           tty using isatty (not _isatty_r) */
	/* assigned to strong type with implicit        */
	/* signed/unsigned conversion.  Required by     */
	/* newlib.                                      */

        return 0;
}

