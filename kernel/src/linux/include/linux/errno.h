/* BrysOS: códigos de error de Linux (mismos números que en Linux x86) */
#ifndef _BRYS_LINUX_ERRNO_H
#define _BRYS_LINUX_ERRNO_H

#include <linux/types.h>
#include <linux/compiler.h>

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define EDEADLK 35
#define ENAMETOOLONG 36
#define ENOLCK 37
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define ENOMSG 42
#define ENODATA 61
#define ETIME 62
#define ENOLINK 67
#define EPROTO 71
#define EBADMSG 74
#define EOVERFLOW 75
#define EILSEQ 84
#define EUSERS 87
#define ENOTSOCK 88
#define EDESTADDRREQ 89
#define EMSGSIZE 90
#define EPROTOTYPE 91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP 95
#define ENOTSUPP 524
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define ENETDOWN 100
#define ENETUNREACH 101
#define ENETRESET 102
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#define EHOSTDOWN 112
#define EHOSTUNREACH 113
#define EALREADY 114
#define EINPROGRESS 115
#define ECANCELED 125
#define ENOKEY 126
#define EKEYEXPIRED 127
#define EKEYREJECTED 129
#define ERFKILL 132
#define EPROBE_DEFER 517
#define ERESTARTSYS 512
#define EWOULDBLOCK EAGAIN

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void*)(x) >= (unsigned long)-MAX_ERRNO)
static inline void* ERR_PTR(long error) { return (void*)error; }
static inline long PTR_ERR(const void* ptr) { return (long)ptr; }
static inline bool IS_ERR(const void* ptr) { return (unsigned long)ptr >= (unsigned long)-MAX_ERRNO; }
static inline bool IS_ERR_OR_NULL(const void* ptr) { return !ptr || IS_ERR(ptr); }
static inline void* ERR_CAST(const void* ptr) { return (void*)ptr; }
static inline int PTR_ERR_OR_ZERO(const void* ptr) { return IS_ERR(ptr) ? (int)PTR_ERR(ptr) : 0; }

#endif
