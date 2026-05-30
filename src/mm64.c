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
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

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
	/* Extract page directories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page directories mapping */

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
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct mm_struct *mm = caller->mm;

  addr_t *pgd_tbl;
  addr_t *p4d_tbl;
  addr_t *pud_tbl;
  addr_t *pmd_tbl;
  addr_t *pt_tbl;
  addr_t *pte;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  pgd_tbl = mm->pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl = (addr_t*)pmd_tbl[pmd];

  pte = &pt_tbl[pt];
#else
  pte = &krnl->mm->pgd[pgn];
#endif
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
  struct mm_struct *mm = caller->mm;

  addr_t *pgd_tbl;
  addr_t *p4d_tbl;
  addr_t *pud_tbl;
  addr_t *pmd_tbl;
  addr_t *pt_tbl;
  addr_t *pte;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;
  
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  pgd_tbl = mm->pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl = (addr_t*)pmd_tbl[pmd];

  pte = &pt_tbl[pt];
#else
  pte = &krnl->mm->pgd[pgn];
#endif
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
  struct mm_struct *mm = caller->mm;

  addr_t *pgd_tbl;
  addr_t *p4d_tbl;
  addr_t *pud_tbl;
  addr_t *pmd_tbl;
  addr_t *pt_tbl;
  addr_t *pte;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;
	
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  
  pgd_tbl = mm->pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl = (addr_t*)pmd_tbl[pmd];
  pte = &pt_tbl[pt];
	
  return (uint32_t)(*pte);
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
	struct mm_struct *mm = caller->mm;

  addr_t *pgd_tbl;
  addr_t *p4d_tbl;
  addr_t *pud_tbl;
  addr_t *pmd_tbl;
  addr_t *pt_tbl;
  addr_t *pte;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);

  pgd_tbl = mm->pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl = (addr_t*)pmd_tbl[pmd];
  pte = &pt_tbl[pt];

  *pte = (addr_t)pte_val;
	
	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit = 0;
  addr_t pgn = addr / PAGING64_PAGESZ;
  uint64_t pattern = 0xdeadbeef;

  /* TODO memset the page table with given pattern
   */
  for (pgit = 0; pgit < pgnum; pgit++)
    pte_set_entry(caller, pgn + pgit, pattern);

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
  int pgit = 0;
  addr_t pgn;

  /* TODO: update the rg_end and rg_start of ret_rg */
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
  ret_rg->rg_start = addr;
  ret_rg->vmaid = 0;
  ret_rg->rg_next = NULL;

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */
  while (pgit < pgnum && fpit != NULL)
  {
    pgn = (addr / PAGING64_PAGESZ);

    pte_set_fpn(caller, pgn + pgit, fpit->fpn);

    /* Tracking for later page replacement activities (if needed)
    * Enqueue new usage page */
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);

    fpit = fpit->fp_next;
    pgit++;
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

  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *fp_tail = NULL;

  *frm_lst = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
    {
      if (find_victim_page(caller->mm, &vicpgn) < 0)
        return -3000; // out of memory

      vicpte = pte_get_entry(caller, vicpgn);
      vicfpn = PAGING_PTE_FPN(vicpte);

      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0)
        return -3000;

      struct sc_regs regs;
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = vicfpn;
      regs.a3 = swpfpn;

      if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0)
        return -3000;

      pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

      fpn = vicfpn;
    }

    // Zero-fill
    for (int i = 0; i < PAGING64_PAGESZ; i++)
      MEMPHY_write(caller->krnl->mram, fpn * PAGING64_PAGESZ + i, 0);

    newfp_str = malloc(sizeof(struct framephy_struct));
    newfp_str->fpn = fpn;
    newfp_str->owner = caller->mm;
    newfp_str->fp_next = NULL;

    if (*frm_lst == NULL)
    {
      *frm_lst = newfp_str;
      fp_tail = newfp_str;
    }
    else
    {
      fp_tail->fp_next = newfp_str;
      fp_tail = newfp_str;
    }
  }
  /* End TODO */

  return 0;
}

/*
 * vm_map_range - do the mapping all vm area to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_range(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
  {
    return -1;
  }

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

addr_t vm_map_kernel(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  addr_t first_fpn = (mapstart - KERNEL_BASE_ADDR) / PAGING64_PAGESZ;

  for (int i = 0; i < incpgnum; i++)
  {
    addr_t *pgd_tbl, *p4d_tbl, *pud_tbl, *pmd_tbl, *pt_tbl, *pte;
    addr_t pgd = 0;
    addr_t p4d = 0;
    addr_t pud = 0;
    addr_t pmd = 0;
    addr_t pt = 0;

    addr_t cur_kva = mapstart + i * PAGING64_PAGESZ;
    addr_t cur_fpn = first_fpn + i;

    get_pd_from_address(cur_kva, &pgd, &p4d, &pud, &pmd, &pt);

    pgd_tbl = caller->krnl->krnl_pgd;
    p4d_tbl = (addr_t*)pgd_tbl[pgd];
    pud_tbl = (addr_t*)p4d_tbl[p4d];
    pmd_tbl = (addr_t*)pud_tbl[pud];
    pt_tbl = (addr_t*)pmd_tbl[pmd];
    pte = &pt_tbl[pt];

    *pte = 0;

    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

    SETVAL(*pte, cur_fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  }

  ret_rg->vmaid = -1;
  ret_rg->rg_start = astart;
  ret_rg->rg_end = aend;
  ret_rg->rg_next = NULL;

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
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* TODO init page table directory */
  mm->pgd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  mm->p4d = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  mm->pud = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  mm->pmd = malloc(PAGING64_MAX_PGN * sizeof(addr_t));
  mm->pt = malloc(PAGING64_MAX_PGN * sizeof(addr_t));

  for (int i = 0; i < PAGING64_MAX_PGN; i++)
  {
    mm->pgd[i] = (addr_t)mm->p4d;
    mm->p4d[i] = (addr_t)mm->pud;
    mm->pud[i] = (addr_t)mm->pmd;
    mm->pmd[i] = (addr_t)mm->pt;
    mm->pt[i] = 0;
  }

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  vma0->vm_freerg_list = NULL;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* TODO update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /* TODO: update mmap */
  mm->mmap = vma0;
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = malloc(PAGING_MAX_SYMTBL_SZ * sizeof(struct kcache_pool_struct));

  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
  {
    mm->kcpooltbl[i].size = 0;
    mm->kcpooltbl[i].align = 0;
    mm->kcpooltbl[i].storage = 0;
  }

  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
  {
    mm->symrgtbl[i].vmaid = 0;
    mm->symrgtbl[i].rg_start = 0;
    mm->symrgtbl[i].rg_end = 0;
    mm->symrgtbl[i].rg_next = NULL;
  }

  if (caller)
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
  struct mm_struct *mm = caller->mm;

  addr_t pgn_start, pgn_end;
  addr_t pgit;

  addr_t *pgd_tbl;
  addr_t *p4d_tbl;
  addr_t *pud_tbl;
  addr_t *pmd_tbl;
  addr_t *pt_tbl;
  addr_t *pte;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  uint32_t pte_val;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */
  pgd_tbl = mm->pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl = (addr_t*)pmd_tbl[pmd];

  printf("print_pgtbl:\n");
  printf(" PGD=%016llx P4D=%016llx PUD=%016llx PMD=%016llx PT=%016llx\n",
        (unsigned long long int)pgd_tbl,
        (unsigned long long int)p4d_tbl,
        (unsigned long long int)pud_tbl,
        (unsigned long long int)pmd_tbl,
        (unsigned long long int)pt_tbl);

  pgn_start = start / PAGING64_PAGESZ;
  if (end == (addr_t)-1 || end == 0)
    pgn_end = PAGING64_MAX_PGN;
  else
  {
    pgn_end = (end + PAGING64_PAGESZ - 1) / PAGING64_PAGESZ;
    if (pgn_end > PAGING64_MAX_PGN)
      pgn_end = PAGING64_MAX_PGN;
  }

  for (pgit = pgn_start; pgit < pgn_end; pgit++)
  {
    get_pd_from_pagenum(pgit, &pgd, &p4d, &pud, &pmd, &pt);

    pgd_tbl = mm->pgd;
    p4d_tbl = (addr_t*)pgd_tbl[pgd];
    pud_tbl = (addr_t*)p4d_tbl[p4d];
    pmd_tbl = (addr_t*)pud_tbl[pud];
    pt_tbl = (addr_t*)pmd_tbl[pmd];

    pte = &pt_tbl[pt];
    pte_val = (uint32_t)(*pte);

    if (pte_val == 0)
      continue;

    if (pte_val & PAGING_PTE_SWAPPED_MASK)
    {
      printf("  PGN=%u PTE=0x%08x SWP=%u\n",
            (unsigned int)pgit, pte_val, (unsigned int)PAGING_SWP(pte_val));
    }
    else if (PAGING_PAGE_PRESENT(pte_val))
    {
      printf("  PGN=%u PTE=0x%08x FPN=%u\n",
            (unsigned int)pgit, pte_val, (unsigned int)PAGING_FPN(pte_val));
    }
  }

  return 0;
}

#endif  //def MM64
