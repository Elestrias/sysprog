#include "thread_pool.h"
#include <pthread.h>
#include "stdio.h"
#include <time.h>
#include "stdlib.h"
#include <unistd.h>


struct TaskQueue{
    struct thread_task* head;
    struct thread_task* tail;
    pthread_mutex_t mutex;
    pthread_cond_t condVar;
    int awaited_threads;
    int capacity;
    int size;
    int stopped;
};

struct thread_task {
	thread_task_f function;
	void *arg;
    struct timespec startOfJoin;
    int joinStarted;
    int joinFinished;

    enum{
      AWAIT = 0,
      PUSHED = 1,
      IN_PROGRESS = 2,
      FINISHED = 3,
    } state;
    int detached;
    struct thread_task * prev;
    void* returnVal;
    int deleted;
};

void configureThreadTask(struct thread_task * task){
    task->joinStarted = 0;
    task->joinFinished = 0;
    task->state = 0;
    task->detached = 0;
    task->deleted = 0;
}

struct thread_pool {
	pthread_t *threads;
    pthread_t main_thread;
    pthread_mutex_t mutex;
    int count;
    int capacity;
    int busy;

    struct TaskQueue*  queue;
    int task_count;
    int assigned_task_count;
    int stopped;
	/* PUT HERE OTHER MEMBERS */
};

struct worker_params {
    struct thread_pool* pool;
};

void pushToQueue(struct TaskQueue* queue, struct thread_task * taskToPush){
    pthread_mutex_lock(&queue->mutex);

    if(queue->size < queue->capacity) {
        if (queue->size == 0) {
            queue->head = taskToPush;
            queue->tail = taskToPush;
        } else {
            queue->tail->prev = taskToPush;
            queue->tail = taskToPush;
        }
        taskToPush->state = PUSHED;
        ++queue->size;
        pthread_mutex_unlock(&queue->mutex);
        pthread_cond_signal(&queue->condVar);

    }else {
        pthread_mutex_unlock(&queue->mutex);
    }
}

struct thread_task* PopFromQueue(struct TaskQueue* queue){
    pthread_mutex_lock(&queue->mutex);
    ++queue->awaited_threads;
    while(queue->size <= 0 && !queue->stopped) {
        pthread_cond_wait(&queue->condVar, &queue->mutex);
    }
    --queue->awaited_threads;
    if(queue->stopped){
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    --queue->size;

    struct thread_task* val = queue->head;
    queue->head = val->prev;


    pthread_mutex_unlock(&queue->mutex);
    return val;
}

void* worker(void * v){
    struct worker_params* param = v;
    struct thread_pool* pool = param->pool;
//    ++pool->count;

    while(!pool->stopped) {
        struct thread_task *task;

        task = PopFromQueue(pool->queue);

        if(pool->stopped){
            free(param);
            pthread_exit(0);
        }

        if (task == NULL) {
            continue;
        }

        ++pool->busy;
        task->state = IN_PROGRESS;


        task->returnVal = task->function(task->arg);

        task->state = FINISHED;
        --pool->busy;
    }
    free(param);
    pthread_exit(0);
}

void thread_pool_main(struct thread_pool * pool){
    while (!pool->stopped){
        if(pool->count < pool->capacity && pool->count == pool->busy && pool->queue->size > 0){
//            int searched_thread_cell = -1;
//            for(int i = 0; i < pool->capacity; i++){
//                if(pool->threads + i == NULL){
//                    searched_thread_cell = i;
//                    break;
//                }
//            }
//
//            if(!pool->stopped) {
//                struct worker_params *params = calloc(1, sizeof(struct worker_params));
//                params->pool = pool;
//                pthread_create(pool->threads + searched_thread_cell, NULL, (void *) worker, params);
//            }
            struct worker_params *params = calloc(1, sizeof(struct worker_params));
            params->pool = pool;
            pthread_create(pool->threads + pool->count, NULL, worker, (void*)params);
            pool->count++;
        }
    }
    pthread_exit(0);
}

void configureThreadPool(struct thread_pool * pool, int capacity){
    pool->capacity = capacity;
    pool->count = 0;
    pool->queue = calloc(1, sizeof(struct TaskQueue));
    pool->stopped = 0;
    pool->busy = 0;

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pool->queue->mutex = mutex;

    pthread_cond_t condVar = PTHREAD_COND_INITIALIZER;
    pool->queue->condVar = condVar;
    pool->queue->capacity = TPOOL_MAX_TASKS;
    pool->queue->size = 0;
    pool->queue->stopped = 0;
    pool->queue->awaited_threads = 0;
    pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
    pool->mutex = mutex2;
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
    pthread_create(&((*pool)->main_thread), NULL, (void*)thread_pool_main, (void *)*(pool));
	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
    printf("COUNT: %d\n", pool->count);
	return pool->count;
}

int
thread_pool_delete(struct thread_pool *pool)
{
    pthread_cond_broadcast(&pool->queue->condVar);

	if(pool == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&pool->queue->mutex);
    if(pool->queue->size > 0){
        pthread_mutex_unlock(&pool->queue->mutex);
        return TPOOL_ERR_HAS_TASKS;
    }

    pool->stopped = 1;
    pool->queue->stopped = 1;
    pthread_mutex_unlock(&pool->queue->mutex);

    pthread_cond_broadcast(&pool->queue->condVar);
    printf("DELETE: SIGNAL\n");

    pthread_join(pool->main_thread, NULL);
    printf("DELETE: MAIn JOINED\n");

    for(int i = 0; i < pool->capacity; ++i){
        if(pool->threads + i != NULL){
            pthread_join(pool->threads[i], NULL);
        }
    }
    free(pool->threads);
    printf("DELETE: workers JOINED\n");

    free(pool->queue);
    free(pool);
	return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
    if(pool == NULL || task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&pool->queue->mutex);
    if(pool->queue->size + pool->count >= TPOOL_MAX_TASKS){
        pthread_mutex_unlock(&pool->queue->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
    pthread_mutex_unlock(&pool->queue->mutex);

    pushToQueue(pool->queue, task);
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

    if(task->state == AWAIT){
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if(task->state == FINISHED){
        *result = task->returnVal;
        return 0;
    }

    if(!task->joinStarted){
        task->joinStarted = 1;
        while(task->state != FINISHED);

        task->joinFinished = 1;
        *result = task->returnVal;
    }

    return 0;
}


int
thread_task_delete(struct thread_task *task)
{
    if(task == NULL){
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    if(task->state == AWAIT || task->joinFinished){
//        if(task->prev != NULL) {
//            task->prev->next = task->next;
//        }
//        if(task->next != NULL) {
//            task->next->prev = task->prev;
//        }
//
//        if(task-> queue != NULL) {
//            pthread_mutex_lock(&task->queue->mutex);
//            --task->queue->size;
//            pthread_mutex_unlock(&task->queue->mutex);
//        }

        free(task);
        return 0;
    }else{

        return TPOOL_ERR_TASK_IN_POOL;
    }
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
