#ifndef AESDSOCKET__AESD_WORKER_H_
#define AESDSOCKET__AESD_WORKER_H_

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief  AESD server worker thread. */
struct aesd_worker
{
    /** @brief  Working buffer for client IO. */
    char *buf;
    /** @brief  Size of the workingbuffer in bytes. */
    size_t buf_size;
    /** @brief  Address information for the client. */
    struct sockaddr_in client_addr;
    /** @brief  Socket fd for the client. */
    int client_fd;
    /** Thread should set this true before exiting. */
    bool exited;
    /** @brief  Output file descriptor. */
    int output_fd;
    /** @brief  Mutex for synchronizing access to the output file. */
    pthread_mutex_t *output_fd_lock;
    /** @brief  Parent thread can set this to true to request shutdown. */
    bool shutdown;
};

/**
 * @brief   Allocate a new AESD worker.
 *
 * @param   buf_size        Size of the worker buffer in bytes.
 * @param   output_fd       Output file descriptor.
 * @param   output_fd_lock  Mutex for synchronizing acces to the output file.
 *
 * @return  Pointer to the allocated worker if successful, NULL on failure.
 */
struct aesd_worker *aesd_worker_new(
    size_t buf_size, int output_fd, pthread_mutex_t *output_fd_lock
);

/**
 * @brief   Free the memory used by an AESD worker.
 *
 * @param   self
 */
static inline void aesd_worker_delete(struct aesd_worker *self)
{
    free(self->buf);
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

#endif  // AESDSOCKET__AESD_WORKER_H_
