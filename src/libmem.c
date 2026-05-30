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
#ifdef MM64
#include "mm64.h"
#endif
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
 *
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
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) != 0)
  {
    struct sc_regs regs;
    regs.a1 = SYSMEM_INC_OP;
    regs.a2 = vmaid;
    regs.a3 = size;

    if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

    if (get_free_vmrg_area(caller, vmaid, size, &rgnode) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }

  /* Successful increase limit */
  caller->mm->symrgtbl[rgid].vmaid = vmaid;
  caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
  caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
  caller->mm->symrgtbl[rgid].rg_next = NULL;
  
  if (rgnode.rg_end > cur_vma->sbrk)
    cur_vma->sbrk = rgnode.rg_end;

  *alloc_addr = rgnode.rg_start;

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

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start >= rgnode->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->vmaid = vmaid;
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

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
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
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

  /* By default using vmaid = 0 */
  return 0;
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
    if (reg_index >= PAGING_MAX_SYMTBL_SZ)
      return -1;

    struct vm_rg_struct *krg = get_symrg_byid(proc->krnl->mm, reg_index);

    if (krg == NULL || krg->rg_start < KERNEL_BASE_ADDR ||
        krg->rg_start >= krg->rg_end)
      return -1;

    krg->rg_start = 0;
    krg->rg_end = 0;
    krg->rg_next = NULL;

    return 0;
  }
  printf("%s:%d\n",__func__,__LINE__);

#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return 0;
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
  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn;
    addr_t vicfpn;
    addr_t swpfpn;
    uint32_t vicpte;

    /* TODO Initialize the target frame storing our variable */
    if ((pte & PAGING_PTE_SWAPPED_MASK) == 0)
      return -1;
    
    // Target page is in swap, tgtfpn is the swap frame number of target page
    addr_t tgtfpn = PAGING_PTE_SWP(pte);

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(caller->mm, &vicpgn) == -1)
      return -1;

    vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_PTE_FPN(vicpte);

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      return -1;

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;

    /* TODO copy victim frame to swap 
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */
    if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0)
      return -1;

    /*
     * Copy target page from swap back to the victim RAM frame.
     * Now vicfpn becomes the physical frame of pgn.
     */
    __swap_cp_page(caller->krnl->active_mswp, tgtfpn, caller->krnl->mram, vicfpn);
    
    MEMPHY_put_freefp(caller->krnl->active_mswp, tgtfpn);

    /* Update page table */
    pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

    /* Update its online status of the target page */
    pte_set_fpn(caller, pgn, vicfpn);

    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

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
  int pgn = ((addr_t)addr) / PAGING64_PAGESZ;
  int off = ((addr_t)addr) % PAGING64_PAGESZ;
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = fpn * PAGING64_PAGESZ + off;

  /* TODO 
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0)
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
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = ((addr_t)addr) / PAGING64_PAGESZ;
  int off = ((addr_t)addr) % PAGING64_PAGESZ;
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = fpn * PAGING64_PAGESZ + off;

  /* TODO 
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE 
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) < 0)
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
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || cur_vma == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (currg->rg_start >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_getval(caller->mm, currg->rg_start + offset, data, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);
  if (val == -1)
  {
    return -1;
  }
  printf("%s:%d\n",__func__,__LINE__);

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return 0;
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

  if (currg->rg_start >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_setval(caller->mm, currg->rg_start + offset, value, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

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
  int val = __write(proc, 0, destination, offset, data);
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

  return 0;
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
  addr_t addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);
  if (val == -1)
    return -1;
  printf("%s:%d",__func__,__LINE__);

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
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */
  struct krnl_t *krnl = caller->krnl;
  struct framephy_struct *fp = krnl->mram->free_fp_list;
  struct framephy_struct *prev = NULL;
  struct framephy_struct *seq_prev = NULL;
  struct framephy_struct *seq_head = NULL;
  struct framephy_struct *seq_tail = NULL;
  struct framephy_struct *after_tail = NULL;
  struct framephy_struct *tmp;

  addr_t reqsz = PAGING64_PAGE_ALIGNSZ(size);
  int reqpg = reqsz / PAGING64_PAGESZ;
  int count = 0;

  addr_t first_fpn;
  addr_t kva;

  pthread_mutex_lock(&mmvm_lock);

  // Tim reqpg frame lien tiep trong free_fp_list
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || size == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  while (fp != NULL)
  {
    if (count == 0)
    {
      seq_prev = prev;
      seq_head = fp;
      count = 1;
    }
    else if (fp->fpn == prev->fpn + 1)
    {
      count++;
    }
    else 
    {
      seq_prev = prev;
      seq_head = fp;
      count = 1;
    }

    if (count == reqpg)
    {
      seq_tail = fp;
      break;
    }

    prev = fp;
    fp = fp->fp_next;
  }

  if (seq_tail == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  // Cat doan frame lien tiep ra khoi free list
  after_tail = seq_tail->fp_next;
  first_fpn = seq_head->fpn;

  if (seq_prev == NULL)
    krnl->mram->free_fp_list = after_tail;
  else
    seq_prev->fp_next = after_tail;

  fp = seq_head;
  while (fp != after_tail)
  {
    tmp = fp;
    fp = fp->fp_next;
    free(tmp);
  }

  // kva theo direct-map
  kva = KERNEL_BASE_ADDR + first_fpn * PAGING64_PAGESZ;

  // Map KVA -> FPN vao krnl_pgd
  vm_map_kernel(caller, kva, kva + size, kva, reqpg, &krnl->mm->symrgtbl[rgid]);

  *alloc_addr = kva;

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
  struct krnl_t *krnl = caller->krnl;
  struct framephy_struct *fp = krnl->mram->free_fp_list;
  struct framephy_struct *prev = NULL;
  struct framephy_struct *seq_prev = NULL;
  struct framephy_struct *seq_head = NULL;
  struct framephy_struct *seq_tail = NULL;
  struct framephy_struct *after_tail = NULL;
  struct framephy_struct *tmp;

  struct vm_rg_struct newrg;

  addr_t reqsz = PAGING64_PAGE_ALIGNSZ(size);
  int reqpg = reqsz / PAGING64_PAGESZ;
  int count = 0;

  addr_t first_fpn;
  addr_t kva;

  pthread_mutex_lock(&mmvm_lock);

  if (cache_pool_id >= PAGING_MAX_SYMTBL_SZ || size == 0 ||
      align == 0 || size < align)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  // Tim reqpg frame lien tiep trong free_fp_list
  while (fp != NULL)
  {
    if (count == 0)
    {
      seq_prev = prev;
      seq_head = fp;
      count = 1;
    }
    else if (fp->fpn == prev->fpn + 1)
    {
      count++;
    }
    else
    {
      seq_prev = prev;
      seq_head = fp;
      count = 1;
    }

    if (count == reqpg)
    {
      seq_tail = fp;
      break;
    }

    prev = fp;
    fp = fp->fp_next;
  }

  if (seq_tail == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  // cCat doan frame lien tiep ra khoi free list
  after_tail = seq_tail->fp_next;
  first_fpn = seq_head->fpn;

  if (seq_prev == NULL)
    krnl->mram->free_fp_list = after_tail;
  else
    seq_prev->fp_next = after_tail;

  fp = seq_head;

  while (fp != after_tail)
  {
    tmp = fp;
    fp = fp->fp_next;
    free(tmp);
  }

  // KVA theo direct-map
  kva = KERNEL_BASE_ADDR + first_fpn * PAGING64_PAGESZ;

  // Map KVA -> FPN vao krnl_pgd
  vm_map_kernel(caller, kva, kva + size, kva, reqpg, &newrg);

  krnl->mm->kcpooltbl[cache_pool_id].size = size / align;
  krnl->mm->kcpooltbl[cache_pool_id].align = align;
  krnl->mm->kcpooltbl[cache_pool_id].storage = kva;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t reg_index, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr;
  int val = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);
  if (val == -1)
    return -1;
  printf("%s:%d",__func__,__LINE__);

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
  struct kcache_pool_struct *pool;
  addr_t addr;

  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pool = &caller->krnl->mm->kcpooltbl[cache_pool_id];

  if (pool->size <= 0 || pool->storage == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr = pool->storage;

  pool->storage += pool->align;
  pool->size--;

  caller->krnl->mm->symrgtbl[rgid].vmaid = vmaid;
  caller->krnl->mm->symrgtbl[rgid].rg_start = addr;
  caller->krnl->mm->symrgtbl[rgid].rg_end = addr + pool->align;
  caller->krnl->mm->symrgtbl[rgid].rg_next = NULL;

  *alloc_addr = addr;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  BYTE data;
  
  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, 0, source, offset + i, &data) != 0)
      return -1;

    if (__write_kernel_mem(caller, -1, destination, offset + i, data) != 0)
      return -1;
  }

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  BYTE data;

  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, -1, source, offset + i, &data) != 0)
      return -1;

    if (__write_user_mem(caller, 0, destination, offset + i, data) != 0)
      return -1;
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
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  if (currg == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t kva;
  addr_t off;
  addr_t phyaddr;

  addr_t *pgd_tbl, *p4d_tbl, *pud_tbl, *pmd_tbl, *pt_tbl, *pte;
  addr_t pgd, p4d, pud, pmd, pt = 0;

  if (currg->rg_start < KERNEL_BASE_ADDR ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  kva = currg->rg_start + offset;
  off = kva % PAGING64_PAGESZ;

  get_pd_from_address(kva, &pgd, &p4d, &pud, &pmd, &pt);

  pgd_tbl = caller->krnl->krnl_pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl  = (addr_t*)pmd_tbl[pmd];
  pte = &pt_tbl[pt];

  phyaddr = PAGING_PTE_FPN(*pte) * PAGING64_PAGESZ + off;

  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  pthread_mutex_unlock(&mmvm_lock);
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
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
  if (currg == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t kva;
  addr_t off;
  addr_t phyaddr;

  addr_t *pgd_tbl, *p4d_tbl, *pud_tbl, *pmd_tbl, *pt_tbl, *pte;
  addr_t pgd, p4d, pud, pmd, pt = 0;

  if (currg->rg_start < KERNEL_BASE_ADDR ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  kva = currg->rg_start + offset;
  off = kva % PAGING64_PAGESZ;

  get_pd_from_address(kva, &pgd, &p4d, &pud, &pmd, &pt);

  pgd_tbl = caller->krnl->krnl_pgd;
  p4d_tbl = (addr_t*)pgd_tbl[pgd];
  pud_tbl = (addr_t*)p4d_tbl[p4d];
  pmd_tbl = (addr_t*)pud_tbl[pud];
  pt_tbl  = (addr_t*)pmd_tbl[pmd];
  pte = &pt_tbl[pt];

  phyaddr = PAGING_PTE_FPN(*pte) * PAGING64_PAGESZ + off;

  MEMPHY_write(caller->krnl->mram, phyaddr, value);

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
  return __read(caller, vmaid, rgid, offset, data);
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
  return __write(caller, vmaid, rgid, offset, value);
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

  for (pagenum = 0; pagenum < PAGING64_MAX_PGN; pagenum++)
  {
    pte = pte_get_entry(caller, pagenum);

    if (pte == 0)
      continue;

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
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
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
  
  if (!prev)
    mm->fifo_pgn = NULL;
  else
    prev->pg_next = NULL;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

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
        {                                /*End of free list */
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
