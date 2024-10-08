/**
 * @file    aesd_server.h
 * @brief   AESD server application.
 */

#ifndef AESDSOCKET__AESD_SERVER_H_
#define AESDSOCKET__AESD_SERVER_H_

#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

#include "aesdsocket/aesd_worker.h"

/** @brief  AESD server application. */
struct aesd_server
{
    /** @brief  Set this to false to shutdown the server. */
    atomic_bool running;
    /** @brief  Buffer size to allocate for client workers. */
    size_t buf_size_;
    /** @brief  If `true`, indicates the output file is a char device, not a plain file. */
    bool char_dev_;
    /** @brief  Output file descriptor. */
    int output_fd_;
    /** @brief  Path to the output file. */
    const char *output_path_;
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
 * @param   buf_size        Buffer size in bytes for client workers.
 * @param   char_dev        Output file is a character device, not a plain file.
 * @param   output_path     Path to the output file.
 */
void aesd_server_init(
    struct aesd_server *self, size_t buf_size, bool char_dev, const char *output_path
);

/**
 * @brief   Run the server.
 *
 * @param   self
 * @param   port        Port on which to listen.
 * @param   backlog     Connection backlog depth.
 *
 * @return  0 on success, -1 on failure.
 */
int aesd_server_run(struct aesd_server *self, const char *port, int backlog);

#endif  // AESDSOCKET__AESD_SERVER_H_
