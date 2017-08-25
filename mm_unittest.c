#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "mmpool.h"

#define TH_NUM 200
//#define M_ARENA 4
MM_POOL *g_static_pool[4];
int g_ppid[TH_NUM] = {0};

int pidtoidx(int pid)
{
#ifdef M_ARENA
	if(pid < 12)
		return 0;
	else if(pid < 24)
		return 1;
	else if(pid < 36)
		return 2;
	else
		return 3;
#else
	return 0;
#endif
}


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
	int idx, size;
	unsigned char *addr[IDX];

	printf("thread %d starting...\n", ppid);
	srand((unsigned)time(NULL)+ppid);

	/* allocte random size in loop 10000 */
	for(idx = S_IDX; idx < IDX; idx++)
	{
		size = rand() % IDX;
		addr[idx] = mmpool_malloc(g_static_pool[pidtoidx(ppid)], size);
		memset(addr[idx], 0, size);
	}
	usleep(500);

	for(idx = S_IDX; idx < IDX; idx++)
	{
		mmpool_free(addr[idx]);
	}
	usleep(500);

	/* allocte fixed size in loop 10000 */
	for(idx = S_IDX; idx < IDX; idx++)
	{
		addr[idx] = mmpool_malloc(g_static_pool[pidtoidx(ppid)], 128);
		memset(addr[idx], 0, 128);
	}
	usleep(500);

	for(idx = S_IDX; idx < IDX; idx++)
        {
                mmpool_free(addr[idx]);
        }
        usleep(500);

        /* allocate large object in loop 100 */
        for(idx = 2000; idx < 2100; idx++)
        {
                addr[idx] = mmpool_malloc(g_static_pool[pidtoidx(ppid)], idx * 1024);
                memset(addr[idx], 0, idx * 1024);
        }
        usleep(500);

        for(idx = 2000; idx < 2100; idx++)
        {
                mmpool_free(addr[idx]);
        }
	usleep(500);

        /* allocte random size in loop 10000 again */
        for(idx = S_IDX; idx < IDX; idx++)
        {   
                size = rand() % IDX;
                addr[idx] = mmpool_malloc(g_static_pool[pidtoidx(ppid)], size);
                memset(addr[idx], 0, size);
        }
        usleep(500);

	for(idx = IDX-1; idx >= S_IDX; idx--)
	{
		mmpool_free(addr[idx]);
	}
	usleep(500);

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
	g_static_pool[0] = mmpool_init();
#ifdef M_ARENA
	g_static_pool[1] = mmpool_init();
	g_static_pool[2] = mmpool_init();
	g_static_pool[3] = mmpool_init();
#endif

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

#ifndef GLIBC
	//mmpool_dump(g_static_pool);
	mmpool_dump_counter(g_static_pool[0]);
#ifdef M_ARENA
	mmpool_dump_counter(g_static_pool[1]);
	mmpool_dump_counter(g_static_pool[2]);
	mmpool_dump_counter(g_static_pool[3]);
#endif
#else
	malloc_stats();
#endif

	mmpool_destroy(g_static_pool[0]);
#ifdef M_ARENA
	mmpool_destroy(g_static_pool[1]);
	mmpool_destroy(g_static_pool[2]);
	mmpool_destroy(g_static_pool[3]);
#endif

	return 0;
}
