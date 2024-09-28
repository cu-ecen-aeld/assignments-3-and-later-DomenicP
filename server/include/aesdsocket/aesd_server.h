/**
 * @file    aesd_server.h
 * @brief   AESD server application.
 */

#ifndef AESDSOCKET__AESD_SERVER_H_
#define AESDSOCKET__AESD_SERVER_H_

#include <arpa/inet.h>
#include <stdbool.h>

/** @brief  Size of the working buffer. */
#define AESD_SERVER_BUF_SIZE 256U

/** @brief  AESD server application. */
struct aesd_server
{
    /** @brief  Working buffer for client IO. */
    char buf_[AESD_SERVER_BUF_SIZE];
    /** @brief  Port on which the server is listening. */
    const char *port_;
    /** @brief  Socket fd for the server. */
    int sock_fd_;
    /** @brief  Address information for the client. */
    struct sockaddr_in client_addr_;
    /** @brief  Socket fd for the client. */
    int client_fd_;
};


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
 * @brief   Receive data from the client and append it to the output file.
 *
 * Continue receiving data until a newline character is reached.
 *
 * @param   self
 * @param   output_fd   Output file where data should be written.
 *
 * @return true if successful, false otherwise.
 */
bool aesd_server_receive_data(struct aesd_server *self, int output_fd);

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
bool aesd_server_send_response(struct aesd_server *self, int output_fd);

/**
 * @brief   Close the client connection and log to syslog.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
void aesd_server_close_client(struct aesd_server *self);

/**
 * @brief   Close the listening server socket.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
void aesd_server_close(struct aesd_server *self);

#endif  // AESDSOCKET__AESD_SERVER_H_
