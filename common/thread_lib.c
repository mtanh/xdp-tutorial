#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <assert.h>
#include "glue_dll.h"
#include "bitsop.h"
#include "thread_lib.h"

thread_t *thread_create(thread_t *thread, char *name)
{
    if (!thread)
    {
        thread = (thread_t *)calloc(1, sizeof(thread_t));
    }

    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return NULL;
    }

    strncpy(thread->name, name, sizeof(thread->name));
    thread->thread_created = false;
    thread->arg = NULL;
    thread->thread_fn = NULL;
    thread->flags = 0;
    thread->thread_pause_fn = 0;
    thread->pause_arg = 0;
    pthread_mutex_init(&thread->state_mutex, NULL);
    pthread_cond_init(&thread->cv, NULL);
    pthread_attr_init(&thread->attributes);

    return thread;
}

void thread_run(thread_t *thread, void *(*thread_fn)(void *), void *arg)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    thread->thread_fn = thread_fn;
    thread->arg = arg;
    thread->thread_created = true;
    SET_BIT(thread->flags, THREAD_F_RUNNING);
    pthread_create(&thread->thread,
                   &thread->attributes,
                   thread_fn,
                   arg);
}

void thread_set_thread_attribute_joinable_or_detached(thread_t *thread, bool joinable)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    pthread_attr_setdetachstate(&thread->attributes,
                                joinable ? PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED);
}

//
// Thread pause and resume APIs.
//
void thread_set_pause_fn(thread_t *thread, void *(*thread_pause_fn)(void *), void *pause_arg)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    thread->thread_pause_fn = thread_pause_fn;
    thread->pause_arg = pause_arg;
}

void thread_pause(thread_t *thread)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    pthread_mutex_lock(&thread->state_mutex);

    if (IS_BIT_SET(thread->flags, THREAD_F_RUNNING))
    {
        SET_BIT(thread->flags, THREAD_F_MARKED_FOR_PAUSE);
    }

    pthread_mutex_unlock(&thread->state_mutex);
}

void thread_resume(thread_t *thread)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    pthread_mutex_lock(&thread->state_mutex);

    if (IS_BIT_SET(thread->flags, THREAD_F_PAUSED))
    {
        pthread_cond_signal(&thread->cv);
    }

    pthread_mutex_unlock(&thread->state_mutex);
}

void thread_test_and_pause(thread_t *thread)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    pthread_mutex_lock(&thread->state_mutex);

    if (IS_BIT_SET(thread->flags, THREAD_F_MARKED_FOR_PAUSE))
    {
        SET_BIT(thread->flags, THREAD_F_PAUSED);
        UNSET_BIT(thread->flags, THREAD_F_MARKED_FOR_PAUSE);
        UNSET_BIT(thread->flags, THREAD_F_RUNNING);

        pthread_cond_wait(&thread->cv, &thread->state_mutex);
        SET_BIT(thread->flags, THREAD_F_RUNNING);
        UNSET_BIT(thread->flags, THREAD_F_PAUSED);
        (thread->thread_pause_fn)(thread->pause_arg);

        pthread_mutex_unlock(&thread->state_mutex);
    }
    else
    {
        pthread_mutex_unlock(&thread->state_mutex);
    }
}

//
// Thread pool APIs.
//
void thread_pool_init(thread_pool_t *th_pool)
{
    if (th_pool == NULL)
    {
        fprintf(stderr, "%s: invalid thread pool data.\n", __func__);
        return;
    }

    init_glue(&th_pool->pool_head);
    pthread_mutex_init(&th_pool->mutex, NULL);
}

void thread_pool_insert_new_thread(thread_pool_t *th_pool, thread_t *thread)
{
    if (th_pool == NULL || thread == NULL)
    {
        fprintf(stderr, "%s: invalid data.\n", __func__);
        return;
    }

    pthread_mutex_lock(&th_pool->mutex);

    assert(is_empty(&thread->glue));
    assert(thread->thread_fn == NULL);

    list_add_next(&th_pool->pool_head, &thread->glue);

    pthread_mutex_unlock(&th_pool->mutex);
}

thread_t *thread_pool_get_thread(thread_pool_t *th_pool)
{
    if (th_pool == NULL)
    {
        fprintf(stderr, "%s: invalid thread pool data.\n", __func__);
        return NULL;
    }

    thread_t *thread = NULL;
    glue_t *glthread = NULL;

    pthread_mutex_lock(&th_pool->mutex);

    glthread = list_dequeue(&th_pool->pool_head);
    if (!glthread)
    {
        pthread_mutex_unlock(&th_pool->mutex);
        return NULL;
    }

    thread = wait_glue_to_thread(glthread);
    pthread_mutex_unlock(&th_pool->mutex);

    return thread;
}

static void thread_pool_run_thread(thread_t *thread)
{
    if (thread == NULL)
    {
        fprintf(stderr, "%s: invalid thread data.\n", __func__);
        return;
    }

    // Make sure the thread in popped from the pool.
    if (!is_empty(&thread->glue))
    {
        return;
    }
    assert(is_empty(&thread->glue));

    if (!thread->thread_created)
    {
        thread_run(thread, thread->thread_fn, thread->arg);
    }
    else
    {
        // If the thread is already exist, it means it is in the blocked state
        // and ready for the new work. So it is good to resume the execution again.
        pthread_cond_signal(&thread->cv);
    }
}

static void thread_pool_thread_stage3_fn(thread_pool_t *th_pool, thread_t *thread)
{
    if (th_pool == NULL || thread == NULL)
    {
        fprintf(stderr, "%s: invalid data.\n", __func__);
        return;
    }

    pthread_mutex_lock(&th_pool->mutex);

    list_add_next(&th_pool->pool_head, &thread->glue);

    if (thread->semaphore)
    {
        sem_post(thread->semaphore);
    }

    // Back to the pool.
    pthread_cond_wait(&thread->cv, &th_pool->mutex);
    pthread_mutex_unlock(&th_pool->mutex);
}

static void *thread_fn_execute_stage2_and_stage3(void *arg)
{
    thread_execution_data_t *thread_execution_data = (thread_execution_data_t *)arg;
    if (thread_execution_data == NULL)
    {
        fprintf(stderr, "%s: invalid thread execution data.\n", __func__);
        return NULL;
    }

    while (1)
    {
        // Actual thread function and its arguments.
        thread_execution_data->thread_stage2_fn(thread_execution_data->stage2_arg);

        // Worker thread goes back to the pool.
        thread_execution_data->thread_stage3_fn(thread_execution_data->thread_pool,
                                                thread_execution_data->thread);
    }
}

void thread_pool_dispatch_thread(thread_pool_t *th_pool,
                                 void *(*thread_fn)(void *),
                                 void *arg,
                                 bool block_caller)
{
    // Grab the thread from pool.
    thread_t *thread = thread_pool_get_thread(th_pool);
    if (thread == NULL)
    {
        fprintf(stderr, "%s: No thread available.\n", __func__);
        return;
    }

    // Application blocks itself until worker thread completes its work
    // and back to the pool.
    if (block_caller && !thread->semaphore)
    {
        thread->semaphore = (sem_t *)calloc(1, sizeof(sem_t));
        assert(thread->semaphore != NULL);

        sem_init(thread->semaphore, 0, 0);
    }

    thread_execution_data_t *thread_execution_data = (thread_execution_data_t *)(thread->arg);

    if (thread_execution_data == NULL)
    {
        // Pack the information which worker needs to execute work flow
        // and how it back to the pool.
        thread_execution_data = calloc(1, sizeof(thread_execution_data_t));
        if (thread_execution_data == NULL)
        {
            fprintf(stderr, "%s: invalid thread execution data.\n", __func__);
            return;
        }
    }

    thread_execution_data->thread_stage2_fn = thread_fn;
    thread_execution_data->stage2_arg = arg;

    thread_execution_data->thread_stage3_fn = thread_pool_thread_stage3_fn;
    thread_execution_data->thread_pool = th_pool;
    thread_execution_data->thread = thread;

    thread->thread_fn = thread_fn_execute_stage2_and_stage3;
    thread->arg = (void *)thread_execution_data;

    // Fire the thread.
    thread_pool_run_thread(thread);

    if (block_caller)
    {
        // Wait for worker thread to finish its work and back to pool.
        sem_wait(thread->semaphore);

        // Caller is notified, release the sem_t resource.
        sem_destroy(thread->semaphore);
        free(thread->semaphore);
        thread->semaphore = NULL;
    }
}
