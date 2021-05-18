#include <inc/fs.h>
#include <inc/string.h>
#include <inc/lib.h>

#define debug 0

#define ROOT "/"
#define CUR_DIR "."
#define PREV_DIR ".."

static char cwd[MAXPATHLEN] = ROOT;

// scans the path for the last dir
// return a pointer to the last dir in the path
static char *find_last_dir(char *path) {
    int len = strlen(path);
    int i;
    int entry_index = 0;
    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            entry_index = i;
        }
    }
    return path + entry_index;
}

// finds the canonical representatin of the given path,
// starting from the root.
// returns 0 on success and stores the result in path_buff
// returns -E_BAD_PATH if the path cannot be canonalized
//
// NOTE: only handles full paths, relative paths with initial dot,
// double dot or empty start (works the same as starting with "./" )
//
// file names starting with . are not supported
static int canonalize_path(const char *path, char *path_buff) {

    assert(path != NULL);
    assert(path_buff != NULL);

    if (debug) {
        cprintf("canonalizing path %s, relative to CWD %s\n", path, cwd);
    }

    // start with an empty string for concatenation
    *path_buff = '\0';

    if (strncmp(path, ROOT, strlen(ROOT)) == 0) {
        // return full paths as is
    } else if (strncmp(path, PREV_DIR, strlen(PREV_DIR)) == 0) {
        // prepend CWD without last dir to relative path instead of ..
        path += strlen(PREV_DIR);

        if (strcmp(cwd, ROOT) == 0) {
            // no path before the root
            return -E_BAD_PATH;
        }

        strcpy(path_buff, cwd);
        // remove the last dir of the path
        *find_last_dir(path_buff) = '\0';
    } else if (strncmp(path, CUR_DIR, strlen(CUR_DIR)) == 0) {
        // prepend CWD to relative path instead of .
        path += strlen(CUR_DIR);

        strcpy(path_buff, cwd);
    } else {
        // prepend CWD to relative path
        strcpy(path_buff, cwd);
    }

    if (strlen(path_buff) + (strlen(path)) >= MAXPATHLEN) {
        return -E_BAD_PATH;
    }

    strcat(path_buff, path);

    int path_len = strlen(path_buff);
    if (path_len > 0 && (path_buff)[path_len - 1] == '/') {
        // remove trailing '/' from path
        (path_buff)[path_len - 1] = '\0';
    }

    if (strncmp(path_buff, "//", 2) == 0) {
        // pretended that "/" behaves like other paths for symmetry
        // now correct it.
        memmove(path_buff, path_buff + 1, strlen(path_buff) - 1);
        // remove repeated character at the end
        (path_buff)[strlen(path_buff) - 1] = '\0';
    }

    if (debug) {
        cprintf("returning canonical path %s\n", path_buff);
    }

    return 0;
}

union Fsipc fsipcbuf __attribute__((aligned(PGSIZE)));

// Send an inter-environment request to the file server, and wait for
// a reply.  The request body should be in fsipcbuf, and parts of the
// response may be written back to fsipcbuf.
// type: request code, passed as the simple integer IPC value.
// dstva: virtual address at which to receive reply page, 0 if none.
// Returns result from the file server.
static int
fsipc(unsigned type, void *dstva)
{
	static envid_t fsenv;
	if (fsenv == 0)
		fsenv = ipc_find_env(ENV_TYPE_FS);

	static_assert(sizeof(fsipcbuf) == PGSIZE);

	if (debug)
		cprintf("[%08x] fsipc %d %08x\n", thisenv->env_id, type, *(uint32_t *)&fsipcbuf);

	ipc_send(fsenv, type, &fsipcbuf, PTE_P | PTE_W | PTE_U);
	return ipc_recv(NULL, dstva, NULL);
}

static int devfile_flush(struct Fd *fd);
static ssize_t devfile_read(struct Fd *fd, void *buf, size_t n);
static ssize_t devfile_write(struct Fd *fd, const void *buf, size_t n);
static int devfile_stat(struct Fd *fd, struct Stat *stat);
static int devfile_trunc(struct Fd *fd, off_t newsize);

struct Dev devfile =
{
	.dev_id =	'f',
	.dev_name =	"file",
	.dev_read =	devfile_read,
	.dev_close =	devfile_flush,
	.dev_stat =	devfile_stat,
	.dev_write =	devfile_write,
	.dev_trunc =	devfile_trunc
};

// Open a file (or directory).
//
// Returns:
// 	The file descriptor index on success
// 	-E_BAD_PATH if the path is too long (>= MAXPATHLEN)
// 	< 0 for other errors.
int
open(const char *path, int mode)
{
	// Find an unused file descriptor page using fd_alloc.
	// Then send a file-open request to the file server.
	// Include 'path' and 'omode' in request,
	// and map the returned file descriptor page
	// at the appropriate fd address.
	// FSREQ_OPEN returns 0 on success, < 0 on failure.
	//
	// (fd_alloc does not allocate a page, it just returns an
	// unused fd address.  Do you need to allocate a page?)
	//
	// Return the file descriptor index.
	// If any step after fd_alloc fails, use fd_close to free the
	// file descriptor.

	int r;
	struct Fd *fd;

    char *path_buff = malloc();
    if (path_buff == NULL) {
        return -E_NO_MEM;
    }

	if (canonalize_path(path, path_buff) < 0)
		return -E_BAD_PATH;

	if ((r = fd_alloc(&fd)) < 0)
		return r;

	strcpy(fsipcbuf.open.req_path, path_buff);
	fsipcbuf.open.req_omode = mode;

	if ((r = fsipc(FSREQ_OPEN, fd)) < 0) {
		fd_close(fd, 0);
		return r;
	}

    free(path_buff);

	return fd2num(fd);
}

// Flush the file descriptor.  After this the fileid is invalid.
//
// This function is called by fd_close.  fd_close will take care of
// unmapping the FD page from this environment.  Since the server uses
// the reference counts on the FD pages to detect which files are
// open, unmapping it is enough to free up server-side resources.
// Other than that, we just have to make sure our changes are flushed
// to disk.
static int
devfile_flush(struct Fd *fd)
{
	fsipcbuf.flush.req_fileid = fd->fd_file.id;
	return fsipc(FSREQ_FLUSH, NULL);
}

// Read at most 'n' bytes from 'fd' at the current position into 'buf'.
//
// Returns:
// 	The number of bytes successfully read.
// 	< 0 on error.
static ssize_t
devfile_read(struct Fd *fd, void *buf, size_t n)
{
	// Make an FSREQ_READ request to the file system server after
	// filling fsipcbuf.read with the request arguments.  The
	// bytes read will be written back to fsipcbuf by the file
	// system server.
	int r;

	fsipcbuf.read.req_fileid = fd->fd_file.id;
	fsipcbuf.read.req_n = n;
	if ((r = fsipc(FSREQ_READ, NULL)) < 0)
		return r;
	assert(r <= n);
	assert(r <= PGSIZE);
	memmove(buf, fsipcbuf.readRet.ret_buf, r);
	return r;
}


// Write at most 'n' bytes from 'buf' to 'fd' at the current seek position.
//
// Returns:
//	 The number of bytes successfully written.
//	 < 0 on error.
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n)
{
	// Make an FSREQ_WRITE request to the file system server.  Be
	// careful: fsipcbuf.write.req_buf is only so large, but
	// remember that write is always allowed to write *fewer*
	// bytes than requested.
	// LAB 5: Your code here
	int r;
	// ensure that the size is at most the req_buf size
    n = MIN(n, PGSIZE - (sizeof(int) + sizeof(size_t)));
	fsipcbuf.write.req_fileid = fd->fd_file.id;
	fsipcbuf.write.req_n = n;
	memmove(fsipcbuf.write.req_buf, buf, n);
	if ((r = fsipc(FSREQ_WRITE, NULL)) < 0)
		return r;
	assert(r <= n);
	return r;
}

static int
devfile_stat(struct Fd *fd, struct Stat *st)
{
	int r;

	fsipcbuf.stat.req_fileid = fd->fd_file.id;
	if ((r = fsipc(FSREQ_STAT, NULL)) < 0)
		return r;
	strcpy(st->st_name, fsipcbuf.statRet.ret_name);
	st->st_size = fsipcbuf.statRet.ret_size;
	st->st_isdir = fsipcbuf.statRet.ret_isdir;
	return 0;
}

// Truncate or extend an open file to 'size' bytes
static int
devfile_trunc(struct Fd *fd, off_t newsize)
{
	fsipcbuf.set_size.req_fileid = fd->fd_file.id;
	fsipcbuf.set_size.req_size = newsize;
	return fsipc(FSREQ_SET_SIZE, NULL);
}


// Synchronize disk with buffer cache
int
sync(void)
{
	// Ask the file server to update the disk
	// by writing any dirty blocks in the buffer cache.

	return fsipc(FSREQ_SYNC, NULL);
}

