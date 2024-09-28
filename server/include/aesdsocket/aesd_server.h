/**
 * @file    aesd_server.h
 * @brief   AESD server application.
 */

#ifndef AESDSOCKET__AESD_SERVER_H_
#define AESDSOCKET__AESD_SERVER_H_

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "aesdsocket/aesd_worker.h"
#include "aesdsocket/queue.h"

struct aesd_worker_entry {
    pthread_t tid;
    struct aesd_worker *worker;
    SLIST_ENTRY(aesd_worker_entry) entries;
};

SLIST_HEAD(aesd_worker_slist, aesd_worker_entry);

/** @brief  AESD server application. */
struct aesd_server
{
    /** @brief  Buffer size to allocate for client workers. */
    size_t buf_size_;
    /** @brief  Output file descriptor. */
    int output_fd_;
    /** @brief  Mutex for output file. */
    pthread_mutex_t output_fd_lock_;
    /** @brief  Port on which the server is listening. */
    const char *port_;
    /** @brief  Socket fd for the server. */
    int sock_fd_;
    /** @brief  Timer for printing timestamps to the data file. */
    timer_t timer_;
    /** @brief  List of server workers. */
    struct aesd_worker_slist workers_;
};

/**
 * @brief   Initialize the server.
 *
 * @param   self
 * @param   buf_size    Buffer size in bytes for client workers.
 * @param   output_fd   Output file descriptor.
 */
void aesd_server_init(struct aesd_server *self, size_t buf_size, int output_fd);

/**
 * @brief   Create a socket fd and bind it to an address for listening.
 *
 * Exits the program with a failure status of -1 if the socket cannot be initialized.
 *
 * @param   self
 * @param   port    The port that the server should listen on.
 *
 * @return  true if the socket was bound, false otherwise.
 */
bool aesd_server_bind(struct aesd_server *self, const char *port);

/**
 * @brief   Start listening for incoming connections.
 *
 * Exits the program with a failure status of -1 if the socket cannot be initialized.
 *
 * @param   self
 * @param   backlog     Connection backlog length.
 *
 * @return  true if successful, false otherwise.
 */
bool aesd_server_listen(struct aesd_server *self, int backlog);

/**
 * @brief   Block and wait to accept an incoming client.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
bool aesd_server_accept_client(struct aesd_server *self);

/**
 * @brief   Check worker threads and cleanup resources.
 *
 * @param   self
 */
void aesd_server_check_workers(struct aesd_server *self);

/**
 * @brief   Close the listening server socket and stop worker threads.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
void aesd_server_shutdown(struct aesd_server *self);

#endif  // AESDSOCKET__AESD_SERVER_H_
