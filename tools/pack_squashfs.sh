#!/bin/sh

SQUASHFS_FILE=$1

if [ ! $SQUASHFS_FILE ]; then
	echo "$0 out_squashfs_file"
	exit 1
fi

if [ ! -d "squashfs-root" ]; then
	echo "You must unpack a squashfs file before running this command"
	exit 1
fi

rm -f "$SQUASHFS_FILE"
sudo mksquashfs squashfs-root "$SQUASHFS_FILE" -nopad -comp xz -b 262144
