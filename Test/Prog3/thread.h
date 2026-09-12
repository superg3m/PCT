#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
// #include <pthread.h>
#include "../../Source/pct.h"

#define EVEN 0
#define ODD 1
#define ArrayCount(arr) (sizeof(arr) / sizeof(arr[0]))
#define BUFFER_SIZE 0x800
#define OUT(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); write(STDOUT_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); } while(false)
#define ERR(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); write(STDERR_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); } while(false)
// NOTE(Jovanni): OUT() and ERR() are nice helper macros that avoid variable shadowing by using #__LINE__
// it also stack allocates a character array.

typedef struct ThreadData {
    int swap_index_1;
    int swap_index_2;

    int* swap_address_1;
    int* swap_address_2;
} ThreadData;

void* thread_worker_sort(void* args);