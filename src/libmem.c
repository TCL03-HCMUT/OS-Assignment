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
 * System Library
 * Memory Module Library libmem.c
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 * (usr and krnl)
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
    struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

    if (rg_elmt->rg_start >= rg_elmt->rg_end)
        return -1;

    if (rg_node != NULL)
        rg_elmt->rg_next = rg_node;

    /* Enlist the new region */
    mm->mmap->vm_freerg_list = rg_elmt;

    return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 * (usr and krnl)
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
        return NULL;

    return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 * (usr),  converted from caller->krnl->mm to caller->mm
 *          _syscall(krnl_t,...)
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
    /*Allocate at the toproof */
    pthread_mutex_lock(&mmvm_lock);

    /* Find an empty symbol region in the table instead of using the passed rgid */
    rgid = -1;
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
        if (caller->mm->symrgtbl[i].rg_start == 0 && caller->mm->symrgtbl[i].rg_end == 0)
        {
            rgid = i;
            break;
        }
    }
    if (rgid == -1)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    struct vm_rg_struct rgnode;
    rgnode.mode_bit = 1;
    if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
    {
        caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
        caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
        caller->mm->symrgtbl[rgid].mode_bit = rgnode.mode_bit;
        caller->mm->symrgtbl[rgid].vmaid = rgnode.vmaid;
        *alloc_addr = rgnode.rg_start;

        pthread_mutex_unlock(&mmvm_lock);
        return 0;
    }
    
    /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

    /*Attempt to increate limit to get space */
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
    if (cur_vma == NULL) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1; 
    }
    int old_sbrk = cur_vma->sbrk;
    int inc_sz = 0;

#ifdef MM64
    inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ);
    inc_sz = inc_sz + 1;
#else
    inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

    /* TODO INCREASE THE LIMIT
     * SYSCALL 1 sys_memmap
     */
    arg_t a1 = SYSMEM_INC_OP;
    arg_t a2 = vmaid;
    arg_t a3 = size;
    // struct sc_regs regs;
    // regs.a1 = SYSMEM_INC_OP;
    // regs.a2 = vmaid;
#ifdef MM64
    // regs.a3 = size;
#else
    // regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif

    libsyscall(caller, 17, a1, a2, a3); /* SYSCALL 17 sys_memmap */

    if (cur_vma->sbrk == old_sbrk) 
    {
        // Syscall failed to expand memory (e.g., Out of Memory)
        pthread_mutex_unlock(&mmvm_lock);
        return -1; 
    }
    /*Successful increase limit */
    caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
    caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
    // NEW: User mode: U/S bit = 1
    caller->mm->symrgtbl[rgid].mode_bit = 1;
    caller->mm->symrgtbl[rgid].vmaid = cur_vma->vm_id;

    *alloc_addr = old_sbrk;

    /* Address internal fragmentation: enlist leftover space to free list */
    if (old_sbrk + size < cur_vma->sbrk)
    {
        struct vm_rg_struct *leftover_rg = malloc(sizeof(struct vm_rg_struct));
        if (leftover_rg != NULL)
        {
            leftover_rg->rg_start = old_sbrk + size;
            leftover_rg->rg_end = cur_vma->sbrk;
            leftover_rg->vmaid = cur_vma->vm_id;
            leftover_rg->mode_bit = 1; /* User mode */
            leftover_rg->rg_next = NULL;
            enlist_vm_freerg_list(caller->mm, leftover_rg);
        }
    }

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
    pthread_mutex_lock(&mmvm_lock);
  
    /* TODO: Manage the collect freed region to freerg_list */
    struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
    
    if (rgnode == NULL)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
    if (rgnode->vmaid != vmaid)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
    struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
    if (freerg_node == NULL)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
    freerg_node->rg_start = rgnode->rg_start;
    freerg_node->rg_end = rgnode->rg_end;
    freerg_node->rg_next = NULL;
    freerg_node->vmaid = rgnode->vmaid;
    enlist_vm_freerg_list(caller->mm, freerg_node);

    rgnode->rg_start = rgnode->rg_end = rgnode->vmaid = 0;
    rgnode->rg_next = NULL;

    /*enlist the obsoleted memory region */
 
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
    addr_t addr = proc->regs[reg_index];
    // ???
    // vmaid lay dau ra?
    int val = __alloc(proc, 0, reg_index, size, &addr);
    if (val == -1)
    {
        return -1;
    }
    proc->regs[reg_index] = addr;
    printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
    printf("\n[LIBALLOC] PID %d: Allocated size %lu at register %u\n", proc->pid, size, reg_index);
    printf("Virtual Address: " FORMATX_ADDR "\n", (void*) addr);

#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

    /* By default using vmaid = 0 */
    return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
    addr_t address = proc->regs[reg_index];
    int rgid = get_rgid_by_addr(proc->mm, address);
    int vmaid = get_vmaid_by_addr(proc->mm, address);
    int val = __free(proc, vmaid, rgid);
    if (val == -1)
    {
        return -1;
    }
    printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
    /* TODO dump IO content (if needed) */
    printf("\n[LIBFREE] PID %d at register %u\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
    return 0; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
    // pte: page table entry
    uint32_t pte = pte_get_entry(caller, pgn);

    if (!PAGING_PAGE_PRESENT(pte))
    { /* Page is not online, make it actively living */
        addr_t vicfpn, swpfpn;
        // addr_t tgtfpn; TODO: check this

        addr_t vicpgn;
        /* Find victim page */
        if (find_victim_page(mm, &vicpgn) == -1)
        {
            return -1;
        }
        uint32_t vicpte = pte_get_entry(caller, vicpgn);
        vicfpn = PAGING_FPN(vicpte);

        /* Get free frame in MEMSWP */
        if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
        {
            return -1;
        }

        /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/
        // vm: now we have victim frame, free frame, and tgt frame

        /* TODO copy victim frame to swap

         * SWP(vicfpn <--> swpfpn)
         * SYSCALL 1 sys_memmap
         */
        
        arg_t a1 = SYSMEM_SWP_OP;
        arg_t a2 = vicfpn;
        arg_t a3 = swpfpn;

        libsyscall(caller, 17, a1, a2, a3);
        // _syscall(caller->krnl, caller->pid, 17, &regs);

        /* Update page table */
        pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

        // Needs swapping ing
        if (pte & PAGING_PTE_SWAPPED_MASK)
        {
            // Extract the swap frame number from the target's PTE
            addr_t target_swpfpn = PAGING_SWP(pte);

            // Perform SWAP IN: Move target from SWAP to RAM
            arg_t a1 = SYSMEM_SWP_IN_OP;
            arg_t a2 = vicfpn;        // Destination frame in RAM
            arg_t a3 = target_swpfpn; // Source frame in SWAP
            libsyscall(caller, 17, a1, a2, a3);

            // The target is back in RAM, so free up its old space in the swap device
            MEMPHY_put_freefp(caller->krnl->active_mswp, target_swpfpn);
        }

        /* Update target's page table to point to the new RAM frame */
        pte_set_fpn(caller, pgn, vicfpn);

        /* Enlist the new page in the USER's FIFO tracking list */
        enlist_pgn_node(&mm->fifo_pgn, pgn);
    }

    *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

    return 0;
}

/*k_pg_getpage 
 *@mm
 *@pgn
 *@fpn
 *@caller
 */
int k_pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
        uint32_t pte = k_pte_get_entry(caller, pgn);
        if (pte == 0)
            return -1;
        *fpn = PAGING_FPN(pte);
        
        return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
    int pgn = PAGING64_PGN(addr);
    int off = PAGING64_OFFST(addr);
    int fpn;

    if (pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1; /* invalid page access */

    int phyaddr = (fpn << PAGING64_ADDR_FPN_LOBIT) + off;

    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_READ;
    regs.a2 = phyaddr;
    _syscall(caller->krnl, caller->pid, 17, &regs);
    *data = (BYTE)regs.a3;

    return 0;
}

/*k_pg_getval 
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int k_pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
    int pgn = PAGING64_PGN(addr);
    int off = PAGING64_OFFST(addr);
    int fpn;

    if (k_pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1; /* invalid page access */

    int phyaddr = (fpn << PAGING64_ADDR_FPN_LOBIT) + off;
    return MEMPHY_read(caller->krnl->mram, phyaddr, data);

}


/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
    int pgn = PAGING64_PGN(addr);
    int off = PAGING64_OFFST(addr);
    int fpn;

    /* Get the page to MEMRAM, swap from MEMSWAP if needed */
    if (pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1; /* invalid page access */

    int phyaddr = (fpn << PAGING64_ADDR_FPN_LOBIT) + off;

    arg_t a1 = SYSMEM_IO_WRITE;
    arg_t a2 = phyaddr;
    arg_t a3 = value;
    libsyscall(caller, 17, a1, a2, a3);

    /* TODO
     *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
     *  MEMPHY WRITE with SYSMEM_IO_WRITE
     * SYSCALL 17 sys_memmap
     */

    return 0;
}

/*k_pg_setval - 
 *@mm: memory region
 *@addr: virtual address to access
 *@value: value
 *
 */
int k_pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
    int pgn = PAGING64_PGN(addr);
    int off = PAGING64_OFFST(addr);
    int fpn;

    /* Get the page to MEMRAM, swap from MEMSWAP if needed */
    if (k_pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1; /* invalid page access */

    int phyaddr = (fpn << PAGING64_ADDR_FPN_LOBIT) + off;

    MEMPHY_write(caller->krnl->mram, phyaddr,value);

    return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
    struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

    /* TODO Invalid memory identify */
    if (currg == NULL || cur_vma == NULL)
    {
        return -1;
    }

    if (currg->rg_start == 0 && currg->rg_end == 0)
    {
        return -1;
    }
    if (currg->rg_start + offset >= currg->rg_end)
    {
        return -1;
    }
 
    return  pg_getval(caller->mm, currg->rg_start + offset, data, caller);
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,      // Source address = [source] + [offset]
    uint32_t *destination)
{
    BYTE data;
    printf("%s:%d\n", __func__, __LINE__);
    
    addr_t address = offset + proc->regs[source];

    if (!IS_USER_SPACE(address))
        return -1;
    
    // 27-04:
    int vmaid = get_vmaid_by_addr(proc->mm, address);
    int rgid = get_rgid_by_addr(proc->mm, address);

    int val = __read(proc, vmaid, rgid, offset, &data);

    if (val == 0)
    {
        int reg_idx = *destination;
        proc->regs[reg_idx] = (addr_t) data;
    }
    
#ifdef IODUMP
    /* TODO dump IO content (if needed) */
    printf("[LIBREAD] PID %d: read %c from reg[%d]+offset[%ld] ", proc->pid, data, source, offset);
#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

    return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
    pthread_mutex_lock(&mmvm_lock);
    struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

    if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    pg_setval(caller->mm, currg->rg_start + offset, value, caller);

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{

    addr_t address = proc->regs[destination] + offset;
    if (!IS_USER_SPACE(address))
        return -1;

    // 27-04
    int vmaid = get_vmaid_by_addr(proc->mm, address);
    int rgid = get_rgid_by_addr(proc->mm, address);
    int val = __write(proc, vmaid, rgid, offset, data);
    if (val == -1)
    {
        return -1;
    }

#ifdef IODUMP
    /* TODO dump IO content (if needed) */
    printf("\n[LIBWRITE] PID %d: data %c written to Reg[%u]+offset[%lu] - " FORMATX_ADDR "\n", proc->pid, data, destination, offset, (void*)address);
#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

    return val;
}

/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
    /* TODO: provide OS level management
     *       and forward the request to helper
     */

    addr_t addr;
    if  (__kmalloc(caller, 0, reg_index, size, &addr) != 0)
    {
        caller->regs[reg_index] = addr;
    }
    else 
    {
         return -1;
    }
       
    return 0;
}

/*

 *kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
    
    pthread_mutex_lock(&mmvm_lock);
    struct mm_struct *krnl_mm = caller->krnl->mm;

    /* Find an empty symbol region in the table instead of using the passed rgid */
    rgid = -1;
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
        if (krnl_mm->symrgtbl[i].rg_start == 0 && krnl_mm->symrgtbl[i].rg_end == 0)
        {
            rgid = i;
            break;
        }
    }
    if (rgid == -1)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return 0x0;
    }
    
    struct vm_area_struct *cur_vma = get_vma_by_num(krnl_mm, vmaid);
    struct vm_rg_struct ret_rg;

    if (cur_vma == NULL)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
       
    ret_rg.mode_bit = 0; 
    if (get_free_vmrg_area(caller,vmaid,size,&ret_rg) != 0)
    {
        addr_t old_sbrk = cur_vma->sbrk;
        if (k_inc_vma_limit(caller, vmaid, size) == -1)
        {
            pthread_mutex_unlock(&mmvm_lock);
            return 0x0;
        }
        else
        {
            ret_rg.rg_start = old_sbrk;
            ret_rg.rg_end = old_sbrk + size;
            ret_rg.vmaid = vmaid;

            // Elist the free space between region end and sbrk
            if (old_sbrk + size < cur_vma->sbrk)
            {
                struct vm_rg_struct *leftover_rg = malloc(sizeof(struct vm_rg_struct));
                if (leftover_rg != NULL)
                {
                    leftover_rg->rg_start = old_sbrk + size;
                    leftover_rg->rg_end = cur_vma->sbrk;
                    leftover_rg->vmaid = cur_vma->vm_id;
                    leftover_rg->mode_bit = 0; /* Kernel mode */
                    leftover_rg->rg_next = NULL;
                    enlist_vm_freerg_list(caller->krnl->mm, leftover_rg);
                }
            }
        }

    }
    
    
    krnl_mm->symrgtbl[rgid].rg_start = ret_rg.rg_start;
    krnl_mm->symrgtbl[rgid].rg_end = ret_rg.rg_end;
    krnl_mm->symrgtbl[rgid].vmaid = ret_rg.vmaid;
    krnl_mm->symrgtbl[rgid].mode_bit = ret_rg.mode_bit;
    *alloc_addr = ret_rg.rg_start;
   
    pthread_mutex_unlock(&mmvm_lock);
    return 0;

}


int __slab_alloc(struct pcb_t *caller, struct kcache_pool_struct *pool)
{
    /* Dynamically determine slab size */
    int min_objs = 8;
    addr_t required_size = pool->size * min_objs + pool->align;
    addr_t slab_size = PAGING64_PAGE_ALIGNSZ(required_size);

    addr_t new_slab_addr;
    if (__kmalloc(caller, 0, -1, slab_size, &new_slab_addr) != 0)
    {
        return -1;
    }
    addr_t aligned_start = new_slab_addr;
    if (pool->align > 0 && (aligned_start % pool->align) != 0) {
        aligned_start += (pool->align - (aligned_start % pool->align));
    }
    
    int usable_space = slab_size - (aligned_start - new_slab_addr);
    int num_objs = usable_space / pool->size;

    if (num_objs <= 0) {
        return -1;
    }

    struct slab_struct *new_slab = malloc(sizeof(struct slab_struct));
    new_slab->addr = aligned_start;
    new_slab->free_list = malloc(num_objs * sizeof(struct free_obj));
    new_slab->storage = 0;
    
    for (int i = 0; i < num_objs; i++)
    {
        new_slab->free_list[i].addr = aligned_start + i * pool->size;
        new_slab->free_list[i].next = (i < num_objs - 1) ? i + 1 : -1;
    }

    pthread_mutex_lock(&mmvm_lock);
    new_slab->next = pool->slabs;
    pool->slabs = new_slab;
    pool->storage = 0;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
    /* TODO: provide OS level management */
    pthread_mutex_lock(&mmvm_lock);
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
        return -1;

    struct krnl_t *krnl = caller->krnl;
    struct mm_struct *mm = krnl->mm;
    struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];

    /* Pad the size to strictly respect the alignment boundaries */
    if (align > 0 && (size % align) != 0) {
        size = size + (align - (size % align));
    }

    pool->size = size;
    pool->align = align;
    pool->storage = (addr_t) -1;
    pthread_mutex_unlock(&mmvm_lock);

    /* Pre-allocate the first slab (1 page) to keep multiple copies ready */
    return __slab_alloc(caller, pool);
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
    addr_t addr;
    /* Use the default kernel VMA (0) instead of -1 */
    if (__kmem_cache_alloc(proc, 0, reg_index, cache_pool_id, &addr) == 0)
    {
        proc->regs[reg_index] = addr;
        return 0;
    }
    return -1;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{

    pthread_mutex_lock(&mmvm_lock);
    struct mm_struct *mm = caller->krnl->mm;
    struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];

    /* Find an empty symbol region in the table instead of using the passed rgid */
    rgid = -1;
    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
        if (mm->symrgtbl[i].rg_start == 0 && mm->symrgtbl[i].rg_end == 0)
        {
            rgid = i;
            break;
        }
    }
    if (rgid == -1)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    if (pool->slabs == NULL)
    {
        /* Cache pool is not initialized properly */
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    struct slab_struct *slab = NULL;
    while (1)
    {
        slab = pool->slabs;
        while (slab != NULL && slab->storage == (addr_t) -1)
        {
            slab = slab->next;
        }
        
        if (slab != NULL)
            break; /* Found a slab with free space */
            
        /* Cache pool is exhausted, allocate new slab */
        pthread_mutex_unlock(&mmvm_lock);
        if (__slab_alloc(caller, pool) < 0)
            return -1;
        pthread_mutex_lock(&mmvm_lock);
    }

    int free_idx = (int)slab->storage;
    *alloc_addr = slab->free_list[free_idx].addr;
    slab->storage = (addr_t)slab->free_list[free_idx].next;

    /* Add to symbol table */
    mm->symrgtbl[rgid].rg_start = *alloc_addr;
    mm->symrgtbl[rgid].rg_end = *alloc_addr + pool->size;
    mm->symrgtbl[rgid].vmaid = vmaid;
    mm->symrgtbl[rgid].mode_bit = 0;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
    addr_t user_addr = caller->regs[source];
    addr_t kernel_addr = caller->regs[destination];

    int user_vmaid = get_vmaid_by_addr(caller->mm, user_addr);
    int user_rgid = get_rgid_by_addr(caller->mm, user_addr);

    int kernel_vmaid = get_vmaid_by_addr(caller->krnl->mm, kernel_addr);
    int kernel_rgid = get_rgid_by_addr(caller->krnl->mm, kernel_addr);

    if (user_vmaid < 0 || user_rgid < 0 || kernel_vmaid < 0 || kernel_rgid < 0)
        return -1;

    struct vm_rg_struct *user_rg = get_symrg_byid(caller->mm, user_rgid);
    struct vm_rg_struct *kernel_rg = get_symrg_byid(caller->krnl->mm, kernel_rgid);

    if (user_rg == NULL || kernel_rg == NULL)
        return -1;

    /* Check if the size will make it go outside of either region */
    if (user_rg->rg_start + offset + size > user_rg->rg_end ||
        kernel_rg->rg_start + offset + size > kernel_rg->rg_end)
    {
        return -1;
    }

    BYTE data;

    for (uint32_t i = 0; i < size; i++)
    {
        if (__read_user_mem(caller, user_vmaid, user_rgid, offset + i, &data) != 0)
        {
            return -1;
        }
        if (__write_kernel_mem(caller, kernel_vmaid, kernel_rgid, offset + i, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
    addr_t kernel_addr = caller->regs[source];
    addr_t user_addr = caller->regs[destination];

    int kernel_vmaid = get_vmaid_by_addr(caller->krnl->mm, kernel_addr);
    int kernel_rgid = get_rgid_by_addr(caller->krnl->mm, kernel_addr);

    int user_vmaid = get_vmaid_by_addr(caller->mm, user_addr);
    int user_rgid = get_rgid_by_addr(caller->mm, user_addr);

    if (kernel_vmaid < 0 || kernel_rgid < 0 || user_vmaid < 0 || user_rgid < 0)
        return -1;

    struct vm_rg_struct *user_rg = get_symrg_byid(caller->mm, user_rgid);
    struct vm_rg_struct *kernel_rg = get_symrg_byid(caller->krnl->mm, kernel_rgid);

    if (user_rg == NULL || kernel_rg == NULL)
        return -1;

    /* Check if the size will make it go outside of either region */
    if (kernel_rg->rg_start + offset + size > kernel_rg->rg_end ||
        user_rg->rg_start + offset + size > user_rg->rg_end)
    {
        return -1;
    }

    BYTE data;

    for (uint32_t i = 0; i < size; i++)
    {
        if (__read_kernel_mem(caller, kernel_vmaid, kernel_rgid, offset + i, &data) != 0)
        {
            return -1;
        }
        if (__write_user_mem(caller, user_vmaid, user_rgid, offset + i, data) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{

    /* TODO: provide OS memory operator for kernel memory region */
    // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

    struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (currg == NULL || cur_vma == NULL)
    {
       return -1;
    }


    if (currg->rg_start == 0 && currg->rg_end == 0)
    {
       pthread_mutex_unlock(&mmvm_lock);
       return -1;
    }
    if (currg->rg_start + offset >= currg->rg_end)
    {
       return -1;
    }
    
    return k_pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
    /* TODO: provide OS memory operator for kernel memory region */
    // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  
    pthread_mutex_lock(&mmvm_lock);
    struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
    struct vm_area_struct* cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (currg == NULL || cur_vma == NULL)
    {
       pthread_mutex_unlock(&mmvm_lock);
       return -1;
    }
    if (currg->rg_start == 0 && currg->rg_end == 0)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
    if (currg->rg_start + offset >= currg->rg_end)
    {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    k_pg_setval(caller->krnl->mm,  currg->rg_start + offset, value, caller);
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
    return __read(caller, vmaid, rgid, offset,data);
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
   return __write(caller,vmaid, rgid, offset,value);
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 (userspace)
 */
int free_pcb_memph(struct pcb_t *caller) // TODO: review concept of demand paging
{
    int fpn;
    uint32_t pte;

    pthread_mutex_lock(&mmvm_lock);
    struct vm_area_struct *vma = caller->mm->mmap;
    
    while (vma != NULL)
    {
        for (addr_t vaddr = vma->vm_start; vaddr < vma->vm_end; vaddr+=PAGING64_PAGESZ) {
            addr_t pgn = PAGING64_PGN(vaddr);
            
            pte = pte_get_entry(caller, pgn);

            if (pte == 0) {
                continue; // Page is empty
            }

            if (PAGING_PAGE_PRESENT(pte))
            {
                fpn = PAGING_FPN(pte);
                MEMPHY_put_freefp(caller->krnl->mram, fpn);
            }
            else
            {
                fpn = PAGING_SWP(pte);
                MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
            }

            pte_set_entry(caller, pgn, 0);
        }
        vma = vma->vm_next;
    }
  

    // [Original code]
    // for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
    // {
    //     pte = caller->mm->pgd[pagenum]; // TODO: (fixed: from krnl->mm to caller->mm)

    //     if (PAGING_PAGE_PRESENT(pte))
    //     {
    //         fpn = PAGING_FPN(pte);
    //         MEMPHY_put_freefp(caller->krnl->mram, fpn);
    //     }
    //     else
    //     {
    //         fpn = PAGING_SWP(pte);
    //         MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    //     }
    // }

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
    struct pgn_t *pg = mm->fifo_pgn;

    /* TODO: Implement the theorical mechanism to find the victim page */
    if (pg == NULL)
    {
        return -1;
    }
    struct pgn_t *prev = NULL;
    while (pg->pg_next)
    {
        prev = pg;
        pg = pg->pg_next;
    }
    *retpgn = pg->pgn;
    if (prev == NULL)
    {
        mm->fifo_pgn = NULL;
    }
    else
    {
        prev->pg_next = NULL;
    }
    

    free(pg);

    return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 * update 25/04: check newrg mode bit to access suitable mm
 * 
 *             
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
    struct vm_area_struct *cur_vma;
    if (KERNELMODEBIT(newrg->mode_bit))
    {
         cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    } 
    else 
    {
         cur_vma = get_vma_by_num(caller->mm, vmaid);
    }
   

    struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

    if (rgit == NULL)
        return -1;

    /* Probe unintialized newrg */
    newrg->rg_start = newrg->rg_end = -1;

    /* Traverse on list of free vm region to find a fit space */
    while (rgit != NULL)
    {
        if (rgit->rg_start + size <= rgit->rg_end)
        { /* Current region has enough space */
            newrg->rg_start = rgit->rg_start;
            newrg->rg_end = rgit->rg_start + size;
            
            /* Update left space in chosen region */
            if (rgit->rg_start + size < rgit->rg_end)
            {
                rgit->rg_start = rgit->rg_start + size;
            }
            else
            { /*Use up all space, remove current node */
                /*Clone next rg node */
                struct vm_rg_struct *nextrg = rgit->rg_next;

                /*Cloning */
                if (nextrg != NULL)
                {
                    rgit->rg_start = nextrg->rg_start;
                    rgit->rg_end = nextrg->rg_end;

                    rgit->rg_next = nextrg->rg_next;

                    free(nextrg);
                }
                else
                {                                  /*End of free list */
                    rgit->rg_start = rgit->rg_end; // dummy, size 0 region
                    rgit->rg_next = NULL;
                }
            }
            break;
        }
        else
        {
            rgit = rgit->rg_next; // Traverse next rg
        }
    }

    if (newrg->rg_start == -1) // new region not found
        return -1;

    return 0;
}

// #endif
