// 01_common_deadlock.cpp

// NOTE(Jovanni): Interestingly this can no longer deadlock if I wait for all threads to be waiting
// However because you can adjust the pct config you can get different behavior which is neat.


#include "../../Source/pct.h"
#include <stdio.h>

pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

void* worker_a(void*) {
    for (;;) {
        pthread_mutex_lock(&a);
        pthread_mutex_lock(&b);

        printf("A did work\n");

        pthread_mutex_unlock(&b);
        pthread_mutex_unlock(&a);
    }

    pct_markthread_done();
    return 0;
}

void* worker_b(void*) {
    for (;;) {
        pthread_mutex_lock(&b);
        pthread_mutex_lock(&a);

        printf("B did work\n");

        pthread_mutex_unlock(&a);
        pthread_mutex_unlock(&b);
    }

    pct_markthread_done();
    return 0;
}

int main() {
    pct_init();
    pthread_t ta, tb;

    pthread_create(&ta, NULL, worker_a, NULL);
    pthread_create(&tb, NULL, worker_b, NULL);

    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    pct_shutdown();
    return 0;
}