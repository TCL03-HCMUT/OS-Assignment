#include <stdio.h>
#include <stdlib.h>
#include "queue.h"
#include <pthread.h>

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

int empty(struct queue_t *q)
{
        int is_empty;

        if (q == NULL)
                return 1;

        pthread_mutex_lock(&queue_lock);
        is_empty = (q->size == 0);
        pthread_mutex_unlock(&queue_lock);

        return is_empty;
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (!q || !proc)
                return;

        pthread_mutex_lock(&queue_lock);

        if (q->size < MAX_QUEUE_SIZE)
        {
                q->proc[q->size++] = proc;
        }
        else
        {
                fprintf(stderr, "Queue is full\n");
        }

        pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (!q) return NULL;

        pthread_mutex_lock(&queue_lock);

        if (q->size == 0)
        {
                pthread_mutex_unlock(&queue_lock);
		return NULL;
        }

        struct pcb_t *result = q->proc[0];
        for (int i = 1; i < q->size; i++)
        {
                q->proc[i-1] = q->proc[i];
        }
        q->size--;

        pthread_mutex_unlock(&queue_lock);
        
        return result;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (!q || !proc) return NULL;

        pthread_mutex_lock(&queue_lock);

        if (q->size == 0) {
                pthread_mutex_unlock(&queue_lock);
                return NULL;
        }

        int index = -1;
        for (int i = 0; i < q->size; i++)
        {
                if (q->proc[i] == proc)
                {
                        index = i;
                        break;
                }
        }
        if (index < 0)
        {
                pthread_mutex_unlock(&queue_lock);
                return NULL;
        }
        
        struct pcb_t *result = q->proc[index];
        for (int i = index + 1; i < q->size; i++)
        {
                q->proc[i-1] = q->proc[i];
        }
        q->size--;

        pthread_mutex_unlock(&queue_lock);

        return result;
}
