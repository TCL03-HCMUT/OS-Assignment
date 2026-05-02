#include "memlog.h"

//Should use this function in _alloc _kmalloc or free()
void dump_mm_layout(struct pcb_t *caller, const char *name)
{
    struct mm_struct *mm = caller->mm;
    if (mm == NULL) {
        printf("%s: mm is NULL\n", name);
        return;
    }

    printf("\n===== MEMORY LAYOUT: %s %d=====\n", name, caller->pid);

    struct vm_area_struct *vma = mm->mmap;

    while (vma != NULL) {
        printf("VMA %lu: vm_start=0x%lx, sbrk=0x%lx, vm_end=0x%lx\n",
               vma->vm_id,
               vma->vm_start,
               vma->sbrk,
               vma->vm_end);

        printf("  Regions:\n");

        for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
            struct vm_rg_struct *rg = &mm->symrgtbl[i];
            if (rg->rg_start < rg->rg_end && rg->vmaid == vma->vm_id) {
                printf("    rgid %d: [0x%lx, 0x%lx), mode=%s\n", i,rg->rg_start,rg->rg_end, rg->mode_bit ? "user" : "kernel");
                int start_pgn = rg->rg_start / PAGING64_PAGESZ;
                int end_pgn   = (rg->rg_end - 1) / PAGING64_PAGESZ;
                printf("      Pages: "); 
                for (int p = start_pgn; p <= end_pgn; p++) 
                {
                    printf("%d ", p);
                }
                printf("\n");

                for (int p = start_pgn; p <= end_pgn; p++) {
                    uint32_t pte = pte_get_entry(caller, p);
                    if (PAGING_PAGE_PRESENT(pte)) 
                    {
                        printf("        Page %d -> Frame %d\n", p, PAGING_FPN(pte));
                    } 
                    else if (pte & PAGING_PTE_SWAPPED_MASK) 
                    {
                        printf("        Page %d -> Swap %d\n", p, PAGING_SWP(pte));
                    } 
                    else 
                    {
                        printf("        Page %d -> Not mapped\n", p);
                    }
                }
            } 
        }

        vma = vma->vm_next;
    }

    printf("================================\n");
}