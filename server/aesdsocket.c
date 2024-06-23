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

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/******************************************************************************
 * Macros and constants
 *****************************************************************************/

/** @brief  Incoming connection backlog length. */
#define BACKLOG 5
/** @brief  Output file where incoming data will be written. */
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
/** @brief  Port for the server to listen on. */
#define PORT "9000"
/** @brief  Macro to mark things (variables, parameters, return values) as unused. */
#define UNUSED(X) (void)X

/******************************************************************************
 * Static global variables
 *****************************************************************************/

/** @brief  Global flag indicating if the sever should keep running or shutdown. */
bool g_shutdown = false;

/******************************************************************************
 * Static function declarations
 *****************************************************************************/

/**
 * @brief   Create a socket fd and bind it to an address for listening.
 *
 * Exits the program with a failure status of -1 if the socket cannot be initialized.
 *
 * @return  The socket fd ready for listening.
 */
static int bind_socket(void);

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
 
    // Bind a socket for the server to use
    int srv_fd = bind_socket();

    // Try to start listening
    if (-1 == listen(srv_fd, BACKLOG))
    {
        perror("listen");
        close(srv_fd);
        return -1;
    }
    printf("server listening on port " PORT "\n");

    while (!g_shutdown)
    {
        struct sockaddr client_addr;
        socklen_t client_addrlen;
        int client_fd = accept(srv_fd, &client_addr, &client_addrlen);

        close(client_fd);
    }

    printf("shutting down");
    close(srv_fd);

    return 0;
}

/******************************************************************************
 * Static function definitions
 *****************************************************************************/

static int bind_socket(void)
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
    int result = getaddrinfo(NULL, PORT, &hints, &srv_info);
    if (result != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        return -1;
    }

    // Try to bind the first working address
    int sockfd = try_bind(srv_info);

    // Free memory for getaddrinfo results
    freeaddrinfo(srv_info);

    // If we haven't exited by this point we should have a bound socket ready to go
    return sockfd;
}

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
