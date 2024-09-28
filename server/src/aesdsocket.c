/**
 * @file    aesdsocket.c
 * @brief   Basic socket server for ECEA 5305 assignment 5.
 *
 * ## Requirements (Assignment 5)
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
 *
 * ## Requirements (Assignment 6)
 *
 * - Modify the program to accept multiple simultaneous connections, with each connection spawning
 *   a new thread to handle the connection.
 *   - Synchronize writes to /var/tmp/aesdsocketdata using a mutex (not relying on file system
 *     synchronization).
 *   - Threads should exit when the connection is closed by the client or on send/receive errors.
 *   - The program should gracefully exit when SIGTERM / SIGNINT is received
 *     - The program should request each thread exit and wait for completion.
 *   - Manage the threads with a singly linked list.
 * - Append a timestamp in the form "timestamp:<time>" where <time> is specified by RFC 2822
 *   compliant strftime format followed by a newline.
 *   - The timestamp string should be appended to /var/tmp/aesdsocketdata every 10 seconds.
 *   - Use appropriate locking to synchronize the file access.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "aesdsocket/aesd_server.h"

/******************************************************************************
 * Macros and constants
 *****************************************************************************/

/** @brief  Incoming connection backlog length. */
#define BACKLOG 5U
/** @brief  Output file where incoming data will be written. */
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
/** @brief  Port for the server to listen on. */
#define PORT "9000"
/** @brief  Macro to mark things (variables, parameters, return values) as unused. */
#define UNUSED(X) (void)(X)

/******************************************************************************
 * Static global variables
 *****************************************************************************/

/** @brief  Global flag indicating if the sever should keep running or shutdown. */
static bool g_shutdown = false;

/******************************************************************************
 * Static function declarations
 *****************************************************************************/

/**
 * @brief   Setup the process to run as a daemon.
 *
 * This function should be called from the child process after forking.
 *
 * @return  0 on success, -1 on error.
 */
static int daemonize(void);

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

/******************************************************************************
 * Extern function definitions
 *****************************************************************************/

int main(int argc, const char **argv)
{
    // Check for daemon mode
    bool daemon = (argc >= 2) && (strcmp("-d", argv[1]) == 0);
    if (daemon)
    {
        // Try to fork the process
        pid_t pid = fork();
        if (-1 == pid)
        {
            perror("fork");
            return -1;
        }
        // Check for exit if parent process
        if (pid != 0)
        {
            printf("server daemon started with PID=%d\n", pid);
            exit(0);
        }
        if (-1 == daemonize())
        {
            fprintf(stderr, "Could not daemonize process");
            return -1;
        }
    }

    // Initialize syslog
    openlog("aesdsocket", 0, LOG_USER);

    // Register signal handlers for SIGINT and SIGTERM
    handle(SIGINT);
    handle(SIGTERM);

    // Try to bind the server address and port
    struct aesd_server srv;
    if (!aesd_server_bind(&srv, PORT))
    {
        fprintf(stderr, "server could not bind\n");
        return -1;
    }

    // Start listening for connections
    if (!aesd_server_listen(&srv, BACKLOG))
    {
        fprintf(stderr, "server could not start listening\n");
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

static int daemonize(void)
{
    // Create a session and assign this process as the session leader
    if (-1 == setsid())
    {
        perror("setsid");
        return -1;
    }
    // Change directories to the filesystem root
    if (-1 == chdir("/"))
    {
        perror("chdir");
        return -1;
    }
    // Redirect stdio to /dev/null
    int dev_null = open("/dev/null", O_RDWR);
    if (-1 == dup2(dev_null, STDIN_FILENO))
    {
        perror("dup2: stdin > /dev/null");
        return -1;
    }
    if (-1 == dup2(dev_null, STDOUT_FILENO))
    {
        perror("dup2: stdout > /dev/null");
        return -1;
    }
    if (-1 == dup2(dev_null, STDERR_FILENO))
    {
        perror("dup2: stderr > /dev/null");
        return -1;
    }
    return 0;
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
