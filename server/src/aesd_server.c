/**
 * @file    aesd_server.c
 * @brief   AESD server application.
 */

#define _GNU_SOURCE

#include "aesdsocket/aesd_server.h"

#include <fcntl.h>
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
 * @brief   Create a socket fd and bind it to an address for listening.
 *
 * Exits the program with a failure status of -1 if the socket cannot be initialized.
 *
 * @param   self
 * @param   port    The port that the server should listen on.
 *
 * @return  true if the socket was bound, false otherwise.
 */
static bool srv_bind(struct aesd_server *self, const char *port)
{
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
    self->sock_fd_ = -1;
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

    if (self->sock_fd_ != -1) {
        // We should have a bound socket ready to go
        self->port_ = port;
        return true;
    }
    return false;
}

/**
 * @brief   Start listening for incoming connections.
 *
 * @param   self
 * @param   backlog     Connection backlog length.
 *
 * @return  true if successful, false otherwise.
 */
static bool srv_listen(struct aesd_server *self, int backlog)
{
    if (-1 == listen(self->sock_fd_, backlog)) {
        perror("listen");
        close(self->sock_fd_);
        return false;
    }
    printf("server listening on port %s\n", self->port_);
    syslog(LOG_INFO, "server listening on port %s", self->port_);
    return true;
}

/**
 * @brief   Block and wait to accept an incoming client.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static bool accept_client(struct aesd_server *self)
{
    // Open the shared file handle if it isn't open yet
    pthread_mutex_lock(&self->output_fd_lock_);
    if (self->output_fd_ == -1) {
        self->output_fd_ = open(self->output_path_, O_RDWR | O_CREAT | O_TRUNC, 0644);
    }
    int output_fd = self->output_fd_;
    pthread_mutex_unlock(&self->output_fd_lock_);
    if (-1 == output_fd) {
        perror("open output file");
        return false;
    }

    // Create a new worker
    struct aesd_worker *worker = aesd_worker_new(
        self->buf_size_, self->output_fd_, &self->output_fd_lock_
    );
    if (worker == NULL) {
        fprintf(stderr, "could not allocate worker\n");
        return false;
    }

    // Accept an incoming connection
    socklen_t client_addr_len = sizeof(worker->client_addr);
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
    syslog(LOG_INFO, "accepted connection from %s", client_ip4_str);

    // Allocate a worker list entry and move ownership of the worker pointer
    struct aesd_worker_entry *entry = aesd_worker_entry_new(worker);
    worker = NULL;

    // Start the worker thread
    if (!aesd_worker_entry_start(entry)) {
        fprintf(stderr, "could not start worker thread\n");
        if (-1 == close(worker->client_fd)) {
            perror("client close");
        }
        aesd_worker_delete(entry->worker);
        return false;
    }

    // Add to the worker list
    SLIST_INSERT_HEAD(&self->workers_, entry, entries);
    entry = NULL;
    return true;
}

/**
 * @brief   Check worker threads and free resources.
 *
 * @param   self
 */
static void check_workers(struct aesd_server *self)
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

/**
 * @brief   Close the listening server socket and stop worker threads.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static void srv_shutdown(struct aesd_server *self)
{
    // Stop the timer if running
    if (self->timer_ != NULL) {
        if (-1 == timer_delete(self->timer_)) {
            perror("aesd_server timer_delete");
        }
    }

    // Close the server socket
    if (-1 == close(self->sock_fd_)) {
        perror("aesd_server socket close");
    }

    // Join workers
    struct aesd_worker_entry *entry = NULL;
    struct aesd_worker_entry *entry_temp = NULL;
    SLIST_FOREACH_SAFE(entry, &self->workers_, entries, entry_temp) {
        entry->worker->shutdown = true;
        SLIST_REMOVE(&self->workers_, entry, aesd_worker_entry, entries);
        aesd_worker_entry_join(entry);
        aesd_worker_entry_delete(entry);
        entry = NULL;
    }

    // Close the output file
    if (-1 == close(self->output_fd_)) {
        perror("close output file");
    }
    // Delete the output if not a device
    if (!self->char_dev_) {
        if (-1 == unlink(self->output_path_)) {
            perror("unlink output file");
        }
    }

    pthread_mutex_destroy(&self->output_fd_lock_);
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
        perror("timestamp timer sigaction");
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
        perror("timestamp timer_create");
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
        perror("timestamp timer_settime");
    }
}

void aesd_server_init(
    struct aesd_server *self, size_t buf_size, const char *output_path, bool char_dev
) {
    self->running = false;
    self->buf_size_ = buf_size;
    self->char_dev_ = char_dev;
    self->output_fd_ = -1;
    self->output_path_ = output_path;
    pthread_mutex_init(&self->output_fd_lock_, NULL);
    self->port_ = "";
    self->sock_fd_ = -1;
    self->timer_ = NULL;
    SLIST_INIT(&self->workers_);

    if (!char_dev) {
        start_timestamp_timer(self);
    }
}

int aesd_server_run(struct aesd_server *self, const char *port, int backlog)
{
    // Try to bind the server address and port
    if (!srv_bind(self, port)) {
        fprintf(stderr, "server could not bind to port %s\n", port);
        return -1;
    }

    // Start listening for connections
    if (!srv_listen(self, backlog)) {
        fprintf(stderr, "server could not start listening\n");
        return -1;
    }

    self->running = true;
    while (self->running) {
        if (!accept_client(self)) {
            fprintf(stderr, "client not accepted\n");
            continue;
        }
        check_workers(self);
    }

    printf("shutting down\n");

    // Shutdown the server
    srv_shutdown(self);

    return 0;
}
