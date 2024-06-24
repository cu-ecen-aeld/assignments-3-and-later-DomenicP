/**
 * @file    aesdsocket.c
 * @brief   Basic socket server for ECEA 5305 assignment 5.
 *
 * ## Requirements
 *
 *  - Open a stream socket bound to port 9000.
 *    - Fail and return -1 if any of the socket connection steps fail.
 *  - Listen for and accept a connection
 *  - Log message to the syslog "Accepted connection from xxx" where XXXX is the IP address of
 *    the connected client.
 *  - Receive data over the connection and append to the file /var/tmp/aesdsocketdata, creating
 *    this file if it doesn't exist.
 *    - Use a newline to separate data packets received.
 *    - Assume the data stream does not include null characters.
 *    - Assume the length of the packet will be shorter than the available heap size.
 *  - Returns the full content of /var/tmp/aesdsocketdata to the client as soon as the received
 *    data packet completes.
 *    - Assume the total size of all packets sent will be less than the size of the root
 *      filesystem.
 *    - Do not assume this total size of all packets sent will be less than the size of the
 *      available RAM for the process heap
 *  - Log message to the syslog "Closed connection from XXX" where XXX is the IP address of the
 *    connected client.
 *  - Restarts accepting connections from new clients forever in a loop until SIGINT or SIGTERM
 *    is received.
 *    - Gracefully exits when SIGINT or SIGTERM is received, completing any open connection
 *      operations, closing any open sockets, and **deleting the file /var/tmp/aesdsocketdata.**
 *    - Logs message to the syslog "Caught signal, exiting" when SIGINT or SIGTERM is received.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/******************************************************************************
 * Macros and constants
 *****************************************************************************/

/** @brief  Incoming connection backlog length. */
#define BACKLOG 5U
/** @brief  Size of the working buffer. */
#define BUF_SIZE 256U
/** @brief  Output file where incoming data will be written. */
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
/** @brief  Port for the server to listen on. */
#define PORT "9000"
/** @brief  Macro to mark things (variables, parameters, return values) as unused. */
#define UNUSED(X) (void)(X)

/******************************************************************************
 * Static global variables
 *****************************************************************************/

/** @brief  AESD server application. */
struct aesd_server
{
    /** @brief  Working buffer for client IO. */
    char buf_[BUF_SIZE];
    /** @brief  Socket fd for the server. */
    int sock_fd_;
    /** @brief  Address information for the client. */
    struct sockaddr_in client_addr_;
    /** @brief  Socket fd for the client. */
    int client_fd_;
};

/******************************************************************************
 * Static global variables
 *****************************************************************************/

/** @brief  Global flag indicating if the sever should keep running or shutdown. */
bool g_shutdown = false;

/******************************************************************************
 * Static function declarations
 *****************************************************************************/

/**
 * @brief   Register a signal with the program-specific signal handler.
 *
 * Exits the program with a failure status of -1 if the system call fails.
 *
 * @param   signal  The signal to be handled.
 */
static void handle(int signal);

/**
 * @brief   Program-specific signal handler.
 *
 * For SIGINT and SIGTERM set a global shutdown flag.
 *
 * @param   signal  The signal to be handled.
 */
static void signal_handler(int signal);

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
static bool aesd_server_bind(struct aesd_server *self, const char *port);

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
static bool aesd_server_listen(struct aesd_server *self, int backlog);

/**
 * @brief   Block and wait to accept an incoming client.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static bool aesd_server_accept_client(struct aesd_server *self);

/**
 * @brief   Receive data from the client and append it to the output file.
 *
 * Continue receiving data until a newline character is reached.
 *
 * @param   self
 * @param   output_fd   Output file where data should be written.
 *
 * @return true if successful, false otherwise.
 */
static bool aesd_server_receive_data(struct aesd_server *self, int output_fd);

/**
 * @brief   Send back the current data in the output file to the client.
 *
 * The output file should be opened in RW mode to enable readback in this function.
 *
 * @param   self
 * @param   output_fd   Output file where data should be read.
 *
 * @return true if successful, false otherwise.
 */
static bool aesd_server_send_response(struct aesd_server *self, int output_fd);

/**
 * @brief   Close the client connection and log to syslog.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static void aesd_server_close_client(struct aesd_server *self);

/**
 * @brief   Close the listening server socket.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static inline void aesd_server_close(struct aesd_server *self)
{
    if (-1 == close(self->sock_fd_))
    {
        perror("server socket close");
    }
}

/**
 * @brief   Try to bind the first working server address.
 *
 * @param   srvs    Potential candidates for the server address.
 *
 * @return  The bound socket fd if successful, otherwise -1.
 */
static int try_bind(struct addrinfo *srvs);

/******************************************************************************
 * Extern function definitions
 *****************************************************************************/

int main(int argc, const char **argv)
{
    UNUSED(argc);
    UNUSED(argv);

    // Initialize syslog
    openlog("aesdsocket", 0, LOG_USER);

    // Register signal handlers for SIGINT and SIGTERM
    handle(SIGINT);
    handle(SIGTERM);
 
    struct aesd_server srv;
    if (!(aesd_server_bind(&srv, PORT) && aesd_server_listen(&srv, BACKLOG)))
    {
        fprintf(stderr, "could not initialize server\n");
        return -1;
    }

    // Open the output file
    int output_fd = open(OUTPUT_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);

    while (!g_shutdown)
    {
        if (!aesd_server_accept_client(&srv))
        {
            fprintf(stderr, "client not accepted\n");
            continue;
        }

        if (!aesd_server_receive_data(&srv, output_fd))
        {
            fprintf(stderr, "error receiving client data\n");
        }
        else if (!aesd_server_send_response(&srv, output_fd))
        {
            fprintf(stderr, "error sending client response\n");
        }

        aesd_server_close_client(&srv);
    }

    printf("shutting down\n");

    // Close and delete the output file
    close(output_fd);
    unlink(OUTPUT_FILE);

    // Close the server socket
    aesd_server_close(&srv);

    return 0;
}

/******************************************************************************
 * Static function definitions
 *****************************************************************************/

static void handle(int sig)
{
    int result = sigaction(
        sig,
        &(struct sigaction) {
            .sa_handler = signal_handler,
        },
        NULL
    );
    if (-1 == result)
    {
        perror("sigaction");
        exit(-1);
    }
}

static void signal_handler(int sig)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    if (sig == SIGINT || sig == SIGTERM)
    {
        g_shutdown = true;
    }
    else
    {
        fprintf(stderr, "received unhandled signal %d\n", sig);
        exit(-1);
    }
}

static bool aesd_server_bind(struct aesd_server *self, const char *port)
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
    return true;
}

static bool aesd_server_listen(struct aesd_server *self, int backlog)
{
    if (-1 == listen(self->sock_fd_, backlog))
    {
        perror("listen");
        close(self->sock_fd_);
        return false;
    }
    printf("server listening on port " PORT "\n");
    return true;
}

static bool aesd_server_accept_client(struct aesd_server *self)
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

static bool aesd_server_receive_data(struct aesd_server *self, int output_fd)
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
        memset(self->buf_, 0, BUF_SIZE);

        // Receive the next data chunk
        if (-1 == recv(self->client_fd_, self->buf_, BUF_SIZE, 0))
        {
            perror("recv");
            return false;
        }

        // Default to write the whole buffer
        size_t n = BUF_SIZE;

        // Search for newline
        char *pnewline = memchr(self->buf_, '\n', BUF_SIZE);
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

static bool aesd_server_send_response(struct aesd_server *self, int output_fd)
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
        ssize_t n = read(output_fd, self->buf_, BUF_SIZE);
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

static void aesd_server_close_client(struct aesd_server *self)
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
