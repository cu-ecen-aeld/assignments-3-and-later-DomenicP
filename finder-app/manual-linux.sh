#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.
# Updated by @DomenicP on 2024-06-18 for assignment 3 part 2
# vim: ts=4 sw=4

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath "$(dirname "$0")")
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p "$OUTDIR"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here

    # Deep clean the kernel build tree
    echo "Cleaning the kernel"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper
    # Clean any files untracked by Git
    echo "Cleaning git"
    git restore .
    git clean -fdx
    # Generate the .config file
    echo "Generating kernel config"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
    # Apply kernel patch for YYLOC bug
    echo "Patching YYLOC bug"
    git apply "${FINDER_APP_DIR}/e33a814e-remove-redundant-ylloc.patch"
    # Build a kernel image for booting with QEMU
    echo "Building kernel"
    make -j4 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all
    # Build any kernel modules
    echo "Building modules"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules
    # Build the device tree
    echo "Building device tree"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs

    echo "Kernel build finished"
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/Image"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf "${OUTDIR}/rootfs"
fi

# TODO: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
make \
    CONFIG_PREFIX="${OUTDIR}/rootfs" \
    ARCH=$ARCH \
    CROSS_COMPILE=$CROSS_COMPILE \
    install

# TODO: Add library dependencies to rootfs
cd "${OUTDIR}/rootfs"
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
cp "${SYSROOT}/lib/ld-linux-aarch64.so.1" lib/
cp "${SYSROOT}/lib64/libc.so.6" lib64/
cp "${SYSROOT}/lib64/libm.so.6" lib64/
cp "${SYSROOT}/lib64/libresolv.so.2" lib64/

# TODO: Make device nodes
cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd "$FINDER_APP_DIR"
make clean
make CROSS_COMPILE=$CROSS_COMPILE

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd "${OUTDIR}/rootfs"
mkdir -p home/conf
cp "${FINDER_APP_DIR}/autorun-qemu.sh" home/
cp "${FINDER_APP_DIR}/conf/username.txt" home/conf/
cp "${FINDER_APP_DIR}/conf/assignment.txt" home/conf/
cp "${FINDER_APP_DIR}/finder.sh" home/
cp "${FINDER_APP_DIR}/finder-test.sh" home/
cp "${FINDER_APP_DIR}/writer" home/

# TODO: Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

# TODO: Create initramfs.cpio.gz
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
cd "${OUTDIR}"
gzip -f initramfs.cpio
