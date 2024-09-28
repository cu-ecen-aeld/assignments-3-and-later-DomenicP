/**
 * @file    aesd_server.c
 * @brief   AESD server application.
 */

#define _GNU_SOURCE

#include "aesdsocket/aesd_server.h"

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief   Cleanup resources used by a worker list entry.
 *
 * @param   entry   Pointer to the entry to be cleaned.
 */
static void cleanup_worker_entry(struct aesd_worker_entry *entry);

/**
 * @brief   Try to bind the first working server address.
 *
 * @param   srvs    Potential candidates for the server address.
 *
 * @return  The bound socket fd if successful, otherwise -1.
 */
static int try_bind(struct addrinfo *srvs);

void aesd_server_init(struct aesd_server *self, size_t buf_size, int output_fd)
{
    self->buf_size_ = buf_size;
    self->output_fd_ = output_fd;
    pthread_mutex_init(&self->output_fd_lock_, NULL);
    self->port_ = "";
    self->sock_fd_ = -1;
    SLIST_INIT(&self->workers_);
}

bool aesd_server_bind(struct aesd_server *self, const char *port)
{
    // Setup hints for TCP server sockets
    struct addrinfo hints = {
        // Use IPv4
        .ai_family = AF_INET,
        // Use a TCP socket
        .ai_socktype = SOCK_STREAM,
        // Set up for localhost address
        .ai_flags = AI_PASSIVE,
    };

    // Lookup potential addresses for the server
    struct addrinfo *srv_info = NULL;
    int result = getaddrinfo(NULL, port, &hints, &srv_info);
    if (result != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        return true;
    }

    // Try to bind the first working address
    self->sock_fd_ = try_bind(srv_info);

    // Free memory for getaddrinfo results
    freeaddrinfo(srv_info);

    // If we haven't exited by this point we should have a bound socket ready to go
    self->port_ = port;
    return true;
}

bool aesd_server_listen(struct aesd_server *self, int backlog)
{
    if (-1 == listen(self->sock_fd_, backlog))
    {
        perror("listen");
        close(self->sock_fd_);
        return false;
    }
    printf("server listening on port %s\n", self->port_);
    return true;
}

bool aesd_server_accept_client(struct aesd_server *self)
{
    struct aesd_worker *worker = aesd_worker_new(
        self->buf_size_, self->output_fd_, &self->output_fd_lock_
    );
    if (worker == NULL)
    {
        perror("malloc");
        return false;
    }
    socklen_t client_addr_len = sizeof(worker->client_addr);

    // Accept an incoming connection
    worker->client_fd = accept(self->sock_fd_, &worker->client_addr, &client_addr_len);
    if (-1 == worker->client_fd)
    {
        perror("accept");
        aesd_worker_delete(worker);
        worker = NULL;
        return false;
    }

    // Log the client connection
    char client_ip4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &worker->client_addr.sin_addr, client_ip4_str, sizeof(client_ip4_str));
    syslog(LOG_INFO, "Accepted connection from %s", client_ip4_str);

    // Allocate a worker list entry and move ownership of the worker pointer
    struct aesd_worker_entry *entry
        = (struct aesd_worker_entry *)malloc(sizeof(struct aesd_worker_entry));
    entry->worker = worker;
    worker = NULL;

    // Start the thread and push the entry onto the list
    pthread_create(&entry->tid, NULL, aesd_worker_main, entry->worker);
    SLIST_INSERT_HEAD(&self->workers_, entry, entries);
    entry = NULL;

    return true;
}

void aesd_server_check_workers(struct aesd_server *self)
{
    struct aesd_worker_entry *entry = NULL;
    struct aesd_worker_entry *entry_temp = NULL;
    SLIST_FOREACH_SAFE(entry, &self->workers_, entries, entry_temp)
    {
        if (entry->worker->exited)
        {
            SLIST_REMOVE(&self->workers_, entry, aesd_worker_entry, entries);
            cleanup_worker_entry(entry);
            entry = NULL;
        }
    }
}

void aesd_server_shutdown(struct aesd_server *self)
{
    if (-1 == close(self->sock_fd_))
    {
        perror("server socket close");
    }
    struct aesd_worker_entry *entry = NULL;
    struct aesd_worker_entry *entry_temp = NULL;
    SLIST_FOREACH_SAFE(entry, &self->workers_, entries, entry_temp)
    {
        entry->worker->shutdown = true;
        SLIST_REMOVE(&self->workers_, entry, aesd_worker_entry, entries);
        cleanup_worker_entry(entry);
        entry = NULL;
    }
    pthread_mutex_destroy(&self->output_fd_lock_);
}

static void cleanup_worker_entry(struct aesd_worker_entry *entry)
{
    if (-1 == pthread_join(entry->tid, NULL))
    {
        perror("pthread_join");
    }
    aesd_worker_delete(entry->worker);
    entry->worker = NULL;
    free(entry);
}

static int try_bind(struct addrinfo *srvs)
{
    // Loop through potential addresses
    for (struct addrinfo *pinfo = srvs; pinfo != NULL; pinfo = pinfo->ai_next)
    {
        // Try to create a socket
        int sockfd = socket(pinfo->ai_family, pinfo->ai_socktype, pinfo->ai_protocol);
        if (sockfd == -1)
        {
            // Failed to create the socket fd, try the next option
            perror("socket");
            continue;
        }

        // Enable address reuse to avoid "already in use" error messages
        int on = 1;
        if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
        {
            perror("setsockopt");
            close(sockfd);
            return -1;
        }

        // Try to bind the socket to the address
        if (-1 == bind(sockfd, pinfo->ai_addr, pinfo->ai_addrlen))
        {
            perror("bind");
            close(sockfd);
        }

        // Good to go!
        return sockfd;
    }

    // Failed to bind any addresses
    return -1;
}
