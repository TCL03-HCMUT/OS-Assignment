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
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#if defined (MM64)
	#include "mm64.h"
#endif
// addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);
/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{	
	if (mm == NULL)
	{
		return NULL;
	}
	struct vm_area_struct *pvma = mm->mmap;

	if (mm->mmap == NULL)
		return NULL;

	int vmait = pvma->vm_id;

	while (pvma != NULL)
	{
		if ((int)pvma->vm_id == vmaid)
		{
			return pvma;
		}
		pvma = pvma->vm_next;
	}
	return NULL;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn, addr_t swpfpn)
{
	__swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
	return 0;
}

int __mm_swap_in_page(struct pcb_t *caller, addr_t ramfpn, addr_t swpfpn)
{
    // __swap_cp_page (source, source_frame, destination, dest_frame)
    __swap_cp_page(caller->krnl->active_mswp, swpfpn, caller->krnl->mram, ramfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
	struct vm_rg_struct *newrg;
	/* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
	// struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

	// newrg = malloc(sizeof(struct vm_rg_struct));

	/* TODO: update the newrg boundary
	// newrg->rg_start = ...
	// newrg->rg_end = ...
	*/
	if (caller == NULL || caller->mm == NULL)
	{
		return -1;
	}
	struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

	if (cur_vma == NULL)
	{
		return NULL;
	}
	newrg = malloc(sizeof(struct vm_rg_struct));
	if (newrg == NULL)
	{
		return NULL;
	}
	newrg->rg_start = cur_vma->sbrk;
	newrg->rg_end = newrg->rg_start + alignedsz;
	newrg->rg_next = NULL;
	/* END TODO */

	return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
	// struct vm_area_struct *vma = caller->krnl->mm->mmap;

	/* TODO validate the planned memory area is not overlapped */
	if (vmastart >= vmaend || caller == NULL )
	{
		return -1;
	}

	struct vm_area_struct *vma = caller->mm->mmap;
	if (vma == NULL)
	{
		return -1;
	}

	/* TODO validate the planned memory area is not overlapped */

	struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
	if (cur_area == NULL)
	{
		return -1;
	}

	while (vma != NULL)
	{
		if (vmaid != vma->vm_id && OVERLAP(vma->vm_start, vma->vm_end, vmastart, vmaend))
		{
			return -1;
		}
		vma = vma->vm_next;
	}
	/* End TODO*/

	return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
	// struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));

	/* TOTO with new address scheme, the size need tobe aligned
	 *      the raw inc_sz maybe not fit pagesize
	 */
	// addr_t inc_amt;

	//  int incnumpage =  inc_amt / PAGING_PAGESZ;

	/* TODO Validate overlap of obtained region */
	// if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
	//   return -1; /*Overlap and failed allocation */

	/* TODO: Obtain the new vm area based on vmaid */
	// cur_vma->vm_end...
	//  inc_limit_ret...
	/* The obtained vm area (only)
	 * now will be alloc real ram region */

	//  if (vm_map_ram(caller, area->rg_start, area->rg_end,
	//                   old_end, incnumpage , newrg) < 0)
	//    return -1; /* Map the memory to MEMRAM */
	if (caller == NULL)
	{
		return -1;
	}
	struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
	struct vm_rg_struct *area;
	struct vm_area_struct *cur_vma;
	addr_t inc_amt, old_end;
	int incnumpage;
	if (newrg == NULL)
	{
		return -1;
	}
	#if defined (MM64)
	inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
	incnumpage = inc_amt / PAGING64_PAGESZ;
	#else
	inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
	incnumpage = inc_amt / PAGING_PAGESZ;
	#endif
	area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
	if (area == NULL)
	{
		free(newrg);
		return -1;
	}

	cur_vma = get_vma_by_num(caller->mm, vmaid);
	if (cur_vma == NULL)
	{
		free(area);
		free(newrg);
		return -1;
	}
	old_end = cur_vma->vm_end;
	int validate = validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end);
	if (validate < 0)
	{
		free(area);
		free(newrg);
		return -1;
	}
	cur_vma->vm_end = old_end + inc_amt;
	cur_vma->sbrk = area->rg_end;
	int vm_map_ram_result = vm_map_ram(caller, area->rg_start, area->rg_end, old_end, incnumpage, newrg);
	if (vm_map_ram_result != 0)
	{
		cur_vma->vm_end = old_end;
		cur_vma->sbrk = area->rg_start;
		free(area);
		free(newrg);
		return -1;
	}
	free(area);
	free(newrg);
	return 0;
}

addr_t vm_map_range(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
	addr_t mapsz;
	if (caller == NULL || caller->krnl == NULL || ret_rg == NULL)
	{
		return -1;
	}
	if (incpgnum <= 0 || astart >= aend)
	{
		return -1;
	}
	#if defined (MM64)
	mapsz = (addr_t)incpgnum * PAGING64_PAGESZ;
	#else
	mapsz = (addr_t)incpgnum * PAGING_PAGESZ;
	#endif
	if (mapstart < astart || (mapstart + mapsz) > aend)
	{
		return -1;
	}
	return vm_map_ram(caller, astart, aend, mapstart, incpgnum, ret_rg);
}
static void put_back_contiguous_frames(struct memphy_struct *mram, struct framephy_struct *frm_lst)
{
	struct framephy_struct *fpit = frm_lst;
	while (fpit != NULL)
	{
		MEMPHY_put_freefp(mram, fpit->fpn);
		fpit = fpit->fp_next;
	}
}
addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
	struct framephy_struct *frmlst = NULL;
	struct mm_struct *user_mm = NULL;
	addr_t mapsz;
	int ret;

	if (caller == NULL || caller->krnl == NULL || ret_rg == NULL)
	{
		return -1;
	}
	if (caller->krnl->mram == NULL)
	{
		return -1;
	}
	if (incpgnum <= 0 || astart >= aend)
	{
		return -1;
	}
	#if defined (MM64)
	mapsz = (addr_t)incpgnum * PAGING64_PAGESZ;
	#else
	mapsz = (addr_t)incpgnum * PAGING_PAGESZ;
	#endif
	if (mapstart < astart || (mapstart + mapsz) > aend)
	{
		return -1;
	}

	/* get contiguous physical mem*/
	ret = MEMPHY_get_contiguous_freefp(caller->krnl->mram, incpgnum, &frmlst);
	if (ret != 0 || frmlst == NULL)
	{
		return -1;
	}
	// user_mm = caller->mm;
	// caller->mm = caller->krnl->mm;

	ret = k_vmap_page_range(caller, mapstart, incpgnum, frmlst, ret_rg);
	// caller->mm = user_mm;
	if (ret < 0)
	{
		put_back_contiguous_frames(caller->krnl->mram, frmlst);
		return -1;
	}
	ret_rg->rg_start = mapstart;
	ret_rg->rg_end = mapstart + mapsz;
	ret_rg->mode_bit = 0;

	return 0;
}


int get_rgid_by_addr(struct mm_struct *mm, addr_t addr)
{
	int i;
	if (mm == NULL)
	{
		return -1;
	}
	for (i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
	{
		struct vm_rg_struct *rg = &mm->symrgtbl[i];
		if (rg->rg_start < rg->rg_end)
		{
			if (rg->rg_start <= addr && addr < rg->rg_end) // May be wrong or true (Depend on sample code's true or false)
			{
				return i;
			}
		}
	}
	return -1;
}

int get_vmaid_by_addr(struct mm_struct *mm, addr_t addr)
{
	struct vm_area_struct *vma;
	if (mm == NULL)
	{
		return -1;
	}

	vma= mm->mmap;
	while (vma != NULL)
	{
		if (vma->vm_start < vma->vm_end)
		{
			if (vma->vm_start <= addr && vma->vm_end > addr ) // May be wrong or true (Depend on sample code's true or false)
			{
				return vma->vm_id;
			}
		}
		vma = vma->vm_next;
	}
	return -1;
}


int k_validate_overlap_vm_area(struct krnl_t *krnl, int vmaid, addr_t vmastart, addr_t vmaend)
{
	// struct vm_area_struct *vma = caller->krnl->mm->mmap;
	if (krnl == NULL)
	{
		return -1;
	}
	/* TODO validate the planned memory area is not overlapped */
	if (vmastart >= vmaend)
	{
		return -1;
	}

	struct vm_area_struct *vma = krnl->mm->mmap;
	if (vma == NULL)
	{
		return -1;
	}

	/* TODO validate the planned memory area is not overlapped */

	struct vm_area_struct *cur_area = get_vma_by_num(krnl->mm, vmaid);
	if (cur_area == NULL)
	{
		return -1;
	}

	while (vma != NULL)
	{
		if (vmaid != vma->vm_id && OVERLAP(vma->vm_start, vma->vm_end, vmastart, vmaend))
		{
			return -1;
		}
		vma = vma->vm_next;
	}
	/* End TODO*/

	return 0;
}


struct vm_rg_struct *k_get_vm_area_node_at_brk(struct krnl_t *krnl, int vmaid, addr_t size, addr_t alignedsz)
{
	struct vm_rg_struct *newrg;
	/* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
	// struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

	// newrg = malloc(sizeof(struct vm_rg_struct));

	/* TODO: update the newrg boundary
	// newrg->rg_start = ...
	// newrg->rg_end = ...
	*/
	if (krnl == NULL || krnl->mm == NULL)
	{
		return -1;
	}
	struct vm_area_struct *cur_vma = get_vma_by_num(krnl->mm, vmaid);

	if (cur_vma == NULL)
	{
		return NULL;
	}
	newrg = malloc(sizeof(struct vm_rg_struct));
	if (newrg == NULL)
	{
		return NULL;
	}
	newrg->rg_start = cur_vma->sbrk;
	newrg->rg_end = newrg->rg_start + alignedsz;
	newrg->rg_next = NULL;
	/* END TODO */

	return newrg;
}


int k_inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
	if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
	{
		return -1;
	}
	struct krnl_t *krnl = caller->krnl;
	struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
	struct vm_rg_struct *area;
	struct vm_area_struct *cur_vma;
	addr_t inc_amt, old_end;
	int incnumpage;
	if (newrg == NULL)
	{
		return -1;
	}
	#if defined (MM64)
	inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
	incnumpage = inc_amt / PAGING64_PAGESZ;
	#else
	inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
	incnumpage = inc_amt / PAGING_PAGESZ;
	#endif
	area = k_get_vm_area_node_at_brk(krnl, vmaid, inc_sz, inc_amt);
	if (area == NULL)
	{
		free(newrg);
		return -1;
	}

	cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
	if (cur_vma == NULL)
	{
		free(area);
		free(newrg);
		return -1;
	}
	old_end = cur_vma->vm_end;
	int validate = k_validate_overlap_vm_area(krnl, vmaid, area->rg_start, area->rg_end);
	if (validate < 0)
	{
		free(area);
		free(newrg);
		return -1;
	}
	cur_vma->vm_end = old_end + inc_amt;
	cur_vma->sbrk = area->rg_end;
	//int vm_map_result = k_vm_map_ram(caller, area->rg_start, area->rg_end, old_end, incnumpage, newrg);
	int vm_map_result = vm_map_kernel(caller,area->rg_start,area->rg_end,old_end, incnumpage, newrg);
	if (vm_map_result != 0)
	{
		cur_vma->vm_end = old_end;
		cur_vma->sbrk = area->rg_start;
		free(area);
		free(newrg);
		return -1;
	}
	free(area);
	free(newrg);
	return 0;
}

// #endif
