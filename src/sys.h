#ifndef CHESSPLATE_SYS_H
#define CHESSPLATE_SYS_H

/* Minimal OS services: monotonic clock, detached threads and mutexes.
 * Implemented with POSIX on Unix-like systems and Win32 on Windows. */

#include <stdint.h>

uint64_t sys_time_ms(void);
uint64_t sys_entropy(void);

typedef void (*SysThreadFn)(void *arg);
/* Starts a detached thread. Returns 0 on success. */
int sys_thread_start(SysThreadFn fn, void *arg);

typedef struct SysMutex SysMutex;
SysMutex *sys_mutex_create(void);
void sys_mutex_destroy(SysMutex *m);
void sys_mutex_lock(SysMutex *m);
void sys_mutex_unlock(SysMutex *m);

#endif
