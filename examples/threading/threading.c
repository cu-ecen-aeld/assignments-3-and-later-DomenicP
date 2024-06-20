#include "threading.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
// #define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

#define US_PER_MS 1000

static bool try_sleep(int ms, const char *err_msg)
{
    DEBUG_LOG("sleeping for %d ms", ms);
    if (-1 == usleep(ms * US_PER_MS))
    {
        perror(err_msg);
        return false;
    }
    return true;
}

static bool try_lock(pthread_mutex_t *mutex)
{
    DEBUG_LOG("locking mutex");
    int result = pthread_mutex_lock(mutex);
    if (result != 0)
    {
        fprintf(stderr, "failed to lock mutex: %s\n", strerror(result));
        return false;
    }
    DEBUG_LOG("locked mutex");
    return true;
}

static bool try_unlock(pthread_mutex_t *mutex)
{
    DEBUG_LOG("unlocking mutex");
    int result = pthread_mutex_unlock(mutex);
    if (result != 0)
    {
        fprintf(stderr, "failed to unlock mutex: %s\n", strerror(result));
        return false;
    }
    DEBUG_LOG("unlocked mutex");
    return true;
}

static void *threadfunc(void *thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data *data = (struct thread_data *)thread_param;
    data->thread_complete_success = (
        try_sleep(data->wait_to_obtain_ms, "failed to wait before locking mutex")
            && try_lock(data->mutex)
            && try_sleep(data->wait_to_release_ms, "failed to wait after locking mutex")
            && try_unlock(data->mutex)
    );
    pthread_exit(thread_param);
}

static struct thread_data *thread_data_create(
    int wait_to_obtain_ms, int wait_to_release_ms, pthread_mutex_t *mutex
)
{
    DEBUG_LOG("allocating thread data");
    struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
    if (data != NULL)
    {
        data->wait_to_obtain_ms = wait_to_obtain_ms;
        data->wait_to_release_ms = wait_to_release_ms;
        data->thread_complete_success = false;
        data->mutex = mutex;
    }
    return data;
}

bool start_thread_obtaining_mutex(
    pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms
)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to
     * created thread using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    struct thread_data *data = thread_data_create(wait_to_obtain_ms, wait_to_release_ms, mutex);
    if (data == NULL)
    {
        fprintf(stderr, "could not allocate thread data\n");
        return false;
    }

    // Start the thread
    DEBUG_LOG("starting thread");
    int result = pthread_create(thread, NULL, threadfunc, data);
    if (result != 0)
    {
        fprintf(stderr, "failed to create thread: %s\n", strerror(result));
        return false;
    }
    DEBUG_LOG("started thread");
    return true;
}
