#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <limits.h>
#include <time.h>


/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */


struct TimeHandler{
    struct timespec
            time_start,
            time_end;
    double totalTimeMs;
};

struct CorHandler{
    char * name;
    int **arrayGlobal;
    int * sizes;
    int *current;
    int *globalSize;
};

struct Queue{
    char **str;
    int current;
    int last_elem;
    int max_size;
} queue;

struct Queue queue_init(int sz) {
    struct Queue newQ;
    newQ.current = 0;
    newQ.last_elem = 0;
    newQ.max_size = sz;
    newQ.str = calloc(sz, sizeof(char *));

    return newQ;
}

void queue_add(struct Queue* q, char *str) {
    q->str[q->last_elem] = str;
    q->last_elem++;
}

char *queue_pop(struct Queue* q) {
    q->current++;
    return q->str[q->current - 1];
}

struct stackEntry{
    int start;
    int end;
    struct stackEntry * prev;
};

struct stackGlobal{
    struct stackEntry * stack;
    unsigned int size;
};

int pushStack(struct stackGlobal *stack, int start, int end)
{
    struct stackEntry* newEntry = (struct stackEntry *)malloc(sizeof(struct stackEntry));
    if(newEntry == NULL){
        printf("pushStack ERROR: not enough  space\n");
        return -1;
    }
    newEntry->start = start;
    newEntry->end = end;
    newEntry->prev = stack->stack;
    ++stack->size;
    stack->stack = newEntry;
    return 0;
}

int popStack(struct  stackGlobal * stack, struct stackEntry * result)
{
    if(stack->size == 0){
        printf("popStack ERROR: Stack is empty\n");
        return -1;
    }
    *result = *(stack->stack);
    --stack->size;
    free(stack->stack);
    if(stack->size == 0){
        return 0;
    }
    stack->stack = result->prev;
    return 0;
}

void swap(int *a, int *b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

int take_partition(int whole[], int start, int end)
{
    int pivot = whole[end];
    int index = start;

    for (int i = start; i < end; ++i){
        if(whole[i] <= pivot){
            swap(whole + i, whole + index);
            ++index;
        }
    }
    swap(whole + index, whole + end);
    return index;
}

void quickSort(int array[], int n, struct TimeHandler *timer)
{
    struct stackGlobal stack;
    stack.size = 0;
    int start = 0;
    int end =  n - 1;
    pushStack(&stack, start, end);

    while(stack.size != 0){
        struct stackEntry current;
        popStack(&stack, &current);
        start = current.start;
        end = current.end;

        int pivot = take_partition(array, start, end);
        if(pivot - 1 > start){
            pushStack(&stack, start, pivot - 1);
        }

        if(pivot + 1 < end){
            pushStack(&stack, pivot + 1, end);
        }

        clock_gettime(CLOCK_MONOTONIC, &timer->time_end);
        double timeElapsed =
                ( (double)(timer->time_end.tv_nsec - timer->time_start.tv_nsec) / 1000.0);
        double totalTime =  (timer->time_end.tv_sec - timer->time_start.tv_sec) * 1000000.0 + timeElapsed;
        timer->totalTimeMs += totalTime;
        coro_yield();
        clock_gettime(CLOCK_MONOTONIC, &timer->time_start);
    }
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
    struct coro *this = coro_this();
    struct CorHandler *handler = context;
    char *name = handler->name;
    struct TimeHandler corTimer;
    corTimer.totalTimeMs = 0;
    clock_gettime(CLOCK_MONOTONIC, &corTimer.time_start);

    printf("Started coroutine %s\n", name);

    while(queue.current < queue.last_elem) {
        char *nameFile = queue_pop(&queue);


        FILE *file = fopen(nameFile, "r");
        int size = 4;
        int *array = (int *) calloc(size, sizeof(int));
        int index = 0;

        while (fscanf(file, "%d", array + index) != EOF) {
            ++index;
            if (size == index) {
                array = realloc(array, size * 2 * sizeof(int));
                size *= 2;
            }
        }
        fclose(file);

        quickSort(array, index, &corTimer);

        handler->arrayGlobal[*(handler->current)] = calloc(index, sizeof(int));
        handler->arrayGlobal[*(handler->current)] = memcpy(handler->arrayGlobal[*(handler->current)], array, index*sizeof(int));

        handler->sizes[*(handler->current)] = index;
        *handler->globalSize += index;
        ++*(handler->current);
        free(array);

    }
    clock_gettime(CLOCK_MONOTONIC, &corTimer.time_end);
    double timeElapsed =
            ( (double)(corTimer.time_end.tv_nsec - corTimer.time_start.tv_nsec) / 1000.0);
    double totalTime =  (corTimer.time_end.tv_sec - corTimer.time_start.tv_sec) * 1000000.0 + timeElapsed;
    printf("%s: Total execution time was %lf us\n", name, totalTime);
    printf("%s: switch count %lld\n", name, coro_switch_count(this));
    free(name);
    free(handler);
    return 0;
}

int
main(int argc, char **argv)
{
    int corutineNumber = atoi(argv[1]);
    struct TimeHandler mainTimer;
    clock_gettime(CLOCK_MONOTONIC, &mainTimer.time_start);

    queue = queue_init(argc - 1);
    for(int i = 2; i < argc; ++i) {
        queue_add(&queue, argv[i]);
    }

    int ** globalArray = calloc(queue.last_elem, sizeof(int *));
    int * globalSize = calloc(1, sizeof(int));
    int * sizes = calloc(queue.last_elem, sizeof(int));
    *globalSize = 0;
    int *currentPos = calloc(1, sizeof(int));
    *currentPos = 0;

    /* Initialize our coroutine global cooperative scheduler. */
    coro_sched_init();
    /* Start several coroutines. */
    for (int i = 0; i < corutineNumber; ++i) {
        /*
         * The coroutines can take any 'void *' interpretation of which
         * depends on what you want. Here as an example I give them
         * some names.
         */
        char name[16];
        sprintf(name, "coro_%d", i);
        /*
         * I have to copy the name. Otherwise all the coroutines would
         * have the same name when they finally start.
         */
        struct CorHandler *handler = calloc(1, sizeof(struct CorHandler));
        handler->name = strdup(name);
        handler->arrayGlobal = globalArray;
        handler->globalSize = globalSize;
        handler->current = currentPos;
        handler->sizes = sizes;
        coro_new(coroutine_func_f, handler);
    }
    /* Wait for all the coroutines to end. */
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        /*
         * Each 'wait' returns a finished coroutine with which you can
         * do anything you want. Like check its exit status, for
         * example. Don't forget to free the coroutine afterwards.
         */
        printf("Finished %d\n", coro_status(c));
        coro_delete(c);
    }
    /* All coroutines have finished. */

    int all_size = *globalSize;

    int *currents = calloc(queue.last_elem, sizeof(int));
    for(int i = 0; i < queue.last_elem; ++i) {
        currents[i] = 0;
    }

    int *result_vector = calloc(all_size, sizeof(int));
    int k = 0;

    while(k < all_size) {
        int idx_min = -1, min_value = INT_MAX;
        for(int i = 0; i < queue.last_elem; ++i) {
            if (currents[i] < sizes[i]) {
                int value = globalArray[i][currents[i]];
                if (value < min_value) {
                    min_value = value;
                    idx_min = i;
                }
            }
        }

        currents[idx_min]++;
        result_vector[k++] = min_value;
    }

    FILE * file  = fopen("result.txt", "w");
    for(int j = 0; j < all_size; ++j){
        fprintf(file, "%d ", result_vector[j]);
    }



    for(int i = 0; i < queue.current; ++i) {
        free(globalArray[i]);
    }

    free(globalArray);
    fclose(file);
    free(result_vector);
    free(queue.str);
    free(currents);
    free(globalSize);
    free(currentPos);
    free(sizes);

    clock_gettime(CLOCK_MONOTONIC, &mainTimer.time_end);
    double timeElapsed =
            ( (double)(mainTimer.time_end.tv_nsec - mainTimer.time_start.tv_nsec) / 1000.0);
    double totalTime =  (mainTimer.time_end.tv_sec - mainTimer.time_start.tv_sec) * 1000000.0 + timeElapsed;
    printf("TOTAL PROGRAM TIME: %lf us\n", totalTime);
    return 0;
}
