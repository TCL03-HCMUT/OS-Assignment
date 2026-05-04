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
#endif

static struct pcb_t *find_proc_in_queue(struct queue_t *q, uint32_t pid)
{
    int i;
    if (q == NULL)
    {
        return NULL;
    }
    for (i = 0; i < q->size; i++)
    {
        if (q->proc[i] != NULL && q->proc[i]->pid == pid)
        {
            return q->proc[i];
        }
    }
    return NULL;
}

int queue_empty(void) {
	int is_empty;

	pthread_mutex_lock(&queue_lock);
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++) {
		if(!empty(&mlq_ready_queue[prio])) {
			pthread_mutex_unlock(&queue_lock);
			return -1;
		}
	}
#endif
	is_empty = (empty(&ready_queue) && empty(&run_queue));
	pthread_mutex_unlock(&queue_lock);

	return is_empty;
}

struct pcb_t *sched_find_proc_by_pid(struct krnl_t *krnl, uint32_t pid)
{
    struct pcb_t *proc = NULL;
    if (krnl == NULL)
    {
        return NULL;
    }

	pthread_mutex_lock(&queue_lock);

	proc = find_proc_in_queue(krnl->running_list, pid);
	if (proc != NULL) 
	{
		pthread_mutex_unlock(&queue_lock);
		return proc;
	}

    #if defined(MLQ_SCHED)
		if (krnl->mlq_ready_queue != NULL) {
			for (int i = 0; i < MAX_PRIO; i++) {
				proc = find_proc_in_queue(&krnl->mlq_ready_queue[i], pid);
				if (proc != NULL)
				{
					pthread_mutex_unlock(&queue_lock);
					return proc;
				}
			}
		}
	#else
		return find_proc_in_queue(krnl->ready_queue, pid);
	#endif
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; 
	}
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

	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from PRIORITY [ready_queue].
	 *      It worth to protect by a mechanism.
	 * */
	// attempt = 2 is quite buggy when num CPU >= num processes - 2
	// consider changing it to 1
	for (int attempt = 0; attempt < 1; attempt++) {
		int has_process = 0;
		for (int i = 0; i < MAX_PRIO; i++) {
			if (!empty(&mlq_ready_queue[i])) {
				has_process = 1;
				if (slot[i] > 0) {
					proc = dequeue(&mlq_ready_queue[i]);
					slot[i]--; // consume one slot
					break;
				}
			}
		}
		// no process in the mlq
		if (proc != NULL || !has_process) {
			break;
		}
		// there are waiting processes but all slots are exhausted
		// reset slot allocation
		for (int i = 0; i < MAX_PRIO; i++) {
			slot[i] = MAX_PRIO - i;
		}
	}
	
	if (proc != NULL) {
		enqueue(&running_list, proc);
	}
	pthread_mutex_unlock(&queue_lock);
	return proc;	
}

void put_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;
	
	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	
	purgequeue(&running_list, proc);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list
	 *       It worth to protect by a mechanism.
	 * 
	 */
       
	
	enqueue(&mlq_ready_queue[proc->prio], proc);
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
	 */
	if (!empty(&ready_queue)) {
		proc = dequeue(&ready_queue);
	}
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
