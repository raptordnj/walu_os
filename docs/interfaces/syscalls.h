#ifndef WALUOS_SYSCALLS_H
#define WALUOS_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef int64_t off_t;
typedef int64_t ssize_t;

/* Process and execution */
pid_t sys_fork(void);
int sys_execve(const char *path, char *const argv[], char *const envp[]);
pid_t sys_waitpid(pid_t pid, int *status, int options);
void sys_exit(int status);

/* File operations */
int sys_openat(int dirfd, const char *path, int flags, mode_t mode);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);
int sys_close(int fd);
int sys_chmod(const char *path, mode_t mode);
int sys_chown(const char *path, uid_t uid, gid_t gid);

/* Identity */
int sys_setuid(uid_t uid);
int sys_setgid(gid_t gid);
uid_t sys_getuid(void);
gid_t sys_getgid(void);

/* Memory */
void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int sys_munmap(void *addr, size_t len);

/* Signals and process control */
int sys_kill(pid_t pid, int sig);
int sys_sigaction(int sig, const void *act, void *oldact);

/* Mount */
int sys_mount(const char *src, const char *target, const char *fstype, unsigned long flags, const void *data);
int sys_umount2(const char *target, int flags);

/* Networking */
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const void *addr, uint32_t addrlen);
int sys_listen(int fd, int backlog);
int sys_connect(int fd, const void *addr, uint32_t addrlen);
int sys_accept4(int fd, void *addr, uint32_t *addrlen, int flags);

/* Eventing */
int sys_poll(void *fds, uint32_t nfds, int timeout_ms);
int sys_epoll_create1(int flags);
int sys_epoll_ctl(int epfd, int op, int fd, void *event);
int sys_epoll_wait(int epfd, void *events, int maxevents, int timeout_ms);

/* Capability model (optional milestone) */
int sys_capget(void *hdrp, void *datap);
int sys_capset(void *hdrp, const void *datap);

#endif
