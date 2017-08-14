#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/mman.h>
#include "mmpool.h"

#define DEFAULT_PAGE_SIZE 4096 /* 4k */
#define DEFAULT_PAGE_COUNT 16384 /* 64MB*/

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

/*
** The free blocks (list) index mapping with (size/MM_BLOCK_HEAD_SIZE), but for the
** last bucket which used to stats the size > (1024 * MM_BLOCK_HEAD_SIZE) the large blocks.
*/
#define SIZE_TO_INDEX(size) \
	(((size)/MM_BLOCK_HEAD_SIZE) <= FREEMMB_BUCKET_SIZE? \
	(((size)/MM_BLOCK_HEAD_SIZE)-1) : (FREEMMB_BUCKET_SIZE-1))

#define INC_POOL_FREEBLOCKS(pool, mmb) ((pool)->free_blocks[SIZE_TO_INDEX((mmb)->size)]++);	
#define DEC_POOL_FREEBLOCKS(pool, mmb) ((pool)->free_blocks[SIZE_TO_INDEX((mmb)->size)]--)

static int IS_ADDR_IN_POOL(const MM_POOL *pool, const void *addr)
{
	uintptr_t p_addr = (uintptr_t)pool->m_addr;
	uintptr_t u_addr = (uintptr_t)addr;
	return ( (u_addr >= p_addr) && (u_addr < p_addr + pool->size) );
}

void mmpool_ins_freelist(MM_POOL *pool, MM_BLOCK *mmb, MMB_LLE *mmb_lle)
{
	int index;

	if(mmb_lle == NULL)
	{
		/* new list item */
		mmb_lle = (MMB_LLE*)malloc(sizeof(MMB_LLE));
		memset(mmb_lle, 0, sizeof(MMB_LLE));
	}

	index = SIZE_TO_INDEX(mmb->size);

	if(pool->free_blocks_list[index] == NULL)
	{
		/* first item */
		mmb_lle->prev = NULL;
		mmb_lle->next = NULL;
		mmb_lle->mmb = mmb;
		
		/* only record tail for head item. */
		mmb_lle->tail = mmb_lle; 
		pool->free_blocks_list[index] = mmb_lle;
	}
	else
	{
		MMB_LLE *p;
		p = pool->free_blocks_list[index]->tail;

		p->next = mmb_lle;
		mmb_lle->prev = p;
		mmb_lle->next = NULL;
		mmb_lle->mmb = mmb;

		pool->free_blocks_list[index]->tail = mmb_lle;
	}
}

MMB_LLE *mmpool_del_freelist(MM_POOL *pool, MM_BLOCK *mmb)
{
	int index;
	index = SIZE_TO_INDEX(mmb->size);

	if(pool->free_blocks_list[index] != NULL)
	{
		MMB_LLE *mmb_lle;
		mmb_lle = pool->free_blocks_list[index];
		
		/* in case we are the head item */
		if(mmb_lle->mmb == mmb)
		{
			pool->free_blocks_list[index] = mmb_lle->next;
			if(pool->free_blocks_list[index])
			{
				pool->free_blocks_list[index]->tail = mmb_lle->tail;
			}
			return mmb_lle;
		}
		else if(mmb_lle->tail->mmb == mmb)
		{
			/* in case we are the tail item*/
			MMB_LLE *tail;

			tail = mmb_lle->tail;
			pool->free_blocks_list[index]->tail = tail->prev;
			tail->prev->next = NULL;
			return tail;
		}
		else
		{
			MMB_LLE *p;
			for(p = mmb_lle; p; p = p->next)
			{
				if(p->mmb == mmb)
					break;
			}

			if(p != NULL)
			{
				p->prev->next = p->next;
				p->next->prev = p->prev;

				return p;
			}
		}
	}

	return NULL;
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
	g_pool->free_size = g_pool->size;
	g_pool->main_pool = g_pool;

	first_mmb = POOL_FIRST_MMBLOCK(g_pool);
	first_mmb->size = g_pool->size - MM_BLOCK_HEAD_SIZE;
	first_mmb->flags = 0;
	first_mmb->pool = g_pool;
	first_mmb->prev = NULL;

	INC_POOL_FREEBLOCKS(g_pool, first_mmb);
	mmpool_ins_freelist(g_pool, first_mmb, NULL);

	/* for main pool only */
	g_pool->meta = (POOL_META*)malloc(sizeof(POOL_META));
	memset(g_pool->meta, 0, sizeof(POOL_META));
	pthread_mutex_init(&g_pool->meta->g_lock, NULL);
	
	g_pool->idx = 0;
	g_pool->meta->pool_len = 1;
	g_pool->meta->pool_array[g_pool->idx] = g_pool;
	g_pool->meta->pool_weight[g_pool->idx]++;

	return g_pool;
}

void _mmpool_destroy(MM_POOL *pool, int all)
{
	if(all)
	{
		/* the input pool must be main pool*/
		int idx;
		POOL_META *meta = pool->meta;

		for(idx = 0; idx < meta->pool_len; idx++)
		{
			munmap(meta->pool_array[idx]->m_addr, meta->pool_array[idx]->size);
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

static MM_BLOCK *pool_get_next_mmb(MM_POOL *pool, MM_BLOCK *mmb)
{
	MM_BLOCK *mmb_next;
	
	mmb_next = (MM_BLOCK*)(MMBLOCK_TO_ADDR(mmb) + mmb->size);
	if(IS_ADDR_IN_POOL(pool, mmb_next))
		return mmb_next;
	else
		return NULL;
}

static MM_BLOCK *pool_get_mmb(MM_POOL *pool, unsigned int size)
{
	MMB_LLE *mmb_lle;
	MM_BLOCK *mmb;
	int i, match_index = -1;
	
	MM_POOL_LOCK(pool);

	/* find a best match index */
	i = SIZE_TO_INDEX(size);
	for(;i < FREEMMB_BUCKET_SIZE; i++)
	{
		if(pool->free_blocks[i] > 0)
		{
			match_index = i;
			break;
		}
	}

	if(match_index == -1)
	{
		/* no free match size block */
		MM_POOL_UNLOCK(pool);
		return NULL;
	}

	mmb_lle = pool->free_blocks_list[match_index]; 

	while((mmb_lle != NULL))
	{
		mmb = mmb_lle->mmb;

		if(!(mmb->flags & MMB_IN_USE) && (mmb->size >= size))
		{
			MMB_LLE *p;

			/* got a free memory block in size */
			mmb->flags |= MMB_IN_USE;
			mmb->pool = pool;

			/* delete the mmb from the freeblocks list */
			p = mmpool_del_freelist(pool, mmb);
			DEC_POOL_FREEBLOCKS(pool, mmb);
			ATOMIC_DEC(&pool->main_pool->meta->pool_weight[pool->idx]);
			
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
				if(mmb_next) mmb_next->prev = new_mmb;

				/* insert new mmb to freeblocks list */
				INC_POOL_FREEBLOCKS(pool, new_mmb);
				ATOMIC_INC(&pool->main_pool->meta->pool_weight[pool->idx]);
				mmpool_ins_freelist(pool, new_mmb, NULL);

				/* update the new mmb size */				
				mmb->size = size;
			}
			else
			{
				free(p);
			}

			pool->free_size -= MMBLOCK_SIZE(mmb);
			MM_POOL_UNLOCK(pool);
			return mmb;
		}	
		mmb_lle = mmb_lle->next; 
	}

	MM_POOL_UNLOCK(pool);
	return NULL;
}

int pool_pick_one(MM_POOL *g_pool)
{
	POOL_META *meta = g_pool->meta;
	int proportion[MAX_POOL_NUM] = {0};
	int rnd, idx, pro_sum = 0;

	/*
	** Per valgrind callgrind tuning, we found the time cost on going
	** through pool list was high. So we implement a weighted random 
	** select to speed up the memory allocation pool hit, the weight
	** is the freeblocks number of the pool.
	** For example the werght as below:
	** [1, 0, 3, 5, 0, 6] for pool idx [0-> 5], then probability will
	** be [6%, 0%, 20%, 33%, 0, 40%] to return the index.
	*/
	for(idx = 0; idx < meta->pool_len; idx++)
	{
		pro_sum += meta->pool_weight[idx];
		if(meta->pool_weight == 0)
			proportion[idx] = 0;
		else
			proportion[idx] = pro_sum;
	}

	if(pro_sum > 0)
	{
		srand((unsigned)time(NULL));
		rnd = rand()%pro_sum;
		for(idx = 0; idx < meta->pool_len; idx++)
		{
			if(proportion[idx] != 0 && rnd <= proportion[idx])
				return idx;
		}
	}

	return meta->pool_len/2;
}

void *mmpool_malloc(MM_POOL *g_pool, unsigned int size)
{
	MM_BLOCK *mmb;
	MM_POOL  *cur_pool, *last_pool, *new_pool;
	POOL_META *meta;
	int idx;

	if(size == 0)
	{
		return NULL;
	}

	/* memory block will always be multiple of MM_BLOCK_HEAD_SIZE */
	size = ((size + MM_BLOCK_HEAD_SIZE - 1) / MM_BLOCK_HEAD_SIZE) * MM_BLOCK_HEAD_SIZE;

	MM_POOL_LIST_LOCK(g_pool);
	/* find a befitting pool and allocate the memory */
	idx = pool_pick_one(g_pool);
	meta = g_pool->meta;

	for(; idx < meta->pool_len; idx++)
	{
		if(size > meta->pool_array[idx]->free_size)
		{
			continue;
		}

		mmb = pool_get_mmb(meta->pool_array[idx], size);
		if(mmb)
		{
			MM_POOL_LIST_UNLOCK(g_pool);
			return (void*)(&mmb->align_base);
		}
	}

	for(--idx; idx >= 0; idx--)
	{
                if(size > meta->pool_array[idx]->free_size)
                {   
                        continue;
                }   

                mmb = pool_get_mmb(meta->pool_array[idx], size);
                if(mmb)
                {   
                        MM_POOL_LIST_UNLOCK(g_pool);
                        return (void*)(&mmb->align_base);
                } 	
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
	new_pool->free_size = new_pool->size;
	new_pool->main_pool = g_pool;

        mmb = POOL_FIRST_MMBLOCK(new_pool);
        mmb->size = new_pool->size - MM_BLOCK_HEAD_SIZE;
        mmb->flags = 0;
        mmb->pool = new_pool;
	mmb->prev = NULL;

	mmpool_ins_freelist(new_pool, mmb, NULL);
	INC_POOL_FREEBLOCKS(new_pool, mmb);

	/* allocate memory from new pool */
	mmb = pool_get_mmb(new_pool, size);

        MM_POOL_LIST_LOCK(g_pool);
	/* add to main pool array */
	new_pool->idx = meta->pool_len;
	meta->pool_len++;
	meta->pool_array[new_pool->idx] = new_pool;
	meta->pool_weight[new_pool->idx]++;        
        MM_POOL_LIST_UNLOCK(g_pool);

	return (void*)(&mmb->align_base);
}

void pool_merge(MM_POOL *pool, MM_BLOCK *mmb)
{
	MM_BLOCK *mmb_prev, *mmb_next;
	MMB_LLE *mmb_lle_prev = NULL, *mmb_lle_next =  NULL;

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

		/* delete prev mmb from freeblock list with old size*/
		mmb_lle_prev = mmpool_del_freelist(pool, mmb_prev);
		DEC_POOL_FREEBLOCKS(pool, mmb_prev);
		ATOMIC_DEC(&pool->main_pool->meta->pool_weight[pool->idx]);

		/* update the prev size and update mmb to new prev */
                mmb_prev->size += MMBLOCK_SIZE(mmb);
		memset(mmb, 0, MM_BLOCK_HEAD_SIZE);
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

		/* delete next mmb from freeblock list with old size */
		mmb_lle_next = mmpool_del_freelist(pool, mmb_next);
		DEC_POOL_FREEBLOCKS(pool, mmb_next);
		ATOMIC_DEC(&pool->main_pool->meta->pool_weight[pool->idx]);

		/* update the mmb to new size merged with next */
		mmb->size += MMBLOCK_SIZE(mmb_next);
		memset(mmb_next, 0, MM_BLOCK_HEAD_SIZE);
	}

	if(mmb_lle_prev != NULL)
	{
		mmpool_ins_freelist(pool, mmb, mmb_lle_prev);
		INC_POOL_FREEBLOCKS(pool, mmb);
		ATOMIC_INC(&pool->main_pool->meta->pool_weight[pool->idx]);

		if(mmb_lle_next != NULL)
			free(mmb_lle_next);
	}
	else if(mmb_lle_next != NULL)
	{
		mmpool_ins_freelist(pool, mmb, mmb_lle_next);
		INC_POOL_FREEBLOCKS(pool, mmb);
		ATOMIC_INC(&pool->main_pool->meta->pool_weight[pool->idx]);
	}
	else
	{
		/* merge nothing, insert as a new item */
		mmpool_ins_freelist(pool, mmb, NULL);
		INC_POOL_FREEBLOCKS(pool, mmb);
		ATOMIC_INC(&pool->main_pool->meta->pool_weight[pool->idx]);
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

	/* try the merge the memory block and update the free blocks */
	pool_merge(cur_pool, mmb);
	MM_POOL_UNLOCK(cur_pool);
}

void _mmpool_dump(MM_POOL *pool, int all)
{
	POOL_META *meta;
	MM_POOL *cur_pool;
	int idx = 0;

	printf("---------------------------------- START DUMP MMPOOL ----------------------------------\n");
	cur_pool = pool;
	do
	{
		MM_BLOCK *mmb;
		int i;

		MM_POOL_LOCK(cur_pool);
		printf("*********************************** START THIS POOL **************************************\n");
		printf("   POOL OVER ALL: start addr [%p] size [%d] freesize [%d] freeblocks: \n",
			cur_pool->m_addr, cur_pool->size, cur_pool->free_size);

		for(i = 0; i < FREEMMB_BUCKET_SIZE; i++)
		{
			if(cur_pool->free_blocks[i] > 0)
				printf("[%d]:%lu ", (i+1)*32, cur_pool->free_blocks[i]);
		}
		printf("\n");

		mmb = POOL_FIRST_MMBLOCK(cur_pool);
		while(mmb != NULL)
		{
			printf("[%p]: prev block [%p] block size [%lu] flags [%d]\n", mmb, mmb->prev, MMBLOCK_SIZE(mmb), mmb->flags);
			mmb = pool_get_next_mmb(cur_pool, mmb);
		}

		printf("*********************************** END THIS POOL ****************************************\n");
		MM_POOL_UNLOCK(cur_pool);

		if(!all)
		{
			break;
		}
		else
		{
			meta = pool->meta;
			cur_pool = meta->pool_array[++idx];
			if(idx >= meta->pool_len)
				break;
		}
	}while(1);
	printf("------------------------------ END DUMP MMPOOL ----------------------------------------\n");
}

void mmpool_dump(MM_POOL *g_pool)
{
        _mmpool_dump(g_pool, 1);
}
