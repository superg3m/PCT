/*
My current plan of action is to do a job system basically. But just create thread and run the func(arg)
This should mean semaphore waits and mutex stuff should still work properly


VERY IMPORTANT: Write the usage code first, I want to make as few compromises as possible

OK I solved it:
mutex locks and semaphore waits will put the thread to sleep waiting for the main thread to wake it up

basically I want the thread id to uniquely map to an array slot (I could use my hashmap...)
*/

#include <pthread.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__MACH__)
    #include <dispatch/dispatch.h>
    #define sem_t dispatch_semaphore_t
    #define sem_init(s, pshared, value) (*s = dispatch_semaphore_create(value), (int)(*s != NULL))
    #define sem_wait(s) dispatch_semaphore_wait(*s, DISPATCH_TIME_FOREVER);
    #define sem_post(s) dispatch_semaphore_signal(*s);
    #define sem_destroy(s) dispatch_release(*s)
#else
    #include <semaphore.h>
#endif

void pct_init();
void pct_shutdown();
int pct_get_thread_priority();
void pct_markthread_done();
int pct_pthread_create(pthread_t* restrict thread_id, const pthread_attr_t *restrict attribute, void*(*func)(void*), void *restrict arg);
int pct_pthread_mutex_lock(pthread_mutex_t* mutex);

#if !defined(PCT_BOOTSTRAP)
    #define pthread_create(thread_id, attribute, func, arg) pct_pthread_create(thread_id, attribute, func, arg)
    #define pthread_mutex_lock(mutex) pct_pthread_mutex_lock(mutex)
#endif