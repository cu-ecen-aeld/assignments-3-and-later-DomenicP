/**
 * @file    writer.c
 * @brief   Write string data to files.
 * @author  Domenic Rodriguez
 *
 * ## Requirements
 *
 * - First argument `writefile` is a full path to a file (including filename) on the filesystem.
 * - Second argument `writestr` is a text string which will be written within this file.
 * - Exit with value 1 and print error to syslog if arguments above are not specified.
 * - Create a new file with name and path `writefile` with content `writestr, overwriting any
 *   existing file. Assume the directory path already exists.
 * - Exit with value 1 and print error to syslog if the file cannot be created.
 * - Setup syslog logging using the `LOG_USER` facility.
 * - Use syslog to write a `LOG_DEBUG` message for the string being written to the file.
 * - Use syslog to write `LOG_ERR` messages for any unexpected errors.
 */

// Enable POSIX API features
#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief   Check command line arguments.
 *
 * If arguments are incorrect, print a usage message and exit.
 *
 * @param   argc    Number of command line arguments (including the program name as
 *                  argument 0).
 * @param   argv    Command line argument string array.
 */
static void check_args(int argc, const char **argv);

/**
 * @brief   Create a file at the specified path and open for writing.
 *
 * If the file cannot be created then an error message will be printed to syslog and the program
 * will exit.
 *
 * @param   path    File path to be created.
 *
 * @return  The file descriptor if successful.
 */
static int create_file(const char *path);

/**
 * @brief   Try to write a string to a file.
 *
 * This function assumes that the string data is well formatted with a terminating `'\0'` and uses
 * `strlen()` to get the string length. Do not call this function with untrusted data.
 *
 * @param   fd      The file descriptor to write to.
 * @param   str     The string data to be written.
 *
 * @return  `0` if successful, otherwise the `errno` value from a failed `write()` system call.
 */
static int try_write(int fd, const char *str);

/**
 * @brief   Program entrypoint.
 *
 * @param   argc    Number of command line arguments (including the program name as
 *                  argument 0).
 * @param   argv    Command line argument string array.
 *
 * @return  Program exit status.
 */
int main(int argc, const char **argv)
{
    // Assume success until something goes wrong
    int exit_status = EXIT_SUCCESS;

    // Configure syslog for LOG_USER
    openlog("writer", 0, LOG_USER);

    // Parse arguments
    check_args(argc, argv);
    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // Open the file for writing
    int fd = create_file(writefile);

    // Write to the file
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    int result = try_write(fd, writestr);
    if (result != 0)
    {
        syslog(LOG_ERR, "could not write to file: %s", strerror(result));
        exit_status = EXIT_FAILURE;
        // Don't exit yet because we should still close the file.
    }

    // Close the file
    if (close(fd) == -1)
    {
        syslog(LOG_ERR, "could not close file: %s", strerror(errno));
        exit_status = EXIT_FAILURE;
    }

    // Exit
    return exit_status;
}

static void check_args(int argc, const char **argv)
{
    if (argc != 3)
    {
        syslog(LOG_ERR, "expected 2 arguments but received %d", argc - 1);
        printf("Usage: %s WRITEFILE WRITESTR\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

static int create_file(const char *path)
{
    // Shorthand for `open()` with `O_WRONLY | O_CREAT | O_TRUNC`
    int fd = creat(path, 0644);
    if (fd == -1)
    {
        syslog(LOG_ERR, "could not open %s: %s", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fd;
}

static int try_write(int fd, const char *str)
{
    // Get the total number of characters to write
    size_t total = strlen(str);

    // Initialize the string pointer
    const char *pstr = str;

    // Loop until all characters are written
    size_t remaining = total;
    while (remaining > 0)
    {
        // Attempt a write
        ssize_t written = write(fd, pstr, remaining);

        // If something went wrong, check errno
        if (written == -1)
        {
            // Retry after signal interrupt
            if (errno == EINTR)
            {
                continue;
            }
            // Fatal error, return errno to caller to handle
            return errno;
        }

        // Otherwise subtract the remaining count and advance the string pointer
        remaining -= (size_t)written;
        pstr += written;
    }

    // No error, return success
    return 0;
}
