/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param   buffer The buffer to search for corresponding offset. Any necessary locking must be
 *              performed by caller.
 * @param   char_offset The position to search for in the buffer list, describing the zero
 *              referenced character index if all buffer strings were concatenated end to end.
 * @param   entry_offset_byte_rtn A pointer specifying a location to store the byte of the returned
 *              aesd_buffer_entry buffptr member corresponding to char_offset. This value is only
 *              set when a matching char_offset is found in aesd_buffer.
 * @return  The struct aesd_buffer_entry structure representing the position described by
 *              char_offset, or NULL if this position is not available in the buffer (not enough
 *              data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer, size_t char_offset, size_t *entry_offset_byte_rtn
) {
    // Running sum of the size of entries
    size_t search_offset = 0;

    // Start at the output offset and search until we wrap back around
    uint8_t i = buffer->out_offs;
    do {
        // Increase the search sum
        search_offset += buffer->entry[i].size;

        // Check if the sum is large enough to find the result
        if (search_offset > char_offset) {
            // Subtract off the most recent entry size to find the relative offset into this entry
            search_offset -= buffer->entry[i].size;
            *entry_offset_byte_rtn = char_offset - search_offset;

            // Return a pointer to the entry
            return &buffer->entry[i];
        }

        // Modular increment to next buffer entry
        i = (i + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (i != buffer->out_offs);
    return NULL;
}

/**
* @brief    Adds entry `entry` to `buffer` in the location specified in `buffer->in_offs`.
*
* - If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to
*   the new start location. The oldest entry will be copied to `out_entry` before overwriting.
* - Any necessary locking must be handled by the caller.
* - Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime
*   managed by the caller.
*
* @return   Pointer to data from overwritten entry to be freed by the user.
*/
char *aesd_circular_buffer_add_entry(
    struct aesd_circular_buffer *buffer,
    const struct aesd_buffer_entry *entry
) {
    char *result = NULL;
    if (buffer->full) {
        result = buffer->entry[buffer->out_offs].buffptr;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    buffer->entry[buffer->in_offs] = *entry;
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }
    return result;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
