/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "aesdchar.h"

MODULE_AUTHOR("DomenicP");
MODULE_LICENSE("Dual BSD/GPL");

int g_aesd_major = 0; // use dynamic major
int g_aesd_minor = 0;

struct aesd_dev g_aesd_device = {0};

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t result = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->entry_lock)) {
        return -ERESTARTSYS;
    }

    // Search the buffer for the entry corresponding to the file position
    size_t offset = 0;
    struct aesd_buffer_entry *entry
        = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buf, *f_pos, &offset);

    if (entry == NULL) {
        // Offset is past the end of data (EOF)
        result = 0;
        goto out;
    }

    size_t read_count = entry->size - offset;
    if (copy_to_user(buf, entry->buffptr + offset, read_count)) {
        result = -EFAULT;
        goto out;
    }

    *f_pos += read_count;

out:
    mutex_unlock(&dev->buf_lock);
    return result;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t result = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->entry_lock)) {
        return -ERESTARTSYS;
    }

    // Check for previous entry
    if (dev->entry.buffptr == NULL) {
        // Allocate a fresh buffer since there's no data from a previous write
        dev->entry.buffptr = kzalloc(count, GFP_KERNEL);
        if (dev->entry.buffptr == NULL) {
            result = -ENOMEM;
            goto out;
        }
        if (copy_from_user(dev->entry.buffptr, buf, count)) {
            result = -EFAULT;
            goto out;
        }
    } else {
        // Allocate a larger buffer to append to the previous write
        char *new_buf = kzalloc(dev->entry.size + count, GFP_KERNEL);
        if (new_buf == NULL) {
            result = -ENOMEM;
            goto out;
        }
        // Copy over the previous data and swap the buffer pointer
        memcpy(new_buf, dev->entry.buffptr, dev->entry.size);
        kfree(dev->entry.buffptr);
        dev->entry.buffptr = new_buf;
        // Copy in the new data from the user
        if (copy_from_user(new_buf + dev->entry.size, buf, count)) {
            result = -EFAULT;
            goto out;
        }
    }
    dev->entry.size += count;
    result = count;

    // Check for newline to mark the end of the entry
    if (mutex_lock_interruptible(&dev->buf_lock)) {
        result = -EINTR;
        goto out;
    }
    if (dev->entry.buffptr[dev->entry.size - 1] == '\n') {
        char *old_data = aesd_circular_buffer_add_entry(&dev->buf, &dev->entry);
        // Clean up old entry data dropped from the buffer
        if (old_data != NULL) {
            kfree(old_data);
        }
        // Reset the entry for the next write
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }
    mutex_unlock(&dev->buf_lock);

out:
    mutex_unlock(&dev->entry_lock);
    return result;
}

struct file_operations g_aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int devno = MKDEV(g_aesd_major, g_aesd_minor);
    cdev_init(&dev->cdev, &g_aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &g_aesd_fops;
    int err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result = alloc_chrdev_region(&dev, g_aesd_minor, 1, "aesdchar");
    g_aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", g_aesd_major);
        return result;
    }

    memset(&g_aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&g_aesd_device.buf);
    mutex_init(&g_aesd_device.buf_lock);
    mutex_init(&g_aesd_device.entry_lock);

    result = aesd_setup_cdev(&g_aesd_device);
    if (result) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(g_aesd_major, g_aesd_minor);

    // Free any remaining buffer entries
    int i;
    struct aesd_buffer_entry *entry = NULL;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &g_aesd_device.buf, i) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }

    cdev_del(&g_aesd_device.cdev);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
