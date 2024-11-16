#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../common/glue_dll.h"
#include "../common/thread_lib.h"

void *worker_func(void *arg)
{
    char *name = (char *)arg;
    if (!name || name[0] == '\0')
    {
        name = strdup("stranger");
    }

    printf("Hello %s\n", name);
    return NULL;
}

int main()
{
    // Create and initialze a thread pool.
    thread_pool_t *th_pool = calloc(1, sizeof(thread_pool_t));
    thread_pool_init(th_pool);

    // Create two threads (not execution units, just thread_t data structures).
    thread_t *thread1 = thread_create(0, "thread_1");
    thread_t *thread2 = thread_create(0, "thread_2");
    thread_t *thread3 = thread_create(0, "thread_3");
    thread_t *thread4 = thread_create(0, "thread_4");

    // Insert both threads in thread pools.
    thread_pool_insert_new_thread(th_pool, thread1);
    thread_pool_insert_new_thread(th_pool, thread2);
    thread_pool_insert_new_thread(th_pool, thread3);
    thread_pool_insert_new_thread(th_pool, thread4);

    bool block = true;

    // Block caller is set to `false` should work because
    // there are enough worker thread handle jobs.
    /*
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_1"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_2"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_3"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_4"), !block);
    */

    // Block caller is set to `true` work in sequential fashion.
    /*
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_1"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_2"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_3"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_4"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_5"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_6"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_7"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_8"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_9"), block);
    */

    // Mix style
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_1"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_2"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_3"), block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_4"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_5"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_6"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_7"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_8"), !block);
    thread_pool_dispatch_thread(th_pool, worker_func, (void *)("worker_9"), block);
   
    pthread_exit(0);
    return 0;
}
