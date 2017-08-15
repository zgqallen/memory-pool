#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "mmpool.h"

#define TH_NUM 100
MM_POOL *g_static_pool;
int g_ppid[TH_NUM] = {0};


#define IDX 10000
#define S_IDX 1

#ifdef GLIBC
MM_POOL* mmpool_init() {return NULL;}
#define mmpool_destroy(a)
#define mmpool_dump(a)
#define mmpool_dump_counter(a)
#define mmpool_malloc(a, b) malloc(b)
#define mmpool_free(a) free(a)
#endif

void *p_malloc_func(void *arg)
{
	int ppid = *(int*)arg;
	int idx;
	unsigned char *addr[IDX];

	printf("thread %d starting...\n", ppid);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool, idx);
		memset(addr[idx], 15, idx);
	}

	//mmpool_dump(g_static_pool);
	usleep(500);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		mmpool_free(addr[idx]);
	}

	//mmpool_dump(g_static_pool);
	usleep(500);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool, 256);
		memset(addr[idx], 15, 256);
	}

	//dump_mmpool(g_static_pool);
	usleep(500);
	for(idx = IDX-1; idx >= S_IDX; idx--)
	{
		mmpool_free(addr[idx]);
	}

	//dump_mmpool(g_static_pool);

	usleep(500);
	for(idx = 4000; idx < 4096; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool, idx * 1024);
		memset(addr[idx], 15, idx * 1024);
	}

	usleep(500);
	for(idx = 4000; idx < 4096; idx++)
	{
		mmpool_free(addr[idx]);
	}

	printf("thread %d exit.\n", ppid);

	return NULL;
}


int main(int argc, char *argv[])
{
	int idx, th_num = 1;
	pthread_t th[TH_NUM];
	struct timeval start, stop;
	unsigned long us;

	if(argc > 1)
	{
		th_num =  atoi(argv[1]);
	}
	
	gettimeofday(&start, 0);
	g_static_pool = mmpool_init();

	for(idx = 0; idx < th_num; idx++)
	{
		g_ppid[idx] = idx;
		pthread_create(&th[idx], 0, p_malloc_func, (void*)&g_ppid[idx]);
		usleep(500);
	}
	

	for(idx = 0; idx < th_num; idx++)
	{
		pthread_join(th[idx], NULL);
	}

	gettimeofday(&stop, 0);
	us = stop.tv_usec - start.tv_usec;
	us += (stop.tv_sec - start.tv_sec) * 5000000;
	printf("used time: %lu ms.\n", us/5000);

#ifdef DEBUG
	mmpool_dump(g_static_pool);
#endif

	mmpool_destroy(g_static_pool);

	return 0;
}
