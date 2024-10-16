#include "aesdsocket/aesd_worker.h"
#include "aesdsocket/aesd_ioctl.h"

#include <fcntl.h>
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
static bool receive_data(struct aesd_worker *self, int output_fd)
{
    bool error = false;

    // Loop until all data has been received
    bool done = false;
    while (!done && !self->shutdown) {
        // Reset the buffer
        memset(self->buf_, 0, self->buf_size_);

        // Receive the next data chunk
        if (-1 == recv(self->client_fd, self->buf_, self->buf_size_, 0)) {
            perror("worker recv");
            error = true;
            goto out;
        }

        // Default to write the whole buffer
        size_t n = self->buf_size_;

        // Search for newline
        char *search_result = memchr(self->buf_, '\n', self->buf_size_);
        if (search_result != NULL) {
            // Check for in-band seek command
            struct aesd_seekto seekto = {0};
            int match_count = sscanf(
                self->buf_,
                "AESDCHAR_IOCSEEKTO:%u,%u\n",
                &seekto.write_cmd,
                &seekto.write_cmd_offset
            );
            if (match_count == 2) {
                syslog(
                    LOG_NOTICE,
                    "ioctl AESDCHAR_IOCSEEKTO %u %u",
                    seekto.write_cmd,
                    seekto.write_cmd_offset
                );
                ioctl(output_fd, AESDCHAR_IOCSEEKTO, &seekto);
                goto out;
            }

            // Write only up to the newline and be done
            done = true;
            n = (size_t)(search_result - self->buf_ + 1);
        }

        // Write to disk
        if (-1 == write(output_fd, self->buf_, n)) {
            perror("worker write");
            error = true;
            goto out;
        }
        if (-1 == lseek(output_fd, 0, SEEK_SET)) {
            perror ("worker lseek");
        }
    }

out:
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
static bool send_response(struct aesd_worker *self, int output_fd)
{
    bool result = false;

    // Loop until the entire response has been sent
    bool done = false;
    while (!done && !self->shutdown) {
        ssize_t n = read(output_fd, self->buf_, self->buf_size_);
        if (-1 == n) {
            perror("worker read");
            goto out;
        }
        else if (0 == n) {
            done = true;
        }
        if (-1 == send(self->client_fd, self->buf_, (size_t)n, 0)) {
            perror("worker send");
            goto out;
        }
    }

    result = !self->shutdown;

out:
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
        syslog(LOG_NOTICE, "closed connection from %s", client_ip4_str);
    }
}

struct aesd_worker *aesd_worker_new(
    size_t buf_size, bool char_dev, const char *output_path, pthread_mutex_t *output_fd_lock
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
    self->output_lock_ = output_fd_lock;
    self->output_path_ = output_path;

    return self;
}

void *aesd_worker_main(void *arg)
{
    struct aesd_worker *self = arg;

    int output_fd = open(self->output_path_, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (-1 == output_fd) {
        perror("worker open file");
        goto out_close_client;
    }
    pthread_mutex_lock(self->output_lock_);
    if (!receive_data(self, output_fd)) {
        fprintf(stderr, "error receiving client data\n");
        goto out_unlock_mutex;
    }
    if (!send_response(self, output_fd)) {
        fprintf(stderr, "error sending client response\n");
        goto out_unlock_mutex;
    }
out_unlock_mutex:
    pthread_mutex_unlock(self->output_lock_);
    if (-1 == close(output_fd)) {
        perror("worker close file");
    }

out_close_client:
    close_client(self);
    self->exited = true;
    pthread_exit(NULL);
}
