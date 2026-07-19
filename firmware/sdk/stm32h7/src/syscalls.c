#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

int _close(int file) { (void)file; return -1; }
int _fstat(int file, struct stat *status) {
  (void)file;
  if (status != NULL) status->st_mode = S_IFCHR;
  return 0;
}
int _isatty(int file) { (void)file; return 1; }
off_t _lseek(int file, off_t offset, int whence) {
  (void)file; (void)offset; (void)whence; return 0;
}
ssize_t _read(int file, void *buffer, size_t bytes) {
  (void)file; (void)buffer; (void)bytes; errno = EINVAL; return -1;
}
ssize_t _write(int file, const void *buffer, size_t bytes) {
  (void)file; (void)buffer; return (ssize_t)bytes;
}
void *_sbrk(ptrdiff_t increment) {
  (void)increment; errno = ENOMEM; return (void *)-1;
}
int _getpid(void) { return 1; }
int _kill(int pid, int signal) {
  (void)pid; (void)signal; errno = EINVAL; return -1;
}
void _exit(int status) { (void)status; for (;;) {} }

