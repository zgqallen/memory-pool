#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/mman.h>
#include "mmpool.h"

#define DEFAULT_PAGE_SIZE 4096 /* 4k */
#define DEFAULT_PAGE_COUNT 1024 /* 4MB*/

static unsigned int pgsize = DEFAULT_PAGE_SIZE;

typedef unsigned char BYTE;

#define ADDR_TO_MMBLOCK(addr) \
	(MM_BLOCK*)((BYTE*)(addr) - MM_BLOCK_HEAD_SIZE)

#define MMBLOCK_TO_ADDR(mmb) \
	(BYTE*)((BYTE*)(mmb) + MM_BLOCK_HEAD_SIZE)

#define MMBLOCK_SIZE(mmb) \
	((mmb)->size + MM_BLOCK_HEAD_SIZE)

#define POOL_FIRST_MMBLOCK(pool) \
	(MM_BLOCK*)((pool)->m_addr)

static inline int IS_ADDR_IN_POOL(const MM_POOL *pool, const void *addr)
{
	uintptr_t p_addr = (uintptr_t)pool->m_addr;
	uintptr_t u_addr = (uintptr_t)addr;
	return ( (u_addr >= p_addr) && (u_addr < p_addr + pool->size) );
}

MM_POOL *mmpool_init(void)
{
	MM_POOL *g_pool;
	MM_BLOCK *first_mmb;

# if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
	pgsize = sysconf (_SC_PAGESIZE);
# elif defined(HAVE_GETPAGESIZE)
	pgsize = getpagesize ();
# endif
	g_pool = (MM_POOL*)malloc(sizeof(MM_POOL));
	memset(g_pool, 0, sizeof(MM_POOL));

	g_pool->size = pgsize * DEFAULT_PAGE_COUNT;
	g_pool->m_addr = mmap (0, g_pool->size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if(g_pool->m_addr == MAP_FAILED)
	{
		printf("memory pool mmap failed, errno %d.\n", errno);
		free(g_pool);
		return NULL;
	}

	pthread_mutex_init(&g_pool->m_lock, NULL);
	pthread_mutex_init(&g_pool->g_lock, NULL);
	g_pool->free_block = 1;
	g_pool->free_size = g_pool->size;
	g_pool->next = NULL;

	first_mmb = POOL_FIRST_MMBLOCK(g_pool);
	first_mmb->size = g_pool->size - MM_BLOCK_HEAD_SIZE;
	first_mmb->flags = 0;
	first_mmb->pool = g_pool;
	first_mmb->prev = NULL;

	return g_pool;
}

void _mmpool_destroy(MM_POOL *pool, int all)
{
	if(all)
	{
		MM_POOL *cur_pool;
		for(cur_pool = pool; cur_pool; cur_pool = cur_pool->next)
		{
			munmap(cur_pool->m_addr, cur_pool->size);
		}
	}
	else
	{
		/* only release the current pool */
		munmap(pool->m_addr, pool->size);
	}
}

void mmpool_destroy(MM_POOL *pool)
{
        _mmpool_destroy(pool, 1); 
}

void mmpool_recycle(MM_POOL *g_pool)
{
	MM_POOL *last_pool, *cur_pool;
	
	MM_POOL_LOCK(g_pool);
	last_pool = g_pool;
	cur_pool = g_pool->next;

	while(cur_pool != NULL)
	{
		if(cur_pool->free_size == cur_pool->size)
		{
			MM_POOL *freep;
			freep = cur_pool;		

			last_pool->next = cur_pool->next;
			cur_pool = cur_pool->next;
			_mmpool_destroy(freep, 0);
			free(freep);
		}
		else
		{
			last_pool = cur_pool;
			cur_pool = cur_pool->next;
		}
	}
	MM_POOL_UNLOCK(g_pool);
}

static MM_BLOCK *pool_get_next_mmb(MM_POOL *pool, MM_BLOCK *mmb)
{
	MM_BLOCK *mmb_next;
	
	mmb_next = (MM_BLOCK*)(MMBLOCK_TO_ADDR(mmb) + mmb->size);
	if(IS_ADDR_IN_POOL(pool, mmb_next))
		return mmb_next;
	else
		return NULL;
}

/*
@@Deprecate - enhance to add a prev point in header to speed up merge prev.
static MM_BLOCK *pool_get_prev_mmb(MM_POOL *pool, MM_BLOCK *mmb)
{
	MM_BLOCK *cur_mmb, *mmb_next;

	cur_mmb = POOL_FIRST_MMBLOCK(pool);
	if(cur_mmb == mmb)
		return NULL;

	while(cur_mmb)
	{
		mmb_next = pool_get_next_mmb(pool, cur_mmb);
		if(mmb_next == mmb)
			break;
		else
			cur_mmb = mmb_next;	
	}

	return cur_mmb;
}
*/

static MM_BLOCK *pool_get_mmb(MM_POOL *pool, unsigned int size)
{
	MM_BLOCK *mmb;
	
	MM_POOL_LOCK(pool);
	mmb = POOL_FIRST_MMBLOCK(pool);

	while((mmb != NULL))
	{
		if(!(mmb->flags & MMB_IN_USE) && (mmb->size >= size))
		{
			/* got a free memory block in size */
			mmb->flags |= MMB_IN_USE;
			mmb->pool = pool;

			/* try to split the block if possiable */
			if((mmb->size - size) > 2 * MM_BLOCK_HEAD_SIZE)
			{
				/* try to split the block */
				MM_BLOCK *new_mmb, *mmb_next;
				mmb_next = pool_get_next_mmb(pool, mmb);

				new_mmb = (MM_BLOCK*)(MMBLOCK_TO_ADDR(mmb) + size);
				new_mmb->pool = pool;
				new_mmb->flags = 0;
				new_mmb->size = mmb->size - size - MM_BLOCK_HEAD_SIZE;
				new_mmb->prev = mmb;
				
				mmb->size = size;
				if(mmb_next) mmb_next->prev = new_mmb;
			}
			else
			{
				pool->free_block--;
			}

			pool->free_size -= MMBLOCK_SIZE(mmb);
			MM_POOL_UNLOCK(pool);
			return mmb;
		}	
		mmb = pool_get_next_mmb(pool, mmb);
	}

	MM_POOL_UNLOCK(pool);
	return NULL;
}

void *mmpool_malloc(MM_POOL *g_pool, unsigned int size)
{
	MM_BLOCK *mmb;
	MM_POOL  *cur_pool, *last_pool, *new_pool;

	if(size == 0)
	{
		return NULL;
	}

	/* memory block will always be multiple of MM_BLOCK_HEAD_SIZE */
	size = ((size + MM_BLOCK_HEAD_SIZE - 1) / MM_BLOCK_HEAD_SIZE) * MM_BLOCK_HEAD_SIZE;

	MM_POOL_LIST_LOCK(g_pool);
	/* find a befitting pool and allocate the memory */
	for(cur_pool = g_pool; cur_pool; cur_pool = cur_pool->next)
	{
		mmb = pool_get_mmb(cur_pool, size);
		if(mmb)
		{
			MM_POOL_LIST_UNLOCK(g_pool);
			return (void*)(&mmb->align_base);
		}

		last_pool = cur_pool;
	}
	MM_POOL_LIST_UNLOCK(g_pool);

	/* no available pool could alloc, new a pool to serve */
	new_pool = (MM_POOL*)malloc(sizeof(MM_POOL));
	memset(new_pool, 0, sizeof(MM_POOL));
	
	if(size > (pgsize * DEFAULT_PAGE_COUNT - MM_BLOCK_HEAD_SIZE))
	{
		new_pool->size = ((size + pgsize + MM_BLOCK_HEAD_SIZE) / pgsize) * pgsize;
	}
	else
	{
		new_pool->size = pgsize * DEFAULT_PAGE_COUNT;
	}

	new_pool->m_addr = mmap (0, new_pool->size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(new_pool->m_addr == MAP_FAILED)
	{
		printf("memory pool mmap failed, errno %d.\n", errno);
		free(new_pool);
		return NULL;
	}

	pthread_mutex_init(&new_pool->m_lock, NULL);
	new_pool->free_block = 1;
	new_pool->free_size = new_pool->size;
	new_pool->next = NULL;

        mmb = POOL_FIRST_MMBLOCK(new_pool);
        mmb->size = new_pool->size - MM_BLOCK_HEAD_SIZE;
        mmb->flags = 0;
        mmb->pool = new_pool;
	mmb->prev = NULL;

	/* allocate memory from new pool */
	mmb = pool_get_mmb(new_pool, size);

        MM_POOL_LIST_LOCK(g_pool);
	/* link to pool list */
        last_pool->next = new_pool;
        MM_POOL_LIST_UNLOCK(g_pool);

	return (void*)(&mmb->align_base);
}

void pool_merge(MM_POOL *pool, MM_BLOCK *mmb)
{
	MM_BLOCK *mmb_prev, *mmb_next;

	mmb_prev = mmb->prev;
	/* merge with prev block*/
        if(mmb_prev && !(mmb_prev->flags & MMB_IN_USE))
        {   
                MM_BLOCK *cur_mmb_next;

                cur_mmb_next = pool_get_next_mmb(pool, mmb);
                if(cur_mmb_next)
                {   
			cur_mmb_next->prev = mmb_prev;    
		}

                mmb_prev->size += MMBLOCK_SIZE(mmb);
                memset(mmb, 0, MM_BLOCK_HEAD_SIZE);
                pool->free_block--;

		mmb = mmb_prev;
        }

	mmb_next = pool_get_next_mmb(pool, mmb);
	/* merge with next block */
	if(mmb_next && !(mmb_next->flags & MMB_IN_USE))
	{
		MM_BLOCK *mmb_nnext;

		mmb_nnext = pool_get_next_mmb(pool, mmb_next);
		if(mmb_nnext)
		{
			mmb_nnext->prev = mmb;
		}

		mmb->size += MMBLOCK_SIZE(mmb_next);
		memset(mmb_next, 0, MM_BLOCK_HEAD_SIZE);
                pool->free_block--;
	}
}

void mmpool_free(void *addr)
{
	MM_BLOCK *mmb;
	MM_POOL  *cur_pool;

	if(addr == NULL)
		return;

	mmb = ADDR_TO_MMBLOCK(addr);
	if(!(mmb->flags & MMB_IN_USE))
	{
		printf("***** Address [%p] has already been freed, double free.*****\n", addr);
		return;
	}

	cur_pool = (MM_POOL*)mmb->pool;

	MM_POOL_LOCK(cur_pool);
	mmb->flags &= ~MMB_IN_USE;
	cur_pool->free_size += MMBLOCK_SIZE(mmb);
	cur_pool->free_block++;

	/* try the merge the memory block */
	pool_merge(cur_pool, mmb);
	MM_POOL_UNLOCK(cur_pool);
}

void _mmpool_dump(MM_POOL *pool, int all)
{
	MM_POOL *cur_pool;

	printf("---------------------------------- START DUMP MMPOOL ----------------------------------\n");
	for(cur_pool = pool; cur_pool; cur_pool = cur_pool->next)
	{
		MM_BLOCK *mmb;	
		MM_POOL_LOCK(cur_pool);
		printf("*********************************** START THIS POOL **************************************\n");
		printf("   POOL OVER ALL: start addr [%p] size [%d] freesize [%d] free_block [%d] \n",
			cur_pool->m_addr, cur_pool->size, cur_pool->free_size, cur_pool->free_block);

		mmb = POOL_FIRST_MMBLOCK(cur_pool);
		while(mmb != NULL)
		{
			printf("[%p]: prev block [%p] block size [%lu] flags [%d]\n", mmb, mmb->prev, MMBLOCK_SIZE(mmb), mmb->flags);
			mmb = pool_get_next_mmb(cur_pool, mmb);
		}

		printf("*********************************** END THIS POOL ****************************************\n");
		MM_POOL_UNLOCK(cur_pool);

		if(!all)
			break;
	}
	printf("------------------------------ END DUMP MMPOOL ----------------------------------------\n");
}

void mmpool_dump(MM_POOL *g_pool)
{
        _mmpool_dump(g_pool, 1);
}
