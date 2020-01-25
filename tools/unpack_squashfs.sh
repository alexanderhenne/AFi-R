#!/bin/sh

SQUASHFS_FILE=$1

if [ ! $SQUASHFS_FILE ]; then
	echo "$0 squashfs_file"
	exit 1
fi

if [ ! -f $SQUASHFS_FILE ]; then
	echo "squashfs_file can't be accessed"
	exit 1
fi

sudo rm -rf squashfs-root
sudo unsquashfs "$SQUASHFS_FILE"
