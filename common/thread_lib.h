#ifndef __THREAD_LIB__
#define __THREAD_LIB__

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <semaphore.h>

// Thread pool is designed to handle three stages:
// - Stage 1: Application assign thread the work (fun + arg).
// - Stage 2: Thread executes the execution flows and finish the work.
// - Stage 3: Thread back to the pool itself.

// When the thread is running and doing its work normally.
#define THREAD_F_RUNNING (1 << 0)

// When the thread has been marked to pause, but not paused yet.
#define THREAD_F_MARKED_FOR_PAUSE (1 << 1)

// When thread is blocked (paused).
#define THREAD_F_PAUSED (1 << 2)

// When thread is blocked on pthread_cond_t for reason other than paused.
#define THREAD_F_BLOCKED (1 << 3)

typedef struct thread_
{
    // Name of the thread.
    char name[32];

    // Whether execution unit has been created or not.
    bool thread_created;

    // Thread handle.
    pthread_t thread;

    // Thread attributes.
    pthread_attr_t attributes;

    // Thread execution function.
    void *(*thread_fn)(void *);

    // Thread function argument.
    void *arg;

    // Function to be involked just before pausing the thread.
    void *(*thread_pause_fn)(void *);

    // Thread pause function argument.
    void *pause_arg;

    // Thread state.
    uint32_t flags;

    // Updated thread state mutual exclusively.
    pthread_mutex_t state_mutex;

    // Conditions which thread will block itself.
    pthread_cond_t cv;

    // Block the caller until worker thread completes job
    // and goes back to pool.
    sem_t *semaphore;

    glue_t glue;
} thread_t;
GLSTRUCT_TO_STRUCT(wait_glue_to_thread, thread_t, glue)

thread_t *thread_create(thread_t *thread, char *name);
void thread_run(thread_t *thread, void *(*thread_fn)(void *), void *arg);
void thread_set_thread_attribute_joinable_or_detached(thread_t *thread, bool joinable);

//
// Thread pause and resume APIs.
//
void thread_set_pause_fn(thread_t *thread, void *(*thread_pause_fn)(void *), void *pause_arg);
void thread_pause(thread_t *thread);
void thread_resume(thread_t *thread);
void thread_test_and_pause(thread_t *thread);

//
// Thread pool APIs.
//
typedef struct thread_pool_
{
    // Glue way double link list of thread.
    glue_t pool_head;

    // Operated on thread list mutual exclusively.
    pthread_mutex_t mutex;
} thread_pool_t;

typedef struct thread_execution_data_
{
    // Actual execution flow.
    void *(*thread_stage2_fn)(void *);
    void *stage2_arg;

    // Back to the pool itself.
    void (*thread_stage3_fn)(thread_pool_t *, thread_t *);

    thread_pool_t *thread_pool;
    thread_t *thread;
} thread_execution_data_t;

void thread_pool_init(thread_pool_t *th_pool);
void thread_pool_insert_new_thread(thread_pool_t *th_pool, thread_t *thread);
thread_t *thread_pool_get_thread(thread_pool_t *th_pool);
void thread_pool_dispatch_thread(thread_pool_t *th_pool,
                                 void *(*thread_fn)(void *),
                                 void *arg,
                                 bool block_caller);

#endif /* __THREAD_LIB__  */
