// 01_rare_deadlock.cpp

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

void* worker_a(void*) {
    for (;;) {
        pthread_mutex_lock(&a);

        usleep(1);

        pthread_mutex_lock(&b);

        printf("A did work\n");

        pthread_mutex_unlock(&b);
        pthread_mutex_unlock(&a);
    }
}

void* worker_b(void*) {
    for (;;) {
        pthread_mutex_lock(&b);

        usleep(1);

        pthread_mutex_lock(&a);

        printf("B did work\n");

        pthread_mutex_unlock(&a);
        pthread_mutex_unlock(&b);
    }
}

int main() {
    pthread_t ta, tb;

    pthread_create(&ta, NULL, worker_a, NULL);
    pthread_create(&tb, NULL, worker_b, NULL);

    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
}