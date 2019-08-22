#define THREAD 32
#define QUEUE  256

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_THREADS 64
#define MAX_QUEUE 65536

typedef struct threadpool_t threadpool_t;

typedef enum {
    threadpool_invalid        = -1,
    threadpool_lock_failure   = -2,
    threadpool_queue_full     = -3,
    threadpool_shutdown       = -4,
    threadpool_thread_failure = -5
} threadpool_error_t;

typedef enum {
    threadpool_graceful       = 1
} threadpool_destroy_flags_t;


threadpool_t *threadpool_create(int thread_count, int queue_size, int flags);


int threadpool_add(threadpool_t *pool, void (*routine)(void *, void*),
                   void *arg1, void *arg2, int flags);


int threadpool_destroy(threadpool_t *pool, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _THREADPOOL_H_ */

int tasks = 0, done = 0;
pthread_mutex_t lock;

typedef enum {
    immediate_shutdown = 1,
    graceful_shutdown  = 2
} threadpool_shutdown_t;



typedef struct {
    void (*function)(void *, void*);
    void *argument1;
    void *argument2;
} threadpool_task_t;


struct threadpool_t {
  pthread_mutex_t lock;
  pthread_cond_t notify;
  pthread_t *threads;
  threadpool_task_t *queue;
  int thread_count;
  int queue_size;
  int head;
  int tail;
  int count;
  int shutdown;
  int started;
};


static void *threadpool_thread(void *threadpool);

int threadpool_free(threadpool_t *pool);


void prime_number(void* arg1, void* arg2 ) {
	long long p = *(long long*)arg1; //
	int n = *(int*)arg2;
    int cnt = 0;
    long long i, j, k;
    for ( i = 2; i<p; i++) {
    	k = sqrt ( i+1);
    	for ( j = 2; j<=k; j++) {
    		if ( i % j  ==0) break;
		}
		if ( j == k+1) cnt++;
	}
	printf ( "The number of primes within %lld is %d.\nThe priority of this task is %d.\n", p, cnt, n);

}

int main(int argc, char **argv) //main
{
	char commandstr[100]; //This is the commandstring which we use to get the input command from user. that is "restart", "exit", and other two numbers.
	long long p; //the first number : the range till primes are to be counted. 
	int n; // the second number: the priority number of thread.
	threadpool_t *pool;  //threadpool pointer

	assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL); // first create a threadpool
	while ( 1) { // main message loop.
		scanf ("%s", commandstr); //input the command
		if ( !strcmp ( commandstr, "restart")) { // if the command is "restart"
   			assert(threadpool_destroy(pool, 0) == 0); // delete the original threadpool
			assert((pool = threadpool_create(THREAD, QUEUE, 0)) != NULL);	//create the new threadpool
    		printf("Pool started with %d threads and queue size of %d\n", THREAD, QUEUE);	// Print the threadpool state: that is THREAD is the number of threads and QUEUE is the QUEUE SIZE of THREAD POOL				
		}	
		else if (!strcmp ( commandstr, "exit")) { //if the command is "exit"
			assert(threadpool_destroy(pool, 0) == 0); // delete the threadpool and stop the program.
			break;
		}	
		else { //other case :that is in case of number1, number2: 
			sscanf (commandstr, "%lld", &p); // input p from commandstr
			scanf ("%d", &n); //input n from user
			long long *arg1 = (long long*)malloc(sizeof(long long)); //create a first argument to be entered in the mainfunction of threadpool
			*arg1 = p;
			int *arg2 = (int*)malloc(sizeof(int)); //create a second argument to be entered in the mainfunction of threadpool
			*arg2 = n; 
			threadpool_add(pool, &prime_number, (void*)arg1, (void*)arg2, 0); //add the main function to the threadpool.

		}
	}

    return 0;
}



threadpool_t *threadpool_create(int thread_count, int queue_size, int flags) //This is the function to create threadpool.
{
    threadpool_t *pool;
    int i;
    (void) flags;

    if(thread_count <= 0 || thread_count > MAX_THREADS || queue_size <= 0 || queue_size > MAX_QUEUE) {
        return NULL;
    }

    if((pool = (threadpool_t *)malloc(sizeof(threadpool_t))) == NULL) {
        goto err;
    }

    /* Initialize */
    pool->thread_count = 0;
    pool->queue_size = queue_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = pool->started = 0;

    /* Allocate thread and task queue */
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);
    pool->queue = (threadpool_task_t *)malloc
        (sizeof(threadpool_task_t) * queue_size);

    /* Initialize mutex and conditional variable first */
    if((pthread_mutex_init(&(pool->lock), NULL) != 0) ||
       (pthread_cond_init(&(pool->notify), NULL) != 0) ||
       (pool->threads == NULL) ||
       (pool->queue == NULL)) {
        goto err;
    }

    /* Start worker threads */
    for(i = 0; i < thread_count; i++) {
        if(pthread_create(&(pool->threads[i]), NULL,
                          threadpool_thread, (void*)pool) != 0) {
            threadpool_destroy(pool, 0);
            return NULL;
        }
        pool->thread_count++;
        pool->started++;
    }

    return pool;

 err:
    if(pool) {
        threadpool_free(pool);
    }
    return NULL;
}

int threadpool_add(threadpool_t *pool, void (*function)(void *, void*),
                   void *argument1,void *argument2,  int flags) //This is the function to add main function to the threadpool.
{
    int err = 0;
    int next;
    (void) flags;

    if(pool == NULL || function == NULL) {
        return threadpool_invalid;
    }
    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }

    next = (pool->tail + 1) % pool->queue_size;
    do {
        /* Are we full ? */
        if(pool->count == pool->queue_size) {
            err = threadpool_queue_full;
            break;
        }

        /* Are we shutting down ? */
        if(pool->shutdown) {
            err = threadpool_shutdown;
            break;
        }

        /* Add task to queue */
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument1 = argument1;
        pool->queue[pool->tail].argument2 = argument2;
        
        pool->tail = next;
        pool->count += 1;
		
        /* pthread_cond_broadcast */
        if(pthread_cond_signal(&(pool->notify)) != 0) {
            err = threadpool_lock_failure;
            break;
        }
    } while(0);

    if(pthread_mutex_unlock(&pool->lock) != 0) {
        err = threadpool_lock_failure;
    }

    return err;
}

int threadpool_destroy(threadpool_t *pool, int flags)  //this is the function to destroy the threadpool.
{
    int i, err = 0;

    if(pool == NULL) {
        return threadpool_invalid;
    }

    if(pthread_mutex_lock(&(pool->lock)) != 0) {
        return threadpool_lock_failure;
    }

    do {
        /* Already shutting down */
        if(pool->shutdown) {
            err = threadpool_shutdown;
            break;
        }

        pool->shutdown = (flags & threadpool_graceful) ?
            graceful_shutdown : immediate_shutdown;

        /* Wake up all worker threads */
        if((pthread_cond_broadcast(&(pool->notify)) != 0) ||
           (pthread_mutex_unlock(&(pool->lock)) != 0)) {
            err = threadpool_lock_failure;
            break;
        }

        /* Join all worker thread */
        for(i = 0; i < pool->thread_count; i++) {
            if(pthread_join(pool->threads[i], NULL) != 0) {
                err = threadpool_thread_failure;
            }
        }
    } while(0);

    /* Only if everything went well do we deallocate the pool */
    if(!err) {
        threadpool_free(pool);
    }
    return err;
}

int threadpool_free(threadpool_t *pool)
{
    if(pool == NULL || pool->started > 0) {
        return -1;
    }

    /* Did we manage to allocate ? */
    if(pool->threads) {
        free(pool->threads);
        free(pool->queue);
 
        /* Because we allocate pool->threads after initializing the
           mutex and condition variable, we're sure they're
           initialized. Let's lock the mutex just in case. */
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
    }
    free(pool);    
    return 0;
}


static void *threadpool_thread(void *threadpool)
{
    threadpool_t *pool = (threadpool_t *)threadpool;
    threadpool_task_t task;

    for(;;) {
        /* Lock must be taken to wait on conditional variable */
        pthread_mutex_lock(&(pool->lock));

        /* Wait on condition variable, check for spurious wakeups.
           When returning from pthread_cond_wait(), we own the lock. */
        while((pool->count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        if((pool->shutdown == immediate_shutdown) ||
           ((pool->shutdown == graceful_shutdown) &&
            (pool->count == 0))) {
            break;
        }

        /* Grab our task */
        task.function = pool->queue[pool->head].function;
        task.argument1 = pool->queue[pool->head].argument1;
		task.argument2 = pool->queue[pool->head].argument2;        
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count -= 1;

        /* Unlock */
        pthread_mutex_unlock(&(pool->lock));

        /* Get to work */
        (*(task.function))(task.argument1, task.argument2);
    }

    pool->started--;

    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);
    return(NULL);
}
