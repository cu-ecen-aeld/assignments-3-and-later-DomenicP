/**
 * @file    aesd_server.c
 * @brief   AESD server application.
 */

#define _GNU_SOURCE

#include "aesdsocket/aesd_server.h"

#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief   Allocate a new worker list entry.
 *
 * @param   worker  Worker pointer to take ownership of.
 *
 * @return  Pointer to the new entry if successfull, `NULL` on failure.
 */
static struct aesd_worker_entry *aesd_worker_entry_new(struct aesd_worker *worker)
{
    struct aesd_worker_entry *self
        = (struct aesd_worker_entry *)malloc(sizeof(struct aesd_worker_entry));
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
static void aesd_worker_entry_delete(struct aesd_worker_entry *self)
{
    aesd_worker_delete(self->worker);
    self->worker = NULL;
    free(self);
}


/**
 * @brief   Thread join for a worker list entry.
 *
 * @param   self
 */
static void aesd_worker_entry_join(struct aesd_worker_entry *self)
{
    if (-1 == pthread_join(self->tid, NULL)) {
        perror("pthread_join");
    }
}

/** @brief  Timer alarm signal handler. */
static void alarm_handler(int sig, siginfo_t *si, void *uc)
{
    (void)uc;

    if (sig == SIGALRM) {
        // Format the timestamp string
        time_t now = time(NULL);
        struct tm *now_local = localtime(&now);
        char timestamp_str[128];
        size_t timestamp_str_len = strftime(
            timestamp_str, sizeof(timestamp_str), "timestamp:%a, %d %b %Y %T %z\n", now_local
        );

        // Write the string to the output file
        struct aesd_server *self = si->si_value.sival_ptr;
        pthread_mutex_lock(&self->output_fd_lock_);
        // Seek to the end of the file
        if (-1 == lseek(self->output_fd_, 0, SEEK_END))
        {
            perror("timer lseek");
        }
        // Write to disk
        else if (-1 == write(self->output_fd_, timestamp_str, timestamp_str_len))
        {
            perror("timer write");
        }
        pthread_mutex_unlock(&self->output_fd_lock_);
    }
}

/**
 * @brief   Create and start the timestamp timer.
 *
 * @param   self
 */
static void start_timestamp_timer(struct aesd_server *self)
{
    // Setup the alarm signal handler
    int result = sigaction(
        SIGALRM,
        &(struct sigaction) {
            .sa_flags = SA_SIGINFO,
            .sa_sigaction = alarm_handler
        },
        NULL
    );
    if (result == -1) {
        perror("aesd_server sigaction");
        return;
    }

    // Create the timer
    result = timer_create(
        CLOCK_MONOTONIC,
        &(struct sigevent) {
            .sigev_notify = SIGEV_SIGNAL,
            .sigev_signo = SIGALRM,
            .sigev_value.sival_ptr = self,
        },
        &self->timer_
    );
    if (result == -1) {
        perror("aesd_server timer_create");
        return;
    }

    // Arm the timer
    result = timer_settime(
        self->timer_,
        0,
        &(struct itimerspec) {
            .it_value.tv_sec = 10,
            .it_interval.tv_sec = 10,
        },
        NULL
    );
    if (-1 == result) {
        perror("aesd_server timer_settime");
    }
}

void aesd_server_init(
    struct aesd_server *self, size_t buf_size, int output_fd, bool enable_timer
) {
    self->buf_size_ = buf_size;
    self->output_fd_ = output_fd;
    pthread_mutex_init(&self->output_fd_lock_, NULL);
    self->port_ = "";
    self->sock_fd_ = -1;
    SLIST_INIT(&self->workers_);
    self->timer_ = NULL;

    if (enable_timer) {
        start_timestamp_timer(self);
    }
}

bool aesd_server_bind(struct aesd_server *self, const char *port)
{
    bool success = false;

    // Setup hints for TCP server sockets
    struct addrinfo hints = {
        .ai_family = AF_INET,           // Use IPv4
        .ai_socktype = SOCK_STREAM,     // Use a TCP socket
        .ai_flags = AI_PASSIVE,         // Set up for localhost address
    };

    // Lookup potential addresses for the server
    struct addrinfo *srv_info = NULL;
    int gai_result = getaddrinfo(NULL, port, &hints, &srv_info);
    if (gai_result != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_result));
        return false;
    }

    // Loop through potential addresses
    for (struct addrinfo *pinfo = srv_info; pinfo != NULL; pinfo = pinfo->ai_next) {
        // Try to create a socket
        int sockfd = socket(pinfo->ai_family, pinfo->ai_socktype, pinfo->ai_protocol);
        if (sockfd == -1) {
            // Failed to create the socket fd, try the next option
            perror("socket");
            continue;
        }

        // Enable address reuse to avoid "already in use" error messages
        int on = 1;
        if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
            perror("setsockopt");
            close(sockfd);
            break;
        }

        // Try to bind the socket to the address
        if (-1 == bind(sockfd, pinfo->ai_addr, pinfo->ai_addrlen)) {
            perror("bind");
            close(sockfd);
            continue;
        }

        // Good to go!
        self->sock_fd_ = sockfd;
        break;
    }

    // Free memory for getaddrinfo results
    freeaddrinfo(srv_info);

    if (success) {
        // We should have a bound socket ready to go
        self->port_ = port;
    }

    return success;
}

bool aesd_server_listen(struct aesd_server *self, int backlog)
{
    if (-1 == listen(self->sock_fd_, backlog)) {
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
    if (worker == NULL) {
        perror("malloc");
        return false;
    }
    socklen_t client_addr_len = sizeof(worker->client_addr);

    // Accept an incoming connection
    worker->client_fd = accept(self->sock_fd_, &worker->client_addr, &client_addr_len);
    if (-1 == worker->client_fd) {
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
    struct aesd_worker_entry *entry = aesd_worker_entry_new(worker);
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
    SLIST_FOREACH_SAFE(entry, &self->workers_, entries, entry_temp) {
        if (entry->worker->exited) {
            SLIST_REMOVE(&self->workers_, entry, aesd_worker_entry, entries);
            aesd_worker_entry_join(entry);
            aesd_worker_entry_delete(entry);
            entry = NULL;
        }
    }
}

void aesd_server_shutdown(struct aesd_server *self)
{
    if (self->timer_ != NULL) {
        if (-1 == timer_delete(self->timer_)) {
            perror("aesd_server timer_delete");
        }
    }
    if (-1 == close(self->sock_fd_)) {
        perror("aesd_server socket close");
    }
    struct aesd_worker_entry *entry = NULL;
    struct aesd_worker_entry *entry_temp = NULL;
    SLIST_FOREACH_SAFE(entry, &self->workers_, entries, entry_temp) {
        entry->worker->shutdown = true;
        SLIST_REMOVE(&self->workers_, entry, aesd_worker_entry, entries);
        aesd_worker_entry_join(entry);
        aesd_worker_entry_delete(entry);
        entry = NULL;
    }
    pthread_mutex_destroy(&self->output_fd_lock_);
}
