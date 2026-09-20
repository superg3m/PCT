// 02_rare_nested_lock.cpp

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int counter = 0;

void update_counter() {
    pthread_mutex_lock(&mutex);

    counter++;

    pthread_mutex_unlock(&mutex);
}

void* worker(void*) {
    for (int i = 0; i < 100000; i++) {

        pthread_mutex_lock(&mutex);

        if (i == 99999) {
            // Rare path.
            update_counter();
        }

        counter++;

        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

int main() {
    pthread_t threads[4];

    for (int i = 0; i < 4; i++)
        pthread_create(&threads[i], NULL, worker, NULL);

    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    printf("%d\n", counter);
}