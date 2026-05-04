/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include "mm64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t memphy_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
    int numstep = 0;

    mp->cursor = 0;
    while (numstep < offset && numstep < mp->maxsz)
    {
        /* Traverse sequentially */
        mp->cursor = (mp->cursor + 1) % mp->maxsz;
        numstep++;
    }

    return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (mp == NULL)
        return -1;

    if (mp->rdmflg)
        return -1; /* Not compatible mode for sequential read */

    MEMPHY_mv_csr(mp, addr);
    *value = (BYTE)mp->storage[addr];

    return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (mp == NULL)
        return -1;
    int res = 0;

    pthread_mutex_lock(&memphy_lock);

    if (mp->rdmflg)
        *value = mp->storage[addr];
    else /* Sequential access device */
        res = MEMPHY_seq_read(mp, addr, value);

    pthread_mutex_unlock(&memphy_lock);

    return res;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{

    if (mp == NULL)
        return -1;

    if (mp->rdmflg)
        return -1; /* Not compatible mode for sequential read */

    MEMPHY_mv_csr(mp, addr);
    mp->storage[addr] = value;

    return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
    if (mp == NULL)
        return -1;
    int res = 0;

    pthread_mutex_lock(&memphy_lock);

    if (mp->rdmflg)
        mp->storage[addr] = data;
    else /* Sequential access device */
        res = MEMPHY_seq_write(mp, addr, data);

    pthread_mutex_unlock(&memphy_lock);

    return res;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
    int numfp = mp->maxsz / pagesz;

    if (numfp <= 0)
        return -1;

    /* Reserve 25% of physical frames for Kernel (Buddy Allocator) */
    int kernel_fp_limit = numfp / 4;

    /* 1. Initialize Buddy Allocator for Kernel */
    mp->buddy_map = malloc(kernel_fp_limit * sizeof(int8_t));
    memset(mp->buddy_map, -1, kernel_fp_limit * sizeof(int8_t)); /* -1 means allocated/unavailable */

    for (int i = 0; i < MAX_BUDDY_ORDER; i++)
        mp->free_buddy_list[i] = NULL;

    /* Initialize by breaking the total kernel memory into maximum possible buddy blocks */
    int current_fpn = 0;
    int remaining = kernel_fp_limit;

    while (remaining > 0)
    {
        int order = 0;
        while ((1 << (order + 1)) <= remaining && order < MAX_BUDDY_ORDER - 1)
        {
            order++;
        }

        struct framephy_struct *new_block = malloc(sizeof(struct framephy_struct));
        new_block->fpn = current_fpn;
        new_block->fp_next = mp->free_buddy_list[order];
        mp->free_buddy_list[order] = new_block;
        mp->buddy_map[current_fpn] = order;

        current_fpn += (1 << order);
        remaining -= (1 << order);
    }

    /* 2. Initialize Free List Allocator for User */
    mp->free_fp_list = NULL;
    for (int i = kernel_fp_limit; i < numfp; i++)
    {
        struct framephy_struct *new_fp = malloc(sizeof(struct framephy_struct));
        new_fp->fpn = i;
        new_fp->fp_next = mp->free_fp_list;
        mp->free_fp_list = new_fp;
    }

    return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
    pthread_mutex_lock(&memphy_lock);

    if (mp->free_fp_list == NULL)
    {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }

    struct framephy_struct *fp = mp->free_fp_list;
    *retfpn = fp->fpn;
    mp->free_fp_list = fp->fp_next;
    free(fp);

    pthread_mutex_unlock(&memphy_lock);

    return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
    /*TODO dump memphy contnt mp->storage
     *     for tracing the memory content
     */
    if (mp == NULL || mp->storage == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&memphy_lock);

    printf("===== PHYSICAL MEMORY DUMP =====\n");
    for (int i = 0; i < mp->maxsz; i++)
    {
        if (mp->storage[i])
        {
            printf("BYTE %016lx: %d\n", (long unsigned int)i, mp->storage[i]);
        }
    }

    pthread_mutex_unlock(&memphy_lock);

    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
    int max_fpn = mp->maxsz / PAGING64_PAGESZ;
    int kernel_fp_limit = max_fpn / 4;

    pthread_mutex_lock(&memphy_lock);

    if (fpn >= kernel_fp_limit)
    {
        /* Free List Allocator for user frames */
        struct framephy_struct *new_fp = malloc(sizeof(struct framephy_struct));
        new_fp->fpn = fpn;
        new_fp->fp_next = mp->free_fp_list;
        mp->free_fp_list = new_fp;
        pthread_mutex_unlock(&memphy_lock);
        return 0;
    }

    /* If this is a tail page of an allocated buddy block, ignore it.
     * The whole block is freed when the head page is processed. */
    if (mp->buddy_map[fpn] == -128)
    {
        pthread_mutex_unlock(&memphy_lock);
        return 0;
    }

    int order = 0;
    if (mp->buddy_map[fpn] < -1)
    {
        /* Recover the allocated order from the head page */
        order = (-mp->buddy_map[fpn]) - 1;
    }

    /* Recursively coalesce with buddies if they are also completely free */
    while (order < MAX_BUDDY_ORDER - 1)
    {
        addr_t buddy_fpn = fpn ^ (1 << order); /* The bitwise XOR buddy math trick */

        if (buddy_fpn >= kernel_fp_limit)
            break; /* Buddy goes out of kernel physical memory bounds */

        if (mp->buddy_map[buddy_fpn] == order)
        {
            /* Buddy is completely free! Detach buddy from the list */
            struct framephy_struct **curr = &mp->free_buddy_list[order];
            while (*curr)
            {
                if ((*curr)->fpn == buddy_fpn)
                {
                    struct framephy_struct *temp = *curr;
                    *curr = (*curr)->fp_next;
                    free(temp);
                    break;
                }
                curr = &((*curr)->fp_next);
            }
            mp->buddy_map[buddy_fpn] = -1;             /* Remove buddy identity */
            fpn = (fpn < buddy_fpn) ? fpn : buddy_fpn; /* The coalesced block starts at the lower FPN */
            order++;
        }
        else
        {
            break; /* Buddy is allocated or part of a larger block */
        }
    }

    /* Attach the maximally coalesced block back to the appropriate free list */
    struct framephy_struct *new_block = malloc(sizeof(struct framephy_struct));
    new_block->fpn = fpn;
    new_block->fp_next = mp->free_buddy_list[order];
    mp->free_buddy_list[order] = new_block;
    mp->buddy_map[fpn] = order;

    pthread_mutex_unlock(&memphy_lock);

    return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
    mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
    mp->maxsz = max_size;
    memset(mp->storage, 0, max_size * sizeof(BYTE));

    MEMPHY_format(mp, PAGING64_PAGESZ);

    mp->rdmflg = (randomflg != 0) ? 1 : 0;

    if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
        mp->cursor = 0;

    return 0;
}
/*
 * MEMPHY_get_contiguous_freefp: find and get a continuous frames inside the physical memory (kernel use)
   @mp: memory device
   @req_pgnum: # of pages required
   @ret_frm_list: list of continuous frames

*/
int MEMPHY_get_contiguous_freefp(struct memphy_struct *mp, int req_pgnum, struct framephy_struct **ret_frm_list)
{
    if (mp == NULL || req_pgnum <= 0)
        return -1;

    int req_order = 0;
    while ((1 << req_order) < req_pgnum)
        req_order++;
    if (req_order >= MAX_BUDDY_ORDER)
        return -1;

    pthread_mutex_lock(&memphy_lock);

    /* Find smallest available buddy block >= req_order */
    int alloc_order = req_order;
    while (alloc_order < MAX_BUDDY_ORDER && mp->free_buddy_list[alloc_order] == NULL)
    {
        alloc_order++;
    }
    if (alloc_order == MAX_BUDDY_ORDER)
    {
        /* Out of physical memory for buddy contiguous frames */
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }

    /* Remove block from the found order's free list */
    struct framephy_struct *block = mp->free_buddy_list[alloc_order];
    mp->free_buddy_list[alloc_order] = block->fp_next;
    addr_t fpn = block->fpn;
    free(block);

    /* Split the block down to the required order */
    while (alloc_order > req_order)
    {
        alloc_order--;
        addr_t buddy_fpn = fpn + (1 << alloc_order);

        struct framephy_struct *buddy_block = malloc(sizeof(struct framephy_struct));
        buddy_block->fpn = buddy_fpn;
        buddy_block->fp_next = mp->free_buddy_list[alloc_order];
        mp->free_buddy_list[alloc_order] = buddy_block;
        mp->buddy_map[buddy_fpn] = alloc_order;
    }
    
    /* Mark the block as allocated with internal fragmentation tracking.
     * Store the allocated order in the head page to know the size when freeing.
     * Use -128 for tail pages so they are ignored if the OS tries to free them individually. */
    mp->buddy_map[fpn] = -(req_order + 1);
    for (int i = 1; i < (1 << req_order); i++) {
        mp->buddy_map[fpn + i] = -128;
    }

    /* Expand the contiguous block into a linked list for the OS page mapping functions */
    struct framephy_struct *head = NULL, *tail = NULL;
    for (int i = 0; i < req_pgnum; i++)
    {
        struct framephy_struct *node = malloc(sizeof(struct framephy_struct));
        node->fpn = fpn + i;
        node->fp_next = NULL;
        if (!head)
            head = node;
        else
            tail->fp_next = node;
        tail = node;
    }
    *ret_frm_list = head;
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

// #endif
