#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
// #include <pthread.h>
#include "../../Source/pct.h"

extern pthread_mutex_t  write_mutex;

#define ArrayCount(arr) (sizeof(arr) / sizeof(arr[0]))
#define BUFFER_SIZE 0x800
#define OUT(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); pthread_mutex_lock(&write_mutex); int unused = write(STDOUT_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); (void)unused; pthread_mutex_unlock(&write_mutex); } while(false)
#define ERR(fmt, ...) do { char buffer##__LINE__[BUFFER_SIZE]; memset(buffer##__LINE__, 0, BUFFER_SIZE); sprintf(buffer##__LINE__, fmt, ##__VA_ARGS__); pthread_mutex_lock(&write_mutex); int unused = write(STDERR_FILENO, buffer##__LINE__, strlen(buffer##__LINE__)); (void)unused; pthread_mutex_unlock(&write_mutex); } while(false)
// NOTE(Jovanni): OUT() and ERR() are nice helper macros that avoid variable shadowing by using #__LINE__
// it also stack allocates a character array.

// ./prog4 m n t
typedef struct ThreadData {
	int foodpot_count;     // n
	int baby_eagle_count;  // m
	int max_refill_count;  // t

	int baby_started_count;

	sem_t* foodpot;
	sem_t* food_sems;
	bool* food;

	sem_t wake_mom;
	sem_t wake_main;

	pthread_mutex_t mutex;
} ThreadData;

typedef struct MomThreadData {
	pthread_t* baby_thread_ids;
	ThreadData* data;
} MomThreadData;

typedef struct BabyThreadData {
	int baby_id;
	ThreadData* data;
} BabyThreadData;

void* mom_thread(void* arg);
void* baby_thread(void* arg);