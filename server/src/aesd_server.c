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
 * @brief   Try to bind the first working server address.
 *
 * @param   srvs    Potential candidates for the server address.
 *
 * @return  The bound socket fd if successful, otherwise -1.
 */
static int try_bind(struct addrinfo *srvs);

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
    memset(&self->client_addr_, 0, sizeof(self->client_addr_));
    socklen_t client_addrlen = sizeof(self->client_addr_);

    // Accept an incoming connection
    self->client_fd_ = accept(self->sock_fd_, &self->client_addr_, &client_addrlen);
    if (-1 == self->client_fd_)
    {
        perror("accept");
        return false;
    }

    // Log the client connection
    char client_ip4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &self->client_addr_.sin_addr, client_ip4_str, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip4_str);

    return true;
}

bool aesd_server_receive_data(struct aesd_server *self, int output_fd)
{
    // Seek to the end of the file
    if (-1 == lseek(output_fd, 0, SEEK_END))
    {
        perror("lseek");
        return false;
    }

    // Loop until all data has been received
    bool done = false;
    while (!done)
    {
        // Reset the buffer
        memset(self->buf_, 0, AESD_SERVER_BUF_SIZE);

        // Receive the next data chunk
        if (-1 == recv(self->client_fd_, self->buf_, AESD_SERVER_BUF_SIZE, 0))
        {
            perror("recv");
            return false;
        }

        // Default to write the whole buffer
        size_t n = AESD_SERVER_BUF_SIZE;

        // Search for newline
        char *pnewline = memchr(self->buf_, '\n', AESD_SERVER_BUF_SIZE);
        if (pnewline != NULL)
        {
            // Write only up to the newline and be done
            done = true;
            n = (size_t)(pnewline - self->buf_ + 1);
        }

        // Write to disk
        if (-1 == write(output_fd, self->buf_, n))
        {
            perror("write");
            return false;
        }
    }
    return true;
}

bool aesd_server_send_response(struct aesd_server *self, int output_fd)
{
    // Seek to the start of the file
    if (-1 == lseek(output_fd, 0, SEEK_SET))
    {
        perror("lseek");
        return false;
    }

    // Loop until the entire response has been sent
    bool done = false;
    while (!done)
    {
        ssize_t n = read(output_fd, self->buf_, AESD_SERVER_BUF_SIZE);
        if (-1 == n)
        {
            perror("read");
            return false;
        }
        else if (self->buf_[n - 1] == '\n')
        {
            done = true;
        }
        if (-1 == send(self->client_fd_, self->buf_, (size_t)n, 0))
        {
            perror("send");
            return false;
        }
    }

    return true;
}

void aesd_server_close_client(struct aesd_server *self)
{
    if (self->client_fd_ >= 0)
    {
        if (-1 == close(self->client_fd_))
        {
            perror("client socket close");
        }

        // Log the client disconnection
        char client_ip4_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &self->client_addr_.sin_addr, client_ip4_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Closed connection from %s", client_ip4_str);
    }
}

void aesd_server_close(struct aesd_server *self)
{
    if (-1 == close(self->sock_fd_))
    {
        perror("server socket close");
    }
}

static int try_bind(struct addrinfo *srvs)
{
    // Loop through potential addresses
    struct addrinfo *pinfo;
    for (pinfo = srvs; pinfo != NULL; pinfo = pinfo->ai_next)
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
        int yes = 1;
        if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
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
