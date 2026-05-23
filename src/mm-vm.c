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
#ifdef MM64
#include "mm64.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

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

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: raw allocated size
 *@alignedsz: page-aligned size
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct mm_struct *mm;
	struct vm_area_struct *cur_vma;
	struct vm_rg_struct *newrg;

	if (caller == NULL)
		return NULL;

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL)
		return NULL;

	cur_vma = get_vma_by_num(mm, vmaid);
	if (cur_vma == NULL)
		return NULL;

	newrg = malloc(sizeof(struct vm_rg_struct));
	if (newrg == NULL)
		return NULL;

	newrg->vmaid = vmaid;
	newrg->rg_start = cur_vma->sbrk;
	newrg->rg_end = cur_vma->sbrk + size;
	newrg->rg_next = NULL;

	(void)alignedsz;

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
    if (vma != cur_area && OVERLAP(cur_area->vm_start, cur_area->vm_end, vma->vm_start, vma->vm_end))
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
  struct mm_struct *mm;
	struct vm_area_struct *cur_vma;
	struct vm_rg_struct mapped_rg;
	addr_t old_sbrk;
	addr_t new_sbrk;
	addr_t old_end;
	addr_t new_end;
	addr_t inc_amt;
	int incnumpage;

	if (caller == NULL || inc_sz == 0)
		return -1;

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL)
		return -1;

	cur_vma = get_vma_by_num(mm, vmaid);
	if (cur_vma == NULL)
		return -1;

	old_sbrk = cur_vma->sbrk;
	old_end = cur_vma->vm_end;
	new_sbrk = old_sbrk + inc_sz;

	/*
	 * If the current VMA still has enough reserved space,
	 * only move sbrk. No new physical frame is needed.
	 */
	if (new_sbrk <= old_end) {
		cur_vma->sbrk = new_sbrk;
		return 0;
	}

#ifdef MM64
	new_end = PAGING64_PAGE_ALIGNSZ(new_sbrk);
	inc_amt = new_end - old_end;
	incnumpage = inc_amt / PAGING64_PAGESZ;
#else
	new_end = PAGING_PAGE_ALIGNSZ(new_sbrk);
	inc_amt = new_end - old_end;
	incnumpage = inc_amt / PAGING_PAGESZ;
#endif

	if (incnumpage <= 0)
		return -1;

	if (validate_overlap_vm_area(caller, vmaid, old_end, new_end) < 0)
		return -1;

	if (vm_map_range(caller, cur_vma->vm_start, new_end,
		               old_end, incnumpage, &mapped_rg) < 0)
		return -1;

	cur_vma->vm_end = new_end;
	cur_vma->sbrk = new_sbrk;

	return 0;
}

// #endif
