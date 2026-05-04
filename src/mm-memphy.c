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
    /* This setting come with fixed constant PAGESZ */
    int numfp = mp->maxsz / pagesz;
    struct framephy_struct *newfst, *fst;
    int iter = 0;

    if (numfp <= 0)
        return -1;

    /* Init head of free framephy list */
    fst = malloc(sizeof(struct framephy_struct));
    fst->fpn = iter;
    mp->free_fp_list = fst;

    /* We have list with first element, fill in the rest num-1 element member*/
    for (iter = 1; iter < numfp; iter++)
    {
        newfst = malloc(sizeof(struct framephy_struct));
        newfst->fpn = iter;
        newfst->fp_next = NULL;
        fst->fp_next = newfst;
        fst = newfst;
    }

    return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
    if (mp == NULL || retfpn == NULL)
        return -1;

    pthread_mutex_lock(&memphy_lock);

    struct framephy_struct *fp = mp->free_fp_list;

    if (fp == NULL) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }

    *retfpn = fp->fpn;
    mp->free_fp_list = fp->fp_next;

    /* MEMPHY is iteratively used up until its exhausted
     * No garbage collector acting then it not been released
     */
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
    if (mp == NULL)
        return -1;

    pthread_mutex_lock(&memphy_lock);

    struct framephy_struct *fp = mp->free_fp_list;
    struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));

    /* Create new node with value fpn */
    newnode->fpn = fpn;
    newnode->fp_next = fp;
    mp->free_fp_list = newnode;

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

    pthread_mutex_lock(&memphy_lock);

    int fpnum = mp->maxsz / PAGING64_PAGESZ;

    if (fpnum <= 0) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }

    BYTE *freefp_table = (BYTE *)calloc(fpnum, sizeof(BYTE));

    struct framephy_struct *head = mp->free_fp_list;

    while (head)
    {
        freefp_table[head->fpn] = 1;
        head = head->fp_next;
    }

    int start_fpn = -1;
    int pgnum = req_pgnum;
    int iter;
    for (iter = 0; iter < fpnum; iter++)
    {
        if (freefp_table[iter] == 1)
        {
            if (pgnum == req_pgnum)
            {
                start_fpn = iter;
            }
            pgnum--;
            if (pgnum == 0)
                break;
        }
        else
        {
            pgnum = req_pgnum;
            start_fpn = -1;
        }
    }

    free(freefp_table);

    if (pgnum > 0) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }

    struct framephy_struct **sorted_nodes = (struct framephy_struct **)malloc(sizeof(struct framephy_struct *) * req_pgnum);
    struct framephy_struct *prev = NULL;
    struct framephy_struct *free_ptr = mp->free_fp_list;

    while (free_ptr)
    {
        if (free_ptr->fpn >= start_fpn && free_ptr->fpn < start_fpn + req_pgnum)
        {
            if (prev)
            {
                prev->fp_next = free_ptr->fp_next;
            }
            else
            {
                mp->free_fp_list = free_ptr->fp_next;
            }

            int offset = free_ptr->fpn - start_fpn;
            sorted_nodes[offset] = free_ptr;

            free_ptr = free_ptr->fp_next;
        }
        else
        {
            prev = free_ptr;
            free_ptr = free_ptr->fp_next;
        }
    }

    for (int i = 0; i < req_pgnum - 1; i++)
    {
        sorted_nodes[i]->fp_next = sorted_nodes[i + 1];
    }
    sorted_nodes[req_pgnum - 1]->fp_next = NULL;

    *ret_frm_list = sorted_nodes[0];

    free(sorted_nodes);

    pthread_mutex_unlock(&memphy_lock);

    return 0;
}

// #endif
