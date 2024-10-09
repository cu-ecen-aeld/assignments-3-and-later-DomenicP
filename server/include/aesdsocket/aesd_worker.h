#ifndef AESDSOCKET__AESD_WORKER_H_
#define AESDSOCKET__AESD_WORKER_H_

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "aesdsocket/queue.h"

/** @brief  AESD server worker thread. */
struct aesd_worker
{
    /** @brief  Address information for the client. */
    struct sockaddr_in client_addr;
    /** @brief  Socket fd for the client. */
    int client_fd;
    /** @brief Thread should set this to `true` before exiting. */
    atomic_bool exited;
    /** @brief  Parent thread can set this to true to request shutdown. */
    atomic_bool shutdown;
    /** @brief  Working buffer for client IO. */
    char *buf_;
    /** @brief  Size of the working buffer in bytes. */
    size_t buf_size_;
    /** @brief  If `true`, indicates the output file is a char device, not a plain file. */
    bool char_dev_;
    /** @brief  Mutex for synchronizing access to the output file. */
    pthread_mutex_t *output_lock_;
    /** @brief  Output file descriptor. */
    const char *output_path_;
};

/**
 * @brief   Allocate a new AESD worker.
 *
 * @param   buf_size        Size of the worker buffer in bytes.
 * @param   char_dev        Output file is a character device, not a plain file.
 * @param   output_path     Path to the output file.
 * @param   output_lock     Mutex for synchronizing access to the output file.
 *
 * @return  Pointer to the allocated worker if successful, NULL on failure.
 */
struct aesd_worker *aesd_worker_new(
    size_t buf_size, bool char_dev, const char *output_path, pthread_mutex_t *output_lock
);

/**
 * @brief   Free the memory used by an AESD worker.
 *
 * @param   self
 */
static inline void aesd_worker_delete(struct aesd_worker *self)
{
    free(self->buf_);
    free(self);
}

/**
 * @brief   Thread routine.
 *
 * @param   arg     Argument pointer passed by `pthread_create()`.
 *
 * @return  `NULL`.
 */
void *aesd_worker_main(void *arg);

/** @brief  List entry for an AESD server worker thread. */
struct aesd_worker_entry {
    /** @brief  Thread ID. */
    pthread_t tid;
    /** @brief  Thread data. */
    struct aesd_worker *worker;
    /** @brief  Linked-list pointer. */
    SLIST_ENTRY(aesd_worker_entry) entries;
};

/** @brief  Singly-linked list for AESD server worker threads. */
SLIST_HEAD(aesd_worker_slist, aesd_worker_entry);

/**
 * @brief   Allocate a new worker list entry.
 *
 * @param   worker  Worker pointer to take ownership of.
 *
 * @return  Pointer to the new entry if successful, `NULL` on failure.
 */
static inline struct aesd_worker_entry *aesd_worker_entry_new(struct aesd_worker *worker)
{
    struct aesd_worker_entry *self = malloc(sizeof(struct aesd_worker_entry));
    if (self != NULL) {
        self->worker = worker;
    }
    return self;
}

/**
 * @brief   Free memory for a worker list entry.
 *
 * @param   self
 */
static inline void aesd_worker_entry_delete(struct aesd_worker_entry *self)
{
    aesd_worker_delete(self->worker);
    free(self);
}

/**
 * @brief   Start a worker thread.
 *
 * @param   self
 *
 * @return  `true` if starting the thread was successful, `false` otherwise.
 */
static inline bool aesd_worker_entry_start(struct aesd_worker_entry *self)
{
    int error = pthread_create(&self->tid, NULL, aesd_worker_main, self->worker);
    if (error) {
        errno = error;
        perror("worker pthread_create");
        return false;
    }
    return true;
}

/**
 * @brief   Thread join for a worker list entry.
 *
 * @param   self
 */
static inline void aesd_worker_entry_join(struct aesd_worker_entry *self)
{
    if (-1 == pthread_join(self->tid, NULL)) {
        perror("worker pthread_join");
    }
}

#endif  // AESDSOCKET__AESD_WORKER_H_
