#include "sys.h"

#include <stdlib.h>
#include <time.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint64_t sys_time_ms(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart / (freq.QuadPart / 1000));
}

uint64_t sys_entropy(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)now.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ (uint64_t)time(NULL);
}

typedef struct {
    SysThreadFn fn;
    void *arg;
} ThreadStart;

static DWORD WINAPI thread_trampoline(LPVOID param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    start.fn(start.arg);
    return 0;
}

int sys_thread_start(SysThreadFn fn, void *arg) {
    ThreadStart *start = (ThreadStart *)malloc(sizeof(ThreadStart));
    if (!start) return -1;
    start->fn = fn;
    start->arg = arg;
    HANDLE h = CreateThread(NULL, 8u * 1024u * 1024u, thread_trampoline, start, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!h) {
        free(start);
        return -1;
    }
    CloseHandle(h);
    return 0;
}

struct SysMutex {
    CRITICAL_SECTION cs;
};

SysMutex *sys_mutex_create(void) {
    SysMutex *m = (SysMutex *)malloc(sizeof(SysMutex));
    if (m) InitializeCriticalSection(&m->cs);
    return m;
}

void sys_mutex_destroy(SysMutex *m) {
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    free(m);
}

void sys_mutex_lock(SysMutex *m) { EnterCriticalSection(&m->cs); }
void sys_mutex_unlock(SysMutex *m) { LeaveCriticalSection(&m->cs); }

#else

#include <pthread.h>
#include <unistd.h>

uint64_t sys_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

uint64_t sys_entropy(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec << 30) ^ (uint64_t)ts.tv_nsec ^ ((uint64_t)getpid() << 48);
}

typedef struct {
    SysThreadFn fn;
    void *arg;
} ThreadStart;

static void *thread_trampoline(void *param) {
    ThreadStart start = *(ThreadStart *)param;
    free(param);
    start.fn(start.arg);
    return NULL;
}

int sys_thread_start(SysThreadFn fn, void *arg) {
    ThreadStart *start = (ThreadStart *)malloc(sizeof(ThreadStart));
    if (!start) return -1;
    start->fn = fn;
    start->arg = arg;
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    /* Recursive search on copied positions; give it a comfortable stack. */
    pthread_attr_setstacksize(&attr, 8u * 1024u * 1024u);
    int rc = pthread_create(&thread, &attr, thread_trampoline, start);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(start);
        return -1;
    }
    return 0;
}

struct SysMutex {
    pthread_mutex_t mutex;
};

SysMutex *sys_mutex_create(void) {
    SysMutex *m = (SysMutex *)malloc(sizeof(SysMutex));
    if (m) pthread_mutex_init(&m->mutex, NULL);
    return m;
}

void sys_mutex_destroy(SysMutex *m) {
    if (!m) return;
    pthread_mutex_destroy(&m->mutex);
    free(m);
}

void sys_mutex_lock(SysMutex *m) { pthread_mutex_lock(&m->mutex); }
void sys_mutex_unlock(SysMutex *m) { pthread_mutex_unlock(&m->mutex); }

#endif
