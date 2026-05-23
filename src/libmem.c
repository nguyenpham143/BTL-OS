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
static addr_t kcache_next[PAGING_MAX_SYMTBL_SZ];

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory management instance
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *cur;
	struct vm_rg_struct *prev = NULL;

	if (mm == NULL || mm->mmap == NULL || rg_elmt == NULL)
		return -1;

	if (rg_elmt->rg_start >= rg_elmt->rg_end)
		return -1;

	cur = mm->mmap->vm_freerg_list;

	while (cur != NULL && cur->rg_start < rg_elmt->rg_start) {
		prev = cur;
		cur = cur->rg_next;
	}

	rg_elmt->rg_next = cur;

	if (prev == NULL)
		mm->mmap->vm_freerg_list = rg_elmt;
	else
		prev->rg_next = rg_elmt;

	/* Merge with next region if they are adjacent. */
	if (rg_elmt->rg_next != NULL &&
	    rg_elmt->rg_end == rg_elmt->rg_next->rg_start) {
		struct vm_rg_struct *next = rg_elmt->rg_next;

		rg_elmt->rg_end = next->rg_end;
		rg_elmt->rg_next = next->rg_next;
		free(next);
	}

	/* Merge with previous region if they are adjacent. */
	if (prev != NULL && prev->rg_end == rg_elmt->rg_start) {
		prev->rg_end = rg_elmt->rg_end;
		prev->rg_next = rg_elmt->rg_next;
		free(rg_elmt);
	}

	return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory management instance
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (mm == NULL)
		return NULL;

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
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  struct mm_struct *mm;
	struct vm_rg_struct rgnode;
	struct vm_area_struct *cur_vma;
	struct sc_regs regs;
  memset(&regs, 0, sizeof(regs));
	addr_t old_sbrk;

	if (caller == NULL || alloc_addr == NULL || size == 0)
		return -1;

	if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
		return -1;

	pthread_mutex_lock(&mmvm_lock);

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	cur_vma = get_vma_by_num(mm, vmaid);
	if (cur_vma == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	/*
	 * Fast path: reuse a freed region if possible.
	 */
	if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
		mm->symrgtbl[rgid].vmaid = vmaid;
		mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
		mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
		mm->symrgtbl[rgid].rg_next = NULL;

		*alloc_addr = rgnode.rg_start;

		pthread_mutex_unlock(&mmvm_lock);
		return 0;
	}

	/*
	 * Slow path: grow VMA from current sbrk.
	 */
	old_sbrk = cur_vma->sbrk;

	regs.a1 = SYSMEM_INC_OP;
	regs.a2 = vmaid;
	regs.a3 = size;

	if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	mm->symrgtbl[rgid].vmaid = vmaid;
	mm->symrgtbl[rgid].rg_start = old_sbrk;
	mm->symrgtbl[rgid].rg_end = old_sbrk + size;
	mm->symrgtbl[rgid].rg_next = NULL;

	*alloc_addr = old_sbrk;

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
  struct mm_struct *mm;
	struct vm_rg_struct *rgnode;
	struct vm_rg_struct *freerg_node;

	if (caller == NULL)
		return -1;

	if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
		return -1;

	pthread_mutex_lock(&mmvm_lock);

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	rgnode = get_symrg_byid(mm, rgid);
	if (rgnode == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	if (rgnode->rg_start == 0 && rgnode->rg_end == 0) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	freerg_node = malloc(sizeof(struct vm_rg_struct));
	if (freerg_node == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	freerg_node->vmaid = vmaid;
	freerg_node->rg_start = rgnode->rg_start;
	freerg_node->rg_end = rgnode->rg_end;
	freerg_node->rg_next = NULL;

	rgnode->vmaid = 0;
	rgnode->rg_start = 0;
	rgnode->rg_end = 0;
	rgnode->rg_next = NULL;

	if (enlist_vm_freerg_list(mm, freerg_node) != 0) {
		free(freerg_node);
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

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
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
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
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return val;
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
  uint32_t pte;
	uint32_t vicpte;
	addr_t freefpn;
	addr_t vicpgn;
	addr_t vicfpn;
	addr_t swpfpn;
	int swptyp;
	struct memphy_struct *mram;
	struct memphy_struct *mswp;
	struct sc_regs regs;
  memset(&regs, 0, sizeof(regs));

	if (mm == NULL || fpn == NULL || caller == NULL)
		return -1;

	mram = caller->mram;
	if (mram == NULL && caller->krnl != NULL)
		mram = caller->krnl->mram;

	mswp = caller->active_mswp;
	if (mswp == NULL && caller->krnl != NULL)
		mswp = caller->krnl->active_mswp;

	if (mram == NULL || mswp == NULL)
		return -1;

	pte = pte_get_entry(caller, pgn);

	if (PAGING_PAGE_PRESENT(pte)) {
		*fpn = PAGING_PTE_FPN(pte);
		return 0;
	}

	if (!(pte & PAGING_PTE_SWAPPED_MASK))
		return -1;

	swptyp = GETVAL(pte, PAGING_PTE_SWPTYP_MASK,
			PAGING_PTE_SWPTYP_LOBIT);
	swpfpn = PAGING_PTE_SWP(pte);

	(void)swptyp;

	/*
	 * Case 1: RAM still has a free frame.
	 * Swap the target page from SWAP into this free RAM frame.
	 */
	if (MEMPHY_get_freefp(mram, &freefpn) == 0) {
		regs.a1 = SYSMEM_SWP_OP;
		regs.a2 = freefpn;
		regs.a3 = swpfpn;

		if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0) {
			MEMPHY_put_freefp(mram, freefpn);
			return -1;
		}

		MEMPHY_put_freefp(mswp, swpfpn);

		pte_set_fpn(caller, pgn, freefpn);
		enlist_pgn_node(&mm->fifo_pgn, pgn);

		*fpn = freefpn;
		return 0;
	}

	/*
	 * Case 2: RAM is full. Select another victim page.
	 * Swap victim RAM frame with the target SWAP frame.
	 */
	if (find_victim_page(mm, &vicpgn) != 0)
		return -1;

	vicpte = pte_get_entry(caller, vicpgn);
	if (!PAGING_PAGE_PRESENT(vicpte))
		return -1;

	vicfpn = PAGING_PTE_FPN(vicpte);

	regs.a1 = SYSMEM_SWP_OP;
	regs.a2 = vicfpn;
	regs.a3 = swpfpn;

	if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
		return -1;

	pte_set_swap(caller, vicpgn, caller->active_mswp_id, swpfpn);
	pte_set_fpn(caller, pgn, vicfpn);

	enlist_pgn_node(&mm->fifo_pgn, pgn);

	*fpn = vicfpn;
	return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
  addr_t pgn;
	addr_t off;
	addr_t phyaddr;
	int fpn;
	struct sc_regs regs;
  memset(&regs, 0, sizeof(regs));

	if (mm == NULL || data == NULL || caller == NULL)
		return -1;

#ifdef MM64
	pgn = addr >> PAGING64_ADDR_PT_SHIFT;
	off = PAGING64_ADDR_OFFST(addr);
#else
	pgn = PAGING_PGN(addr);
	off = PAGING_OFFST(addr);
#endif

	if (pg_getpage(mm, pgn, &fpn, caller) != 0)
		return -1;

#ifdef MM64
	phyaddr = ((addr_t)fpn * PAGING64_PAGESZ) + off;
#else
	phyaddr = ((addr_t)fpn * PAGING_PAGESZ) + off;
#endif

	regs.a1 = SYSMEM_IO_READ;
	regs.a2 = phyaddr;
	regs.a3 = 0;

	if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
		return -1;

	*data = (BYTE)regs.a3;

	return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
  addr_t pgn;
	addr_t off;
	addr_t phyaddr;
	int fpn;
	struct sc_regs regs;
  memset(&regs, 0, sizeof(regs));

	if (mm == NULL || caller == NULL)
		return -1;

#ifdef MM64
	pgn = addr >> PAGING64_ADDR_PT_SHIFT;
	off = PAGING64_ADDR_OFFST(addr);
#else
	pgn = PAGING_PGN(addr);
	off = PAGING_OFFST(addr);
#endif

	if (pg_getpage(mm, pgn, &fpn, caller) != 0)
		return -1;

#ifdef MM64
	phyaddr = ((addr_t)fpn * PAGING64_PAGESZ) + off;
#else
	phyaddr = ((addr_t)fpn * PAGING_PAGESZ) + off;
#endif

	regs.a1 = SYSMEM_IO_WRITE;
	regs.a2 = phyaddr;
	regs.a3 = value;

	if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
		return -1;

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
  struct mm_struct *mm;
	struct vm_rg_struct *currg;
	addr_t rgsize;

	if (caller == NULL || data == NULL)
		return -1;

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL)
		return -1;

	currg = get_symrg_byid(mm, rgid);
	if (currg == NULL)
		return -1;

	if (currg->rg_start == 0 && currg->rg_end == 0)
		return -1;

	rgsize = currg->rg_end - currg->rg_start;
	if (offset >= rgsize)
		return -1;

	return pg_getval(mm, currg->rg_start + offset, data, caller);
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  printf("%s:%d\n",__func__,__LINE__);
  uint32_t dst;
	int val;

	if (proc == NULL || destination == NULL)
		return -1;

	dst = *destination;
	if (dst >= 10)
		return -1;

	val = __read(proc, 0, source, offset, &data);
	if (val == -1)
		return -1;

	proc->regs[dst] = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
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
  struct mm_struct *mm;
	struct vm_rg_struct *currg;
	addr_t rgsize;
	int ret;

	if (caller == NULL)
		return -1;

	pthread_mutex_lock(&mmvm_lock);

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	currg = get_symrg_byid(mm, rgid);
	if (currg == NULL) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	if (currg->rg_start == 0 && currg->rg_end == 0) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	rgsize = currg->rg_end - currg->rg_start;
	if (offset >= rgsize) {
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	ret = pg_setval(mm, currg->rg_start + offset, value, caller);

	pthread_mutex_unlock(&mmvm_lock);
	return ret;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val;

	if (proc == NULL)
		return -1;

	val = __write(proc, 0, destination, offset, data);
	if (val == -1)
		return -1;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
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
int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
//addr_t  addr;
//int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */

  return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  struct krnl_t *krnl;
	struct mm_struct *kmm;
	struct vm_area_struct *kvma;
	addr_t start;
	addr_t end;
	addr_t old_end;
	addr_t new_end;
	addr_t mapaddr;
	addr_t kpgn;
	addr_t fpn;
	addr_t pte;

	(void)vmaid;

	if (caller == NULL || alloc_addr == NULL || size == 0)
		return -1;

	if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
		return -1;

	krnl = caller->krnl;
	if (krnl == NULL || krnl->mm == NULL || krnl->mram == NULL)
		return -1;

	kmm = krnl->mm;
	kvma = kmm->mmap;
	if (kvma == NULL)
		return -1;

	pthread_mutex_lock(&mmvm_lock);

#ifdef MM64
	if (kvma->sbrk < KERNEL_BASE_ADDR) {
		kvma->vm_start = KERNEL_BASE_ADDR;
		kvma->vm_end = KERNEL_BASE_ADDR;
		kvma->sbrk = KERNEL_BASE_ADDR;
	}
#endif

	start = kvma->sbrk;
	end = start + size;

#ifdef MM64
	new_end = KERNEL_BASE_ADDR +
		  PAGING64_PAGE_ALIGNSZ(end - KERNEL_BASE_ADDR);
#else
	new_end = PAGING_PAGE_ALIGNSZ(end);
#endif

	old_end = kvma->vm_end;

#ifdef MM64
	for (mapaddr = old_end; mapaddr < new_end; mapaddr += PAGING64_PAGESZ) {
		if (MEMPHY_get_freefp(krnl->mram, &fpn) != 0) {
			pthread_mutex_unlock(&mmvm_lock);
			return -1;
		}

		kpgn = (mapaddr - KERNEL_BASE_ADDR) >> PAGING64_ADDR_PT_SHIFT;
		if (kpgn >= PAGING64_MAX_PGN) {
			MEMPHY_put_freefp(krnl->mram, fpn);
			pthread_mutex_unlock(&mmvm_lock);
			return -1;
		}

		pte = 0;
		SETBIT(pte, PAGING_PTE_PRESENT_MASK);
		CLRBIT(pte, PAGING_PTE_SWAPPED_MASK);
		SETVAL(pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

		krnl->krnl_pt[kpgn] = pte;
	}
#endif

	kvma->vm_end = new_end;
	kvma->sbrk = end;

	kmm->symrgtbl[rgid].vmaid = 0;
	kmm->symrgtbl[rgid].rg_start = start;
	kmm->symrgtbl[rgid].rg_end = end;
	kmm->symrgtbl[rgid].rg_next = NULL;

	*alloc_addr = start;

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

  //struct krnl_t *krnl = caller->krnl;
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;
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
  /* TODO: provide OS level management */
  /* TODO: provide OS level management */

  //struct krnl_t *krnl = caller->krnl;
  //krnl->symrgtbl...
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;

}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  return 1;
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
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  return 0;
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
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

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
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...

   return 0;
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
  /* TODO: provide OS level management user memory access */
  //krnl->pgd ...

  return 0;
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

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
  }

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
  struct pgn_t *pg;
	struct pgn_t *prev = NULL;

	if (mm == NULL || retpgn == NULL)
		return -1;

	pg = mm->fifo_pgn;
	if (pg == NULL)
		return -1;

	while (pg->pg_next != NULL) {
		prev = pg;
		pg = pg->pg_next;
	}

	*retpgn = pg->pgn;

	if (prev == NULL)
		mm->fifo_pgn = NULL;
	else
		prev->pg_next = NULL;

	free(pg);

	return 0;
}

/*
 * get_free_vmrg_area - Get a free VM region from VMA free list
 * @caller : process calling memory operation
 * @vmaid  : target VMA ID
 * @size   : requested size
 * @newrg  : returned free region
 *
 * Find the first free region that can contain @size bytes.
 * The chosen free region is removed or shrunk from the free list.
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct mm_struct *mm;
	struct vm_area_struct *cur_vma;
	struct vm_rg_struct *cur;
	struct vm_rg_struct *prev = NULL;

	if (caller == NULL || newrg == NULL || size <= 0)
		return -1;

	mm = caller->mm;
	if (mm == NULL && caller->krnl != NULL)
		mm = caller->krnl->mm;

	if (mm == NULL)
		return -1;

	cur_vma = get_vma_by_num(mm, vmaid);
	if (cur_vma == NULL)
		return -1;

	cur = cur_vma->vm_freerg_list;

	while (cur != NULL) {
		if (cur->rg_start + size <= cur->rg_end) {
			newrg->vmaid = vmaid;
			newrg->rg_start = cur->rg_start;
			newrg->rg_end = cur->rg_start + size;
			newrg->rg_next = NULL;

			/*
			 * If the free node still has remaining bytes,
			 * shrink it from the left side.
			 */
			if (newrg->rg_end < cur->rg_end) {
				cur->rg_start = newrg->rg_end;
			} else {
				/*
				 * The free node is fully used, remove it
				 * from the free list.
				 */
				if (prev == NULL)
					cur_vma->vm_freerg_list = cur->rg_next;
				else
					prev->rg_next = cur->rg_next;

				free(cur);
			}

			return 0;
		}

		prev = cur;
		cur = cur->rg_next;
	}

	return -1;
}

// #endif
