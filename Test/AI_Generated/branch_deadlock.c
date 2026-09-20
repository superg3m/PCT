// 04_branch_deadlock.cpp

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

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

    return NULL;
}

int main() {
    pthread_t a;
    pthread_t b;

    pthread_create(&a, NULL, account_thread, NULL);
    pthread_create(&b, NULL, logger_thread, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);
}