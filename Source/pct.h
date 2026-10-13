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

    // TODO(Jovanni): Make this be tracked in the pct_sem_count map
    #define sem_destroy(s) dispatch_release(*(s))
#else
    #include <semaphore.h>
#endif

#if defined(PCT_DISABLE)
    #define pct_init()
    #define pct_shutdown()
    #define pct_get_thread_priority() 0
#else
    #ifdef __cplusplus
    extern "C" {
    #endif
        void pct_init();
        void pct_shutdown();
        int pct_get_thread_priority();
        int pct_pthread_create(pthread_t* thread_id, const pthread_attr_t* attribute, void*(*func)(void*), void* arg);
        int pct_pthread_mutex_lock(pthread_mutex_t* mutex, const char* function, const char* file, int line);
        int pct_pthread_mutex_unlock(pthread_mutex_t* mutex, const char* function, const char* file, int line);
        int pct_sem_init(sem_t* s, int pshared, unsigned int value);
        int pct_sem_wait(sem_t* s, const char* function, const char* file, int line);
        int pct_sem_post(sem_t* s, const char* function, const char* file, int line);
        int pct_pthread_join(pthread_t thread, void** ret, const char* function, const char* file, int line);
    #ifdef __cplusplus
    }
    #endif
#endif

#if !defined(PCT_BOOTSTRAP)
    #if !defined(PCT_DISABLE)

        #define sem_init(s, pshared, value) pct_sem_init(s, pshared, value)
        #define pthread_create(thread_id, attribute, func, arg) pct_pthread_create(thread_id, attribute, func, arg)
        #define pthread_mutex_lock(_mutex) pct_pthread_mutex_lock((_mutex),  __func__, __FILE__, __LINE__)
        #define pthread_mutex_unlock(_mutex) pct_pthread_mutex_unlock((_mutex),  __func__, __FILE__, __LINE__)

        #define sem_wait(s) pct_sem_wait(s,  __func__, __FILE__, __LINE__)
        #define sem_post(s) pct_sem_post(s,  __func__, __FILE__, __LINE__)

        #define pthread_join(thread, ret) pct_pthread_join(thread, ret,  __func__, __FILE__, __LINE__)
    #else
        #if defined(__APPLE__) || defined(__MACH__)
            #define sem_wait(s) dispatch_semaphore_wait(*(s), DISPATCH_TIME_FOREVER)
            #define sem_post(s) dispatch_semaphore_signal(*(s))
        #endif
    #endif
#endif