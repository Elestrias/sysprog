#include "thread_pool.h"
#include <pthread.h>
#include "stdlib.h"
#include <unistd.h>
#include <stdatomic.h>
#include "sys/time.h"
#include  "stdio.h"

struct thread_task {
	thread_task_f function;
	void *arg;
    int joinStarted;
    pthread_mutex_t mutex;
    pthread_cond_t taskCond;

    enum{
      AWAIT = 0,
      PUSHED = 1,
      IN_PROGRESS = 2,
      FINISHED = 3,
      JOINED = 4
    } state;

    int detached;
    void* returnVal;
};

void configureThreadTask(struct thread_task * task){
    task->joinStarted = 0;
    task->state = 0;
    task->detached = 0;
    pthread_mutex_init(&task->mutex, NULL);
    pthread_cond_init(&task->taskCond, NULL);
}

struct thread_pool {
	pthread_t *threads;
    pthread_mutex_t mutex;
    pthread_cond_t poolCond;

    int count;
    int capacity;
    atomic_int busy;
    struct thread_task* queue[TPOOL_MAX_TASKS];
    int cursor;
    int readCursor;

    atomic_int task_count;
    atomic_int stopped;
};

struct worker_params {
    struct thread_pool* pool;
};

void* worker(void * v){
    struct worker_params* param = v;
    struct thread_pool* pool = param->pool;
    while(!pool->stopped) {
        pthread_mutex_lock(&pool->mutex);
        while(pool->task_count <= 0 && !pool->stopped) {
            pthread_cond_wait(&pool->poolCond, &pool->mutex);
            if(pool->stopped){
                pthread_mutex_unlock(&pool->mutex);
                free(param);
                return NULL;
            }
        }

        struct thread_task* task = pool->queue[pool->readCursor];
        pool->queue[pool->readCursor] = NULL;
        ++pool->readCursor;
        --pool->task_count;

        if(pool->readCursor >= TPOOL_MAX_TASKS){
            pool->readCursor = 0;
        }

        pthread_mutex_unlock(&pool->mutex);
        ++pool->busy;

        if(task == NULL){
            goto notBusy;
        }

        task->state = IN_PROGRESS;
        task->returnVal = task->function(task->arg);


        pthread_mutex_lock(&task->mutex);
        task->state = FINISHED;
        pthread_cond_broadcast(&task->taskCond);

        if(task->detached) {
            thread_task_delete(task);
        }

        pthread_mutex_unlock(&task->mutex);

    notBusy:
        --pool->busy;
    }
    free(param);
    return NULL;
}

void configureThreadPool(struct thread_pool * pool, int capacity){
    pool->capacity = capacity;
    pool->count = 0;
    pool->stopped = 0;
    pool->busy = 0;
    pool->cursor = 0;
    pool->readCursor = 0;
    pthread_cond_t condVar = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
    pool->mutex = mutex2;
    pool->poolCond = condVar;
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
    if(max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS || pool == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    *pool = calloc(1, sizeof(struct thread_pool));

    configureThreadPool(*pool, max_thread_count);
    (*pool)->threads = calloc(max_thread_count, sizeof (pthread_t));
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->count;
}

int
thread_pool_delete(struct thread_pool *pool)
{
    if(pool == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&pool->mutex);
    if(pool->task_count > 0  || pool->busy > 0){
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_HAS_TASKS;
    }

    pool->stopped = 1;

    pthread_mutex_unlock(&pool->mutex);

    for(int i = 0; i < pool->count; ++i){
        pthread_cond_broadcast(&pool->poolCond);
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->poolCond);

    free(pool);

	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
    if(pool == NULL || task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&pool->mutex);
    if(pool->task_count + pool->busy >= TPOOL_MAX_TASKS){
        pthread_mutex_unlock(&pool->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    task->state = PUSHED;
    task->joinStarted = 0;

    pool->queue[pool->cursor++] = task;
    pool->task_count++;

    if(pool->cursor >= TPOOL_MAX_TASKS){
        pool->cursor = 0;
    }

    if(pool->busy < pool->count || pool->count >= pool->capacity){
        pthread_cond_signal(&pool->poolCond);
    }else{
        struct worker_params *params = calloc(1, sizeof(struct worker_params));
        params->pool = pool;
        pthread_create(pool->threads + pool->count, NULL, worker, (void*)params);
        pool->count++;
    }

    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
    if(task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *task = calloc(1, sizeof(struct thread_task));
    (*task)->function = function;
    (*task)->arg = arg;
    configureThreadTask(*task);
	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
    if(task == NULL){
        return  TPOOL_ERR_INVALID_ARGUMENT;
    }

	return task->state == FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
    if(task == NULL){
        return  TPOOL_ERR_INVALID_ARGUMENT;
    }

    return task->state == IN_PROGRESS;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	if(result == NULL || task == NULL){
        return  TPOOL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&task->mutex);

    if(task->state == AWAIT){
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if(task->state == FINISHED || task->state == JOINED){
        task->state = JOINED;
        *result = task->returnVal;
        pthread_mutex_unlock(&task->mutex);
        return 0;
    }

    if(!task->joinStarted){
        task->joinStarted = 1;

        while(task->state != FINISHED){
            pthread_cond_wait(&task->taskCond, &task->mutex);
            if(task == NULL){
                return 0;
            }
        }

        task->state = JOINED;
        *result = task->returnVal;
    }

    pthread_mutex_unlock(&task->mutex);
    return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	if(result == NULL || task == NULL){
        return  TPOOL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&task->mutex);

    if(task->state == AWAIT){
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if(task->state == FINISHED || task->state == JOINED){
        task->state = JOINED;
        *result = task->returnVal;
        pthread_mutex_unlock(&task->mutex);
        return 0;
    }

    if(!task->joinStarted){
        task->joinStarted = 1;
        while(task->state != FINISHED){
            if(timeout == 0){
                pthread_cond_wait(&task->taskCond, &task->mutex);
                if(task == NULL){
                    return 0;
                }
            }else {
                struct timeval curent;
                gettimeofday(&curent, 0);
                struct timespec ts_timeout;
                ts_timeout.tv_sec = curent.tv_sec + (long long int)timeout;
                ts_timeout.tv_nsec = curent.tv_usec * 1000;

                pthread_cond_timedwait(&task->taskCond, &task->mutex, &ts_timeout);

                if(task == NULL){
                    return 0;
                }

                if (task->state != FINISHED) {
                    pthread_mutex_unlock(&task->mutex);
                    printf("TPOOL_ERR_TIMEOUT\n");
                    return TPOOL_ERR_TIMEOUT;
                }
            }
        }

        task->state = JOINED;
        *result = task->returnVal;
    }

    pthread_mutex_unlock(&task->mutex);
    return 0;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
    if(task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&task->mutex);
    if(task->state == AWAIT || task->state == JOINED || (task->state == FINISHED && task->detached)){
        pthread_mutex_unlock(&task->mutex);
        pthread_mutex_destroy(&task->mutex);
        pthread_cond_destroy(&task->taskCond);
        free(task);
        return 0;
    }

    pthread_mutex_unlock(&task->mutex);
    return TPOOL_ERR_TASK_IN_POOL;

}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
    if(task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&task->mutex);

    if(task->state == AWAIT){
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if(task->state == FINISHED){
        pthread_cond_broadcast(&task->taskCond);
        pthread_mutex_unlock(&task->mutex);
        thread_task_delete(task);
        return 0;
    }

    task->detached = 1;
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

#endif
