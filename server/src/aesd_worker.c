#include "aesdsocket/aesd_worker.h"

#include <string.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief   Receive data from the client and append it to the output file.
 *
 * Continue receiving data until a newline character is reached.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static bool receive_data(struct aesd_worker *self)
{
    // START CRITICAL REGION: output_fd
    pthread_mutex_lock(self->output_fd_lock_);

    // Loop until all data has been received
    bool done = false;
    bool error = false;
    while (!done && !self->shutdown) {
        // Reset the buffer
        memset(self->buf_, 0, self->buf_size_);

        // Receive the next data chunk
        if (-1 == recv(self->client_fd, self->buf_, self->buf_size_, 0)) {
            perror("recv");
            error = true;
            break;
        }

        // Default to write the whole buffer
        size_t n = self->buf_size_;

        // Search for newline
        char *pnewline = memchr(self->buf_, '\n', self->buf_size_);
        if (pnewline != NULL) {
            // Write only up to the newline and be done
            done = true;
            n = (size_t)(pnewline - self->buf_ + 1);
        }

        // Seek to the end of the file
        if (!self->char_dev_) {
            if (-1 == lseek(self->output_fd_, 0, SEEK_END)) {
                perror("lseek");
                error = true;
                break;
            }
        }
        // Write to disk
        if (-1 == write(self->output_fd_, self->buf_, n)) {
            perror("write");
            error = true;
            break;
        }
    }

    pthread_mutex_unlock(self->output_fd_lock_);
    // END CRITICAL REGION: output_fd

    return !error && !self->shutdown;
}

/**
 * @brief   Send back the current data in the output file to the client.
 *
 * The output file should be opened in RW mode to enable readback in this function.
 *
 * @param   self
 *
 * @return true if successful, false otherwise.
 */
static bool send_response(struct aesd_worker *self)
{
    bool result = false;

    // START CRITICAL REGION: output_fd
    pthread_mutex_lock(self->output_fd_lock_);

    if (!self->char_dev_) {
        // Seek to the start of the file
        if (-1 == lseek(self->output_fd_, 0, SEEK_SET)) {
            perror("lseek");
            goto out;
        }
    }

    // Loop until the entire response has been sent
    bool done = false;
    while (!done && !self->shutdown) {
        ssize_t n = read(self->output_fd_, self->buf_, self->buf_size_);
        if (-1 == n) {
            perror("read");
            goto out;
        }
        else if (self->buf_[n - 1] == '\n') {
            done = true;
        }
        if (-1 == send(self->client_fd, self->buf_, (size_t)n, 0)) {
            perror("send");
            goto out;
        }
    }

    result = !self->shutdown;

out:
    pthread_mutex_unlock(self->output_fd_lock_);
    // END CRITICAL REGION: output_fd

    return result;
}

/**
 * @brief   Close the client connection and log to syslog.
 *
 * @param   self
 */
static void close_client(struct aesd_worker *self)
{
    if (self->client_fd >= 0) {
        if (-1 == close(self->client_fd)) {
            perror("client socket close");
        }

        // Log the client disconnection
        char client_ip4_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &self->client_addr.sin_addr, client_ip4_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Closed connection from %s", client_ip4_str);
    }
}

struct aesd_worker *aesd_worker_new(
    size_t buf_size, bool char_dev, int output_fd, pthread_mutex_t *output_fd_lock
) {
    // Allocate self
    struct aesd_worker *self = (struct aesd_worker *)malloc(sizeof(struct aesd_worker));
    if (self == NULL) {
        perror("malloc aesd_worker");
        return NULL;
    }

    // Allocate buf
    self->buf_ = (char *)malloc(buf_size);
    if (self->buf_ == NULL) {
        perror("malloc aesd_worker buf");
        free(self);
        self = NULL;
        return NULL;
    }

    // Initialize remaining members
    self->buf_size_ = buf_size;
    memset(&self->client_addr, 0, sizeof(self->client_addr));
    self->client_fd = -1;
    self->exited = false;
    self->shutdown = false;
    self->char_dev_ = char_dev;
    self->output_fd_ = output_fd;
    self->output_fd_lock_ = output_fd_lock;

    return self;
}

void *aesd_worker_main(void *arg)
{
    struct aesd_worker *self = arg;
    if (!receive_data(self)) {
        fprintf(stderr, "error receiving client data\n");
    }
    else if (!send_response(self)) {
        fprintf(stderr, "error sending client response\n");
    }
    close_client(self);
    self->exited = true;
    pthread_exit(NULL);
}
