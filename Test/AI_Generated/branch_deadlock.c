// 04_branch_deadlock.cpp

// NOTE(Jovanni): Interestingly this can no longer deadlock if I wait for all threads to be waiting
// However because you can adjust the pct config you can get different behavior which is neat.

#include "../../Source/pct.h"
#include <stdio.h>

pthread_mutex_t account_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void* account_thread(void*) {
    for (int i = 0; i < 100000; i++) {
        pthread_mutex_lock(&account_mutex);

        if (i == 54321) {
            pthread_mutex_lock(&log_mutex);

            printf("Account event\n");

            pthread_mutex_unlock(&log_mutex);
        }

        pthread_mutex_unlock(&account_mutex);
    }

    pct_markthread_done();
    return NULL;
}

void* logger_thread(void*) {
    for (int i = 0; i < 100000; i++) {
        pthread_mutex_lock(&log_mutex);

        if (i == 54321) {
            pthread_mutex_lock(&account_mutex);

            printf("Logger event\n");

            pthread_mutex_unlock(&account_mutex);
        }

        pthread_mutex_unlock(&log_mutex);
    }

    pct_markthread_done();
    return NULL;
}

int main() {
    pct_init();
    pthread_t a;
    pthread_t b;

    pthread_create(&a, NULL, account_thread, NULL);
    pthread_create(&b, NULL, logger_thread, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);

    pct_shutdown();
    return 0;
}