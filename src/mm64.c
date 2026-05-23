/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}

/*
 * get_pte_ptr - Get PTE pointer from page number
 * @caller : process calling memory operation
 * @pgn    : page number
 *
 * Return the address of the PTE storing mapping information of @pgn.
 * This simple implementation keeps real PTE values in a linear PT array.
 * The 5-level page indexes are still parsed for tracing and future update.
 */
static addr_t *get_pte_ptr(struct pcb_t *caller, addr_t pgn)
{
	addr_t pgd = 0;
	addr_t p4d = 0;
	addr_t pud = 0;
	addr_t pmd = 0;
	addr_t pt = 0;

	if (caller == NULL || caller->mm == NULL)
		return NULL;

	if (pgn >= PAGING64_MAX_PGN)
		return NULL;

	get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

	/*
 	* Simplified design:
 	* We extract 5-level paging indexes for tracing/report,
 	* but keep real PTE values in a linear PT array.
 	*/
	(void)pgd;
	(void)p4d;
	(void)pud;
	(void)pmd;
	(void)pt;

	return &caller->mm->pt[pgn];
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte;

#ifdef MM64
	pte = get_pte_ptr(caller, pgn);
#else
	struct krnl_t *krnl = caller->krnl;
	pte = &krnl->mm->pgd[pgn];
#endif

	if (pte == NULL)
		return -1;

	*pte = 0;

	CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
	SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

	SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
	SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

	return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  addr_t *pte;

#ifdef MM64
	pte = get_pte_ptr(caller, pgn);
#else
	struct krnl_t *krnl = caller->krnl;
	pte = &krnl->mm->pgd[pgn];
#endif

	if (pte == NULL)
		return -1;

	*pte = 0;

	SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
	CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

	SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

	return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  uint32_t pte = 0;

#ifdef MM64
	addr_t *pteptr = get_pte_ptr(caller, pgn);

	if (pteptr != NULL)
		pte = (uint32_t)(*pteptr);
#else
	struct krnl_t *krnl = caller->krnl;
	pte = krnl->mm->pgd[pgn];
#endif

	return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
#ifdef MM64
	addr_t *pte = get_pte_ptr(caller, pgn);

	if (pte == NULL)
		return -1;

	*pte = pte_val;
#else
	struct krnl_t *krnl = caller->krnl;
	krnl->mm->pgd[pgn] = pte_val;
#endif

	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit;
	addr_t cur_addr;
	addr_t pgn;
	uint32_t dummy_pte;

	if (caller == NULL || caller->mm == NULL || pgnum <= 0)
		return -1;

	dummy_pte = 0x0BADBEEF;
	CLRBIT(dummy_pte, PAGING_PTE_PRESENT_MASK);
	CLRBIT(dummy_pte, PAGING_PTE_SWAPPED_MASK);

	for (pgit = 0; pgit < pgnum; pgit++) {
#ifdef MM64
		cur_addr = addr + pgit * PAGING64_PAGESZ;
		pgn = cur_addr >> PAGING64_ADDR_PT_SHIFT;

		if (pgn >= PAGING64_MAX_PGN)
			return -1;
#else
		cur_addr = addr + pgit * PAGING_PAGESZ;
		pgn = PAGING_PGN(cur_addr);

		if (pgn >= PAGING_MAX_PGN)
			return -1;
#endif

		if (pte_set_entry(caller, pgn, dummy_pte) != 0)
			return -1;
	}

	return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
  struct framephy_struct *fpit = frames;
	struct framephy_struct *tmp;
	addr_t pgn;
	addr_t cur_addr;
	int pgit;

	if (caller == NULL || caller->mm == NULL ||
	    frames == NULL || ret_rg == NULL || pgnum <= 0)
		return -1;

	ret_rg->vmaid = 0;
	ret_rg->rg_start = addr;
	ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
	ret_rg->rg_next = NULL;

	for (pgit = 0; pgit < pgnum; pgit++) {
		if (fpit == NULL)
			return -1;

		cur_addr = addr + pgit * PAGING64_PAGESZ;
		pgn = cur_addr >> PAGING64_ADDR_PT_SHIFT;

		if (pte_set_fpn(caller, pgn, fpit->fpn) != 0)
			return -1;

		enlist_pgn_node(&caller->mm->fifo_pgn, pgn);

		tmp = fpit;
		fpit = fpit->fp_next;
		free(tmp);
	}

	return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
	addr_t vicpgn;
	addr_t vicfpn;
	addr_t swpfpn;
	uint32_t vicpte;
	int pgit;
	struct memphy_struct *mram;
	struct memphy_struct *mswp;
	struct mm_struct *mm;
	struct framephy_struct *head = NULL;
	struct framephy_struct *tail = NULL;
	struct framephy_struct *newfp = NULL;
	struct sc_regs regs;
  	memset(&regs, 0, sizeof(regs));

	if (caller == NULL || frm_lst == NULL || req_pgnum <= 0)
		return -1;

	*frm_lst = NULL;

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	mram = caller->mram;
	if (mram == NULL && caller->krnl != NULL)
		mram = caller->krnl->mram;

	mswp = caller->active_mswp;
	if (mswp == NULL && caller->krnl != NULL)
		mswp = caller->krnl->active_mswp;

	if (mm == NULL || mram == NULL || mswp == NULL)
		return -1;

	for (pgit = 0; pgit < req_pgnum; pgit++) {
		if (MEMPHY_get_freefp(mram, &fpn) != 0) {
			/*
			 * RAM is full. Select a victim page and move it to SWAP.
			 */
			if (find_victim_page(mm, &vicpgn) != 0)
				goto failed;

			vicpte = pte_get_entry(caller, vicpgn);
			if (!PAGING_PAGE_PRESENT(vicpte))
				goto failed;

			vicfpn = PAGING_PTE_FPN(vicpte);

			if (MEMPHY_get_freefp(mswp, &swpfpn) != 0)
				goto failed;

			regs.a1 = SYSMEM_SWP_OP;
			regs.a2 = vicfpn;
			regs.a3 = swpfpn;

			if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
				MEMPHY_put_freefp(mswp, swpfpn);
				goto failed;
			}

			pte_set_swap(caller, vicpgn, caller->active_mswp_id, swpfpn);

			/*
			 * The victim RAM frame now becomes available for
			 * the new virtual page.
			 */
			fpn = vicfpn;
		}

		newfp = malloc(sizeof(struct framephy_struct));
		if (newfp == NULL) {
			MEMPHY_put_freefp(mram, fpn);
			goto failed;
		}

		newfp->fpn = fpn;
		newfp->owner = mm;
		newfp->fp_next = NULL;

		if (head == NULL)
			head = newfp;
		else
			tail->fp_next = newfp;

		tail = newfp;
	}

	*frm_lst = head;
	return 0;

failed:
	while (head != NULL) {
		newfp = head;
		head = head->fp_next;
		MEMPHY_put_freefp(mram, newfp->fpn);
		free(newfp);
	}

	*frm_lst = NULL;
	return -3000;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned mapped region
 */
addr_t vm_map_range(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
	addr_t ret_alloc = 0;

	if (caller == NULL || ret_rg == NULL || incpgnum <= 0)
		return -1;

	ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);
	if (ret_alloc != 0)
		return -1;

	if (vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg) != 0)
		return -1;

	return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  int i;
	struct vm_area_struct *vma0;

	if (mm == NULL)
		return -1;

	vma0 = malloc(sizeof(struct vm_area_struct));
	if (vma0 == NULL)
		return -1;

#ifdef MM64
	/* Init long-address paging structures */
	mm->pgd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
	mm->p4d = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
	mm->pud = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
	mm->pmd = calloc(PAGING64_MAX_PGN, sizeof(addr_t));
	mm->pt  = calloc(PAGING64_MAX_PGN, sizeof(addr_t));

	if (mm->pgd == NULL || mm->p4d == NULL || mm->pud == NULL ||
	    mm->pmd == NULL || mm->pt == NULL)
		return -1;
#else
	mm->pgd = calloc(PAGING_MAX_PGN, sizeof(uint32_t));

	if (mm->pgd == NULL)
		return -1;
#endif

	mm->mmap = NULL;
	mm->fifo_pgn = NULL;
	mm->kcpooltbl = NULL;

	for (i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
		mm->symrgtbl[i].vmaid = 0;
		mm->symrgtbl[i].rg_start = 0;
		mm->symrgtbl[i].rg_end = 0;
		mm->symrgtbl[i].rg_next = NULL;
	}

	/* By default the owner comes with at least one VMA */
	vma0->vm_id = 0;
	vma0->vm_start = 0;
	vma0->vm_end = vma0->vm_start;
	vma0->sbrk = vma0->vm_start;
	vma0->vm_mm = mm;
	vma0->vm_freerg_list = NULL;
	vma0->vm_next = NULL;

	mm->mmap = vma0;

	if (caller != NULL)
		caller->mm = mm;

	return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
//addr_t pgn_start;//, pgn_end;
//addr_t pgit;
//struct krnl_t *krnl = caller->krnl;

  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */

  return 0;
}

#endif  //def MM64
