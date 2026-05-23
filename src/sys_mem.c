/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

/*
 * find_proc_by_pid - Find process PCB from kernel process lists
 * @krnl : kernel structure
 * @pid  : target process id
 *
 * System calls receive only PID from user side. Kernel must resolve
 * the real PCB by traversing its managed process lists.
 */
static struct pcb_t *find_proc_by_pid(struct krnl_t *krnl, uint32_t pid)
{
	int i;

	if (krnl == NULL)
		return NULL;

	if (krnl->running_list != NULL) {
		for (i = 0; i < krnl->running_list->size; i++) {
			if (krnl->running_list->proc[i] != NULL &&
			    krnl->running_list->proc[i]->pid == pid)
				return krnl->running_list->proc[i];
		}
	}

	if (krnl->ready_queue != NULL) {
		for (i = 0; i < krnl->ready_queue->size; i++) {
			if (krnl->ready_queue->proc[i] != NULL &&
			    krnl->ready_queue->proc[i]->pid == pid)
				return krnl->ready_queue->proc[i];
		}
	}

	return NULL;
}

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop;
	BYTE value;
	struct pcb_t *caller;

	if (regs == NULL)
		return -1;

	memop = regs->a1;
	caller = find_proc_by_pid(krnl, pid);

	if (caller == NULL)
		return -1;

	/*
	 * Keep legacy aliases for old code paths.
	 * Main memory routines should prefer caller->mm and caller->mram.
	 */
	if (caller->mm == NULL)
		caller->mm = krnl->mm;

	if (caller->mram == NULL)
		caller->mram = krnl->mram;

	if (caller->mswp == NULL)
		caller->mswp = krnl->mswp;

	if (caller->active_mswp == NULL)
		caller->active_mswp = krnl->active_mswp;

	switch (memop) {
	case SYSMEM_MAP_OP:
		return vmap_pgd_memset(caller, regs->a2, regs->a3);

	case SYSMEM_INC_OP:
		return inc_vma_limit(caller, regs->a2, regs->a3);

	case SYSMEM_SWP_OP:
		return __mm_swap_page(caller, regs->a2, regs->a3);

	case SYSMEM_IO_READ:
		MEMPHY_read(caller->mram, regs->a2, &value);
		regs->a3 = value;
		return 0;

	case SYSMEM_IO_WRITE:
		MEMPHY_write(caller->mram, regs->a2, regs->a3);
		return 0;

	default:
		printf("Memop code: %d\n", memop);
		break;
	}

	return -1;
}


