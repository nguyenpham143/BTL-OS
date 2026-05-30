/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];

static int in_ready[MAX_PRIO];     
static int in_active[MAX_PRIO];     
static int active_prio[MAX_PRIO];   
static int active_count;
static int total_ready;

static int normalize_prio(int prio)
{
	if (prio < 0)
		return 0;
	if (prio >= MAX_PRIO)
		return MAX_PRIO - 1;
	return prio;
}

static void remove_active_prio(int prio)
{
	int i;

	if (!in_active[prio])
		return;

	for (i = 0; i < active_count; i++) {
		if (active_prio[i] == prio)
			break;
	}

	for (; i < active_count - 1; i++)
		active_prio[i] = active_prio[i + 1];

	active_count--;
	in_active[prio] = 0;
}

static void insert_active_prio(int prio)
{
	int i;

	if (in_active[prio])
		return;

	if (!in_ready[prio] || slot[prio] <= 0)
		return;

	for (i = active_count; i > 0 && active_prio[i - 1] > prio; i--)
		active_prio[i] = active_prio[i - 1];

	active_prio[i] = prio;
	active_count++;
	in_active[prio] = 1;
}

static void update_prio_state(int prio)
{
	in_ready[prio] = !empty(&mlq_ready_queue[prio]);

	if (in_ready[prio] && slot[prio] > 0)
		insert_active_prio(prio);
	else
		remove_active_prio(prio);
}

static void reset_mlq_round(void)
{
	int i;

	active_count = 0;

	for (i = 0; i < MAX_PRIO; i++) {
		slot[i] = MAX_PRIO - i;
		in_active[i] = 0;
		update_prio_state(i);
	}
}
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	if (total_ready > 0)
		return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i;
		in_ready[i] = 0;
		in_active[i] = 0;
		active_prio[i] = 0;
	}

active_count = 0;
total_ready = 0;
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) {
	struct pcb_t * proc = NULL;
	int prio;
	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from PRIORITY [ready_queue].
	 *      It worth to protect by a mechanism.
	 * */
	if (total_ready > 0 && active_count == 0)
		reset_mlq_round();

	if (active_count > 0) {
		prio = active_prio[0];

		proc = dequeue(&mlq_ready_queue[prio]);

		if (proc != NULL) {
			total_ready--;
			slot[prio]--;
			update_prio_state(prio);
			enqueue(&running_list, proc);
		}
	}

	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_mlq_proc(struct pcb_t * proc) {
	int prio;
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */
	prio = normalize_prio(proc->prio);
	proc->prio = prio;

	pthread_mutex_lock(&queue_lock);

	purgequeue(&running_list, proc);
	enqueue(&mlq_ready_queue[prio], proc);

	total_ready++;
	update_prio_state(prio);

	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	int prio;
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 * 
	 */
	prio = normalize_prio(proc->prio);
	proc->prio = prio;

	pthread_mutex_lock(&queue_lock);

	enqueue(&mlq_ready_queue[prio], proc);

	total_ready++;
	update_prio_state(prio);

	pthread_mutex_unlock(&queue_lock);	
}

struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from [ready_queue].
	 *       It worth to protect by a mechanism.
	 * 
	 */
	if (!empty(&ready_queue))
		proc = dequeue(&ready_queue);

	if (proc != NULL)
		enqueue(&running_list, proc);

	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);	
}
#endif
