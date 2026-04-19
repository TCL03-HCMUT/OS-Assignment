/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg);
/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
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

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
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
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (cur_vma == NULL) 
  {
    return NULL;
  }
  newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL){
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
  //struct vm_area_struct *vma = caller->krnl->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
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
  //struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));

  /* TOTO with new address scheme, the size need tobe aligned 
   *      the raw inc_sz maybe not fit pagesize
   */ 
  //addr_t inc_amt;

//  int incnumpage =  inc_amt / PAGING_PAGESZ;

  /* TODO Validate overlap of obtained region */
  //if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
  //  return -1; /*Overlap and failed allocation */

  /* TODO: Obtain the new vm area based on vmaid */
  //cur_vma->vm_end... 
  // inc_limit_ret...
  /* The obtained vm area (only)
   * now will be alloc real ram region */

//  if (vm_map_ram(caller, area->rg_start, area->rg_end, 
//                   old_end, incnumpage , newrg) < 0)
//    return -1; /* Map the memory to MEMRAM */

  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  struct vm_rg_struct *area;
  struct vm_area_struct *cur_vma;
  addr_t inc_amt, old_end;
  int incnumpage;
  if (newrg == NULL)
  {
    return -1;
  }

  inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  incnumpage = inc_amt / PAGING_PAGESZ;
  area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
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
  int validate = validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end);
  if ( validate < 0 )
  {
    free(area);
    free(newrg);
    return -1;
  }
  cur_vma->vm_end = old_end + inc_amt;
  cur_vma->sbrk = area->rg_end;
  int vm_map_ram_result = vm_map_ram(caller, area->rg_start, area->rg_end, old_end,incnumpage,newrg);
  if ( vm_map_ram_result != 0)
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
  mapsz = (addr_t)incpgnum * PAGING_PAGESZ;
  if (mapstart < astart || (mapstart + mapsz) > aend)
  {
    return -1;
  }
  return vm_map_ram(caller, astart, aend, mapstart, incpgnum, ret_rg);
}

addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  addr_t ret;
  struct krnl_t *krnl;
  struct mm_struct *mm;
  if (caller == NULL || caller->krnl == NULL || ret_rg == NULL)
  {
    return -1;
  }
  krnl = caller->krnl;
  mm= krnl->mm;
  if (mm == NULL)
  {
    return -1;
  }
  #ifdef MM64
  {
    addr_t *saved_pgd = mm->pgd;
    addr_t *saved_p4d = mm->p4d;
    addr_t *saved_pud = mm->pud;
    addr_t *saved_pmd = mm->pmd;
    addr_t *saved_pt  = mm->pt;

    if (krnl->krnl_pgd != NULL) mm->pgd = krnl->krnl_pgd;
    if (krnl->krnl_p4d != NULL) mm->p4d = krnl->krnl_p4d;
    if (krnl->krnl_pud != NULL) mm->pud = krnl->krnl_pud;
    if (krnl->krnl_pmd != NULL) mm->pmd = krnl->krnl_pmd;
    if (krnl->krnl_pt  != NULL) mm->pt  = krnl->krnl_pt;

    ret = vm_map_range(caller,astart,aend,mapstart,incpgnum,ret_rg);
    mm->pgd = saved_pgd;
    mm->p4d = saved_p4d;
    mm->pud = saved_pud;
    mm->pmd = saved_pmd;
    mm->pt = saved_pt;
  }
  #else
  {
    uint32_t *savedpgd = mm->pgd;
    if (krnl->krnl_pgd != NULL)
    {
      mm->pgd = krnl->krnl_pgd;
    }
    ret = vm_map_range(caller, astart, aend, mapstart, incpgnum, ret_rg);
  }
  #endif
  return ret;
}
// #endif
