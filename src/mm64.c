#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#if defined(MM64)

// ----------------------- HELPER FUNCTION -------------------------------------------------------- //
/*
 * get_pte_ptr - Helper function to perform a 5-level page table walk (read-only)
 * @pgd_root: Pointer to the root Page Global Directory
 * @pgn     : Target Page Number to locate
 *
 * Returns a pointer to the corresponding Page Table Entry (PTE) if the entire
 * hierarchy exists. Returns NULL if any intermediate directory is missing.
 */
static addr_t *get_pte_ptr(addr_t *pgd_root, addr_t pgn)
{
	addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
	get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

	if (pgd_root == NULL || pgd_root[pgd_idx] == 0)
		return NULL;

	addr_t *p4d = (addr_t *)(pgd_root[pgd_idx]);
	if (p4d[p4d_idx] == 0)
		return NULL;

	addr_t *pud = (addr_t *)(p4d[p4d_idx]);
	if (pud[pud_idx] == 0)
		return NULL;

	addr_t *pmd = (addr_t *)(pud[pud_idx]);
	if (pmd[pmd_idx] == 0)
		return NULL;

	addr_t *pt = (addr_t *)(pmd[pmd_idx]);
	return &pt[pt_idx];
}

/*
 * alloc_pte_ptr - Helper function to perform an allocating 5-level page table walk
 * @pgd_root: Pointer to the root Page Global Directory
 * @pgn     : Target Page Number to map
 *
 * Traverses the page table hierarchy for the given page number. If any intermediate
 * directories (P4D, PUD, PMD, PT) are missing, it proactively allocates and
 * zero-initializes them. Returns a pointer to the target PTE.
 */
static addr_t *alloc_pte_ptr(addr_t *pgd_root, addr_t pgn)
{
	if (pgd_root == NULL)
		return NULL;

	addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
	get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

	if (pgd_root[pgd_idx] == 0)
	{
		addr_t *new_p4d = malloc(PAGING64_PAGESZ);
		memset(new_p4d, 0, PAGING64_PAGESZ);
		pgd_root[pgd_idx] = (addr_t)new_p4d;
	}
	addr_t *p4d = (addr_t *)(pgd_root[pgd_idx]);

	if (p4d[p4d_idx] == 0)
	{
		addr_t *new_pud = malloc(PAGING64_PAGESZ);
		memset(new_pud, 0, PAGING64_PAGESZ);
		p4d[p4d_idx] = (addr_t)new_pud;
	}
	addr_t *pud = (addr_t *)(p4d[p4d_idx]);

	if (pud[pud_idx] == 0)
	{
		addr_t *new_pmd = malloc(PAGING64_PAGESZ);
		memset(new_pmd, 0, PAGING64_PAGESZ);
		pud[pud_idx] = (addr_t)new_pmd;
	}
	addr_t *pmd = (addr_t *)(pud[pud_idx]);

	if (pmd[pmd_idx] == 0)
	{
		addr_t *new_pt = malloc(PAGING64_PAGESZ);
		memset(new_pt, 0, PAGING64_PAGESZ);
		pmd[pmd_idx] = (addr_t)new_pt;
	}
	addr_t *pt = (addr_t *)(pmd[pmd_idx]);

	return &pt[pt_idx];
}

/*
 * __init_mm_common - Common helper to initialize a memory management structure
 * @mm:            The mm_struct to initialize
 * @vm_start_addr: The starting virtual address for the memory space
 * @pgd_root:      Pointer to the root of the page table hierarchy
 *
 * Initializes the core components of an mm_struct, including setting up the
 * page directory pointers, creating a default virtual memory area (VMA) starting
 * at vm_start_addr, and zeroing out the symbol region table. This function
 * is shared between user-space (init_mm) and kernel-space (k_init_mm) setup.
 */
static int __init_mm_common(struct mm_struct *mm, addr_t vm_start_addr, addr_t *pgd_root)
{
	mm->pgd = pgd_root;
	mm->p4d = NULL;
	mm->pud = NULL;
	mm->pmd = NULL;
	mm->pt = NULL;

	// One default VMA (0)
	struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
	vma0->vm_id = 0;
	vma0->vm_start = vm_start_addr;
	vma0->vm_end = vma0->vm_start;
	vma0->sbrk = vma0->vm_start;
	vma0->vm_freerg_list = NULL;

	struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end); // Set region to have size 0 (start = end = 0)
	enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);						  // Size = 0 is not the same as "Doesn't exist"

	vma0->vm_next = NULL;
	vma0->vm_mm = mm;

	mm->mmap = vma0;
	mm->fifo_pgn = NULL;

	// Initialize symbol table from 0 - 29
	for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
	{
		mm->symrgtbl[i].rg_start = 0;
		mm->symrgtbl[i].rg_end = 0;
		mm->symrgtbl[i].rg_next = NULL;
	}

	return 0;
}

// ----------------------- END OF HELPER FUNCTION ------------------------------------------------- //




// ----------------------- COMMON FUNCTION -------------------------------------------------------- //
/*
 * get_pd_from_address - Extract 5-level page directory indices from a virtual address
 * @addr  : The 64-bit virtual address to parse
 * @pgd   : Output pointer for Page Global Directory index
 * @p4d   : Output pointer for Page Level 4 Directory index
 * @pud   : Output pointer for Page Upper Directory index
 * @pmd   : Output pointer for Page Middle Directory index
 * @pt    : Output pointer for Page Table index
 *
 * Uses bitwise macros to extract the specific index for each level of the
 * 64-bit paging hierarchy from the given virtual address.
 */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
	*pgd = PAGING64_ADDR_PGD(addr);
	*p4d = PAGING64_ADDR_P4D(addr);
	*pud = PAGING64_ADDR_PUD(addr);
	*pmd = PAGING64_ADDR_PMD(addr);
	*pt = PAGING64_ADDR_PT(addr);
	return 0;
}

/*
 * get_pd_from_pagenum - Extract 5-level page directory indices from a page number
 * @pgn   : The Page Number to parse
 * @pgd   : Output pointer for Page Global Directory index
 * @p4d   : Output pointer for Page Level 4 Directory index
 * @pud   : Output pointer for Page Upper Directory index
 * @pmd   : Output pointer for Page Middle Directory index
 * @pt    : Output pointer for Page Table index
 *
 * Shifts the given page number back into a full virtual address, then extracts
 * the indices for all 5 levels of the paging hierarchy.
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
							   pgd, p4d, pud, pmd, pt);
}

/*
 * init_pte - Initialize a Page Table Entry (PTE) with specific flags and values
 * @pte   : Pointer to the PTE to initialize
 * @present   : Present bit flag (1 if the page is valid/mapped, 0 otherwise)
 * @fpn   : Frame Physical Number (used if the page is in RAM)
 * @drt   : Dirty bit flag (1 if modified, 0 otherwise)
 * @swp   : Swapped bit flag (1 if the page is in swap space, 0 if in RAM)
 * @swptyp: Swap type/device index (used if the page is swapped)
 * @swpoff: Swap offset (used if the page is swapped)
 *
 * Formats the raw PTE value based on whether the page resides in physical RAM
 * or has been swapped out to a secondary storage device. Returns 0 on success,
 * or -1 if an invalid configuration (like a 0 FPN for a non-swapped page) is given.
 */
int init_pte(addr_t *pte, int pre, addr_t fpn, int drt, int swp, int swptyp, addr_t swpoff)
{
	if (pre != 0)
	{
		if (swp == 0)
		{ // Non swap ~ page online
			if (fpn == 0)
				return -1; // Invalid setting

			/* Valid setting with FPN */
			SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
			CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
			CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

			SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
		}
		else
		{ // page swapped
			CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
			SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
			CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

			SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
			SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
		}
	}

	return 0;
}

/*
 * pte_set_swap - Set PTE entry for a swapped out page
 * @caller : Pointer to the PCB of the caller
 * @pgn    : Target Page Number to map to swap
 * @swptyp : Swap type/device index
 * @swpoff : Swap offset
 *
 * Allocates/locates the PTE for the given page number and configures it
 * to indicate that the page resides in swap space. Sets the PRESENT and
 * SWAPPED bits, and stores the swap device index and offset.
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
	if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL)
		return -1;

#ifdef MM64
	addr_t *pte = alloc_pte_ptr(caller->mm->pgd, pgn);
	if (!pte)
		return -1;
#else
	uint32_t *pte = (uint32_t *)&caller->mm->pgd[pgn];
#endif

	CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
	SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
	SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
	SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

	return 0;
}

/*
 * alloc_pages_range - Allocate a sequence of physical frames in RAM
 * @caller    : Pointer to the PCB of the caller
 * @req_pgnum : Number of physical frames requested
 * @frm_lst   : Output pointer to the linked list of allocated frames
 *
 * Attempts to acquire the requested number of free frames from the RAM
 * memory device. If it fails to allocate the full amount (due to out-of-memory),
 * it releases any already allocated frames and returns an error.
 */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
	int pgit;
	addr_t fpn;
	struct framephy_struct *last_node = NULL;

	for (pgit = 0; pgit < req_pgnum; pgit++)
	{
		// Mram -> free physical page?
		if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0) // check
		{
			struct framephy_struct *newfp_str = malloc(sizeof(struct framephy_struct));
			newfp_str->fpn = fpn;
			newfp_str->fp_next = NULL;

			// Link to linked list
			if (*frm_lst == NULL)
			{
				*frm_lst = newfp_str; // Head of the list
			}
			else
			{
				last_node->fp_next = newfp_str; // Attach to the end
			}
			last_node = newfp_str;
		}
		else
		{
			/* Clean up already allocated frames to avoid memory leaks */
			struct framephy_struct *curr = *frm_lst;
			while (curr != NULL)
			{
				struct framephy_struct *next = curr->fp_next;
				MEMPHY_put_freefp(caller->krnl->mram, curr->fpn);
				free(curr);
				curr = next;
			}
			*frm_lst = NULL;
			return -3000; // Error code
		}
	}
	return 0;
}

/*
 * __swap_cp_page - Copy a full page of data between memory devices
 * @mpsrc  : Source physical memory device (e.g., RAM or Swap)
 * @srcfpn : Source Frame Physical Number (FPN)
 * @mpdst  : Destination physical memory device
 * @dstfpn : Destination Frame Physical Number (FPN)
 *
 * Reads byte-by-byte from the source frame and writes to the destination frame.
 * Typically used for moving pages between active RAM and Swap storage.
 */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
				   struct memphy_struct *mpdst, addr_t dstfpn)
{
	int cellidx;
	addr_t addrsrc, addrdst;
	for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
	{
		addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
		addrdst = dstfpn * PAGING_PAGESZ + cellidx;

		BYTE data;
		MEMPHY_read(mpsrc, addrsrc, &data);
		MEMPHY_write(mpdst, addrdst, data);
	}

	return 0;
}

/*
 * init_vm_rg - Initialize a virtual memory region structure
 * @rg_start : Start address of the region
 * @rg_end   : End address of the region
 *
 * Allocates and initializes a vm_rg_struct to represent contiguous
 * memory segment boundaries.
 */
struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
	struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

	rgnode->rg_start = rg_start;
	rgnode->rg_end = rg_end;
	rgnode->rg_next = NULL;

	return rgnode;
}

/*
 * enlist_vm_rg_node - Insert a memory region into a linked list
 * @rglist : Pointer to the head of the region list
 * @rgnode : Region node to be inserted
 *
 * Prepends the provided virtual memory region node to the given list.
 */
int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
	rgnode->rg_next = *rglist;
	*rglist = rgnode;

	return 0;
}

/*
 * enlist_pgn_node - Insert a page number into a tracking list
 * @plist : Pointer to the head of the page number list
 * @pgn   : Page number to track
 *
 * Prepends the provided page number to a linked list. Mainly used
 * for tracking allocated pages in the FIFO page replacement queue.
 */
int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
	struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

	pnode->pgn = pgn;
	pnode->pg_next = *plist;
	*plist = pnode;

	return 0;
}

/*
 * print_list_fp - Print a linked list of physical frames
 * @ifp : Pointer to the head of the frame list
 *
 * Iterates through the list of physical frames and prints their
 * Frame Physical Numbers (FPNs) for debugging purposes.
 */
int print_list_fp(struct framephy_struct *ifp)
{
	struct framephy_struct *fp = ifp;

	printf("print_list_fp: ");
	if (fp == NULL)
	{
		printf("NULL list\n");
		return -1;
	}
	printf("\n");
	while (fp != NULL)
	{
		printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
		fp = fp->fp_next;
	}
	printf("\n");
	return 0;
}

/*
 * print_list_rg - Print a linked list of virtual memory regions
 * @irg : Pointer to the head of the region list
 *
 * Iterates through the list of virtual memory regions and prints their
 * start and end addresses for debugging purposes.
 */
int print_list_rg(struct vm_rg_struct *irg)
{
	struct vm_rg_struct *rg = irg;

	printf("print_list_rg: ");
	if (rg == NULL)
	{
		printf("NULL list\n");
		return -1;
	}
	printf("\n");
	while (rg != NULL)
	{
		printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
		rg = rg->rg_next;
	}
	printf("\n");
	return 0;
}

/*
 * print_list_vma - Print a linked list of virtual memory areas
 * @ivma : Pointer to the head of the VMA list
 *
 * Iterates through the list of virtual memory areas and prints their
 * start and end addresses for debugging purposes.
 */
int print_list_vma(struct vm_area_struct *ivma)
{
	struct vm_area_struct *vma = ivma;

	printf("print_list_vma: ");
	if (vma == NULL)
	{
		printf("NULL list\n");
		return -1;
	}
	printf("\n");
	while (vma != NULL)
	{
		printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
		vma = vma->vm_next;
	}
	printf("\n");
	return 0;
}

/*
 * print_list_pgn - Print a linked list of tracked page numbers
 * @ip : Pointer to the head of the page number list
 *
 * Iterates through the list of page numbers and prints their values.
 * Used for debugging the FIFO tracking queue.
 */
int print_list_pgn(struct pgn_t *ip)
{
	printf("print_list_pgn: ");
	if (ip == NULL)
	{
		printf("NULL list\n");
		return -1;
	}
	printf("\n");
	while (ip != NULL)
	{
		printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
		ip = ip->pg_next;
	}
	printf("n");
	return 0;
}

// ----------------------- END OF COMMON FUNCTION ------------------------------------------------- //




// ----------------------- KERNEL FUNCTION -------------------------------------------------------- //
/*
 * k_pte_set_fpn - Set the Frame Physical Number (FPN) for a Kernel PTE
 * @caller: Pointer to the PCB of the caller (must contain valid kernel info)
 * @pgn   : Target Page Number to map
 * @fpn   : Frame Physical Number to assign to the PTE
 *
 * Uses the allocating helper to proactively build the 5-level kernel page table
 * if it's missing, then sets the PRESENT bit and clears the SWAPPED bit.
 */
int k_pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
	if (caller == NULL || caller->krnl == NULL)
		return -1;
	addr_t *pte = alloc_pte_ptr(caller->krnl->krnl_pgd, pgn);
	if (!pte)
		return -1;

	SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
	CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
	SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

	return 0;
}

/*
 * k_pte_get_entry - Retrieve the raw Kernel PTE value
 * @caller: Pointer to the PCB of the caller
 * @pgn   : Target Page Number to locate
 *
 * Performs a read-only walk of the kernel's 5-level page table.
 * Returns the raw 32-bit PTE value if found, or 0 if unmapped.
 */
uint32_t k_pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
	if (caller == NULL || caller->krnl == NULL)
		return 0;
	addr_t *pte = get_pte_ptr(caller->krnl->krnl_pgd, pgn);
	return pte ? (uint32_t)(*pte) : 0;
}

/*
 * k_vmap_page_range - Map a contiguous range of kernel virtual pages
 * @caller : Pointer to the PCB of the caller
 * @addr   : Starting virtual address (must be page-aligned)
 * @pgnum  : Number of pages to map
 * @frames : Linked list of physical frames to map to the virtual pages
 * @ret_rg : Output parameter to store the mapped virtual region boundaries
 *
 * Iterates through the provided frames and maps them into kernel space.
 * Kernel memory is pinned, so these pages are not added to any FIFO swap list.
 */
addr_t k_vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum,
						 struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
	struct framephy_struct *fpit = frames;

	if (caller == NULL || ret_rg == NULL)
		return -1;

	ret_rg->rg_start = addr;
	ret_rg->rg_end = addr + (pgnum * PAGING64_PAGESZ);

	for (int pgit = 0; pgit < pgnum; pgit++)
	{
		if (fpit == NULL)
			break;

		addr_t pgn = PAGING64_PGN((addr + (pgit * PAGING64_PAGESZ)));

		/* Call the Kernel version of the setter */
		k_pte_set_fpn(caller, pgn, fpit->fpn);

		/* NOTICE: No enlist_pgn_node here! Kernel memory is pinned. */

		fpit = fpit->fp_next;
	}
	return 0;
}

/*
 * k_pte_set_entry - Directly overwrite a Kernel PTE with a raw value
 * @caller : Pointer to the PCB of the caller
 * @pgn    : Target Page Number
 * @pte_val: Raw 32-bit Page Table Entry value to set
 *
 * Updates an existing kernel PTE. Fails if the intermediate directories do not exist.
 */
int k_pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
	if (caller == NULL || caller->krnl == NULL)
		return -1;
	addr_t *pte = get_pte_ptr(caller->krnl->krnl_pgd, pgn);
	if (!pte)
		return -1;
	*pte = pte_val;
	return 0;
}

/*
 * k_alloc_pages_range - Allocate a sequence of contiguous physical frames for the Kernel
 * @caller    : Pointer to the PCB of the caller
 * @req_pgnum : Number of physical frames requested
 * @frm_lst   : Output pointer to the linked list of allocated frames
 *
 * Wrapper function to maintain API symmetry with the user-space alloc_pages_range.
 * It specifically uses the contiguous free frame allocator since kernel space 
 * memory often requires strict physical contiguity for hardware/DMA interactions.
 */
addr_t k_alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
	if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL)
		return -1;

	return MEMPHY_get_contiguous_freefp(caller->krnl->mram, req_pgnum, frm_lst);
}

/*
 * k_vm_map_ram - Map a kernel virtual memory area to physical RAM
 * @caller    : Pointer to the PCB of the caller
 * @astart    : Virtual memory area start address (unused directly here)
 * @aend      : Virtual memory area end address (unused directly here)
 * @mapstart  : Starting virtual address for mapping
 * @incpgnum  : Number of pages to map
 * @ret_rg    : Output parameter to store the returned region boundaries
 *
 * Allocates contiguous physical frames for the kernel and maps them securely
 * into the kernel's high-memory page tables.
 */
addr_t k_vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
	struct framephy_struct *frmlst = NULL;
	addr_t ret_alloc = k_alloc_pages_range(caller, incpgnum, &frmlst);

	if (ret_alloc != 0 || frmlst == NULL)
		return -1;

	if (k_vmap_page_range(caller, mapstart, incpgnum, frmlst, ret_rg) < 0)
	{
		/* Rollback: release all newly allocated contiguous frames */
		struct framephy_struct *fpit = frmlst;
		while (fpit != NULL)
		{
			struct framephy_struct *next = fpit->fp_next;
			MEMPHY_put_freefp(caller->krnl->mram, fpit->fpn);
			free(fpit); // Clean up the node to prevent memory leaks
			fpit = next;
		}
		return -1;
	}

	ret_rg->mode_bit = 0; // 0 indicates Kernel mode
	return 0;
}

/*
 * k_init_mm - Initialize the Memory Management structure for the Kernel
 * @mm  : Pointer to the mm_struct to initialize
 * @krnl: Pointer to the kernel structure containing the shared PGD
 *
 * Sets up the kernel's memory space, configuring its starting boundary to high memory
 * for dual-space separation. Also allocates the pool array for kernel caches.
 */
int k_init_mm(struct mm_struct *mm, struct krnl_t *krnl)
{
	// Dual-space: Kernel resides at high memory, MSB is 1 (0x8000...)
	addr_t k_start = (1ULL << 63);

	__init_mm_common(mm, k_start, krnl->krnl_pgd);

	// Allocate an array large enough for kernel cache pools
	mm->kcpooltbl = malloc(100 * sizeof(struct kcache_pool_struct));
	memset(mm->kcpooltbl, 0, 100 * sizeof(struct kcache_pool_struct));

	return 0;
}

// ----------------------- END OF KERNEL FUNCTION ------------------------------------------------- //




// ----------------------- USER FUNCTION ---------------------------------------------------------- //

/*
 * pte_set_fpn - Set PTE entry for a mapped (on-line) user page
 * @caller : Pointer to the PCB of the caller
 * @pgn    : Target Page Number to map
 * @fpn    : Frame Physical Number (FPN) to assign to the PTE
 *
 * Configures the PTE for the given page number to point to a physical frame
 * in RAM. Sets the PRESENT bit and clears the SWAPPED bit.
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
	if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL)
		return -1;

#ifdef MM64
	addr_t *pte = alloc_pte_ptr(caller->mm->pgd, pgn);
	if (!pte)
		return -1;
#else
	uint32_t *pte = (uint32_t *)&caller->mm->pgd[pgn];
#endif

	SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
	CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
	SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

	return 0;
}

/*
 * pte_get_entry - Retrieve the raw User PTE value
 * @caller : Pointer to the PCB of the caller
 * @pgn    : Target Page Number to locate
 *
 * Performs a read-only walk of the user's 5-level page table.
 * Returns the raw 32-bit PTE value if found, or 0 if unmapped.
 */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
	if (caller == NULL || caller->mm == NULL)
		return 0;
	addr_t *pte = get_pte_ptr(caller->mm->pgd, pgn);
	return pte ? (uint32_t)(*pte) : 0;
}

/*
 * pte_set_entry - Directly overwrite a User PTE with a raw value
 * @caller : Pointer to the PCB of the caller
 * @pgn    : Target Page Number
 * @pte_val: Raw 32-bit Page Table Entry value to set
 *
 * Updates an existing user PTE. Fails if the intermediate directories do not exist.
 */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
	if (caller == NULL || caller->mm == NULL)
		return -1;
	addr_t *pte = get_pte_ptr(caller->mm->pgd, pgn);
	if (!pte)
		return -1;
	*pte = pte_val;
	return 0;
}

/*
 * vmap_pgd_memset - Initialize a range of page table entries with a specific pattern
 * @caller : Pointer to the PCB of the caller
 * @addr   : Starting virtual address (must be page-aligned)
 * @pgnum  : Number of pages to initialize
 *
 * Proactively allocates page table directories for a contiguous virtual address
 * range and sets their PTEs to a dummy pattern (0xdeadbeef). Used for memory
 * reservation or tracking.
 */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
	uint32_t pattern = 0xdeadbeef;
	if (caller == NULL || caller->mm == NULL || caller->mm->pgd == NULL)
		return -1;

	for (int pgit = 0; pgit < pgnum; pgit++)
	{
		addr_t current_vaddr = addr + (pgit * PAGING64_PAGESZ);
		addr_t pgn = PAGING64_PGN(current_vaddr);

		addr_t *pte = alloc_pte_ptr(caller->mm->pgd, pgn);
		if (pte)
			*pte = pattern;
	}
	return 0;
}

/*
 * vmap_page_range - Map a range of pages to physical frames
 * @caller : Pointer to the PCB of the caller
 * @addr   : Starting virtual address (must be page-aligned)
 * @pgnum  : Number of pages to map
 * @frames : Linked list of physical frames to be mapped
 * @ret_rg : Output parameter to store the mapped virtual region boundaries
 *
 * Iterates through the provided frames and maps them into user virtual memory space.
 * Enlists mapped pages into the process's FIFO tracking list for swapping.
 */
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum, 
					   struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
	struct framephy_struct *fpit = frames;
	int pgit = 0;

	// 1. Update the virtual region boundaries
	ret_rg->rg_start = addr;
	ret_rg->rg_end = addr + (pgnum * PAGING64_PAGESZ);

	// 2. Map each page in the range
	for (pgit = 0; pgit < pgnum; pgit++)
	{
		if (fpit == NULL)
			break; // not enough frames

		// Calculate the virtual address for this specific page
		addr_t current_vaddr = addr + (pgit * PAGING64_PAGESZ);

		// Convert the virtual address into a Page Number (PGN)
		addr_t pgn = PAGING64_PGN(current_vaddr);

		// 3. Build the PTE
		pte_set_fpn(caller, pgn, fpit->fpn);
		enlist_pgn_node(&caller->mm->fifo_pgn, pgn);

		fpit = fpit->fp_next;
	}

	return 0;
}

/*
 * vm_map_ram - Map a virtual memory area to physical RAM
 * @caller    : Pointer to the PCB of the caller
 * @astart    : Virtual memory area start address (unused directly in current map range)
 * @aend      : Virtual memory area end address (unused directly in current map range)
 * @mapstart  : Starting virtual address for mapping
 * @incpgnum  : Number of pages to map
 * @ret_rg    : Output parameter to store the returned region boundaries
 *
 * Allocates physical frames for the requested number of pages and maps them
 * into the user virtual memory space at the given start mapping point.
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
	/*@bksysnet: author provides a feasible solution of getting frames
	 *FATAL logic in here, wrong behaviour if we have not enough page
	 *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
	 *Don't try to perform that case in this simple work, it will result
	 *in endless procedure of swap-off to get frame and we have not provide
	 *duplicate control mechanism, keep it simple
	 */
	struct framephy_struct *frm_lst = NULL;
	addr_t ret_alloc = 0;
	int pgnum = incpgnum;

	ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

	if (ret_alloc < 0 && ret_alloc != -3000)
		return -1;

	/* Out of memory */
	if (ret_alloc == -3000)
	{
		return -1;
	}

	/* it leaves the case of memory is enough but half in ram, half in swap
	 * do the swaping all to swapper to get the all in ram */
	vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
	return 0;
}

/*
 * init_mm - Initialize the Memory Management structure for a User Process
 * @mm     : Pointer to the mm_struct to initialize
 * @caller : Pointer to the PCB of the owning process
 *
 * Sets up the process's memory space, configuring its starting boundary to 0
 * and allocating a completely empty 5-level user Page Global Directory (PGD).
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
	addr_t *pgd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
	memset(pgd, 0, PAGING64_MAX_PGN * sizeof(addr_t));

	__init_mm_common(mm, 0, pgd);
	mm->kcpooltbl = NULL;

	return 0;
}

/*
 * print_pgtbl - Print the page directory pointers for a given address
 * @caller : Pointer to the PCB of the caller
 * @start  : Target virtual address to examine
 * @end    : End virtual address (currently unused in body)
 *
 * Walks the 5-level page table structure for the given starting virtual address
 * and prints the raw pointer values for each directory level (PGD, P4D, PUD, PMD).
 */
int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
	// addr_t pgn_start;//, pgn_end;
	// addr_t pgit;
	// struct krnl_t *krnl = caller->krnl;

	addr_t pgd = 0;
	addr_t p4d = 0;
	addr_t pud = 0;
	addr_t pmd = 0;
	addr_t pt = 0;

	get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

	/* TODO traverse the page map and dump the page directory entries */
	printf("print_pgtbl:\n");

	addr_t *pgd_root = caller->mm->pgd;
	if (pgd_root == NULL)
	{
		return 0;
	}

	addr_t pdg_val = pgd_root[pgd];
	addr_t p4g_val = 0;
	addr_t pud_val = 0;
	addr_t pmd_val = 0;

	if (pdg_val != 0)
	{
		p4g_val = ((addr_t *)pdg_val)[p4d];
	}
	if (p4g_val != 0)
	{
		pud_val = ((addr_t *)p4g_val)[pud];
	}
	if (pud_val != 0)
	{
		pmd_val = ((addr_t *)pud_val)[pmd];
	}

	printf("PGD=%lx P4D=%lx PUD=%lx PMD=%lx\n", pdg_val, p4g_val, pud_val, pmd_val);
	return 0;
}

// ----------------------- END OF USER FUNCTION --------------------------------------------------- //

#endif // def MM64
