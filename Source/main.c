#include "pct.h"
#include <stdlib.h>
#include <stdio.h>

pthread_mutex_t print_mutex;

void* print_thread(void* arg) {
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&print_mutex);
            printf("LOOP# %d | Thread Priority: %d\n", i, pct_get_thread_priority());
        pthread_mutex_unlock(&print_mutex);
    }

    pct_markthread_done();
    return 0;
}

#define pct_thread_count 4
int main() {
    pct_init();

    pthread_mutex_init(&print_mutex, NULL);
    pthread_t threads[pct_thread_count] = {NULL};
    for (int i = 0; i < pct_thread_count; i++) {
        pthread_create(&threads[i], NULL, print_thread, NULL);
    }

    for (int i = 0; i < pct_thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    pct_shutdown();
    pthread_mutex_destroy(&print_mutex);

    return 0;
}