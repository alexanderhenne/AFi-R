#!/bin/bash

VANILLA_FIRMWARE=$1
SQUASHFS_FILE=$2

if [ ! $VANILLA_FIRMWARE ] || [ ! $SQUASHFS_FILE ]; then
	echo "$0 vanilla_firmware output_squashfs_file"
	exit 1
fi

if [ ! -f $VANILLA_FIRMWARE ]; then
	echo "vanilla_firmware file can't be accessed"
	exit 1
fi

OUTPUT_SQUASHFS_FILE_TEMP=$SQUASHFS_FILE.tmp

# delete working files
[ -f $SQUASHFS_FILE ] && rm $SQUASHFS_FILE
[ -f $OUTPUT_SQUASHFS_FILE_TEMP ] && rm $OUTPUT_SQUASHFS_FILE_TEMP

# analyze output (input) firmware before manipulating
VANILLA_FIRMWARE_BINWALK=$( binwalk $VANILLA_FIRMWARE )
VANILLA_FIRMWARE_BINWALK_SQUASHFS=$( echo "$VANILLA_FIRMWARE_BINWALK" | grep "Squashfs filesystem" )

# make sure vanilla firmware contains 1 squashfs filesystem
if [ $( echo "$VANILLA_FIRMWARE_BINWALK_SQUASHFS" | wc -l ) != 1 ]; then
	echo "vanilla_firmware ($VANILLA_FIRMWARE) does not contain a squashfs filesystem or contains multiple."	
	exit 6
fi

# remove current squashfs filesystem in output firmware and replace with squashfs_file contents
VANILLA_FIRMWARE_BINWALK_OFFSET=$( echo "$VANILLA_FIRMWARE_BINWALK_SQUASHFS" | grep -oP "^\d+" )
VANILLA_FIRMWARE_BINWALK_SIZE=$( echo "$VANILLA_FIRMWARE_BINWALK_SQUASHFS" | grep -oP ' size: \K\w+' )

$( { dd bs=1 skip=$VANILLA_FIRMWARE_BINWALK_OFFSET count=$VANILLA_FIRMWARE_BINWALK_SIZE; } <$VANILLA_FIRMWARE \
> $OUTPUT_SQUASHFS_FILE_TEMP )

# analyze squashfs file
SQUASHFS_FILE_BINWALK=$( binwalk $OUTPUT_SQUASHFS_FILE_TEMP | grep "Squashfs filesystem" )

# make sure squashfs file contains 1 squashfs filesystem
if [ $( echo "$SQUASHFS_FILE_BINWALK" | wc -l ) != 1 ]; then
	echo "output squashfs_file ($OUTPUT_SQUASHFS_FILE_TEMP) does not contain a squashfs filesystem or contains multiple."	
	exit 2
fi

# make sure squashfs header says the filesize is the same as the wc filesize
SQUASHFS_FILE_BINWALK_SIZE=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' size: \K\w+' )
SQUASHFS_FILE_WC_SIZE=$( wc -c < $OUTPUT_SQUASHFS_FILE_TEMP )

if [ "$SQUASHFS_FILE_BINWALK_SIZE" != "$SQUASHFS_FILE_WC_SIZE" ]; then
	echo "output squashfs_file ($OUTPUT_SQUASHFS_FILE_TEMP) filesystem header and wc reports different sizes! ($SQUASHFS_FILE_BINWALK_SIZE vs $SQUASHFS_FILE_WC_SIZE)"
	exit 3
fi

# make sure blocksize is 262144 bytes
SQUASHFS_FILE_BINWALK_BLOCKSIZE=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' blocksize: \K\w+' )

if [ "$SQUASHFS_FILE_BINWALK_BLOCKSIZE" != "262144" ]; then
	echo "output squashfs_file ($OUTPUT_SQUASHFS_FILE_TEMP) has wrong blocksize (real: $SQUASHFS_FILE_BINWALK_BLOCKSIZE, expected: 262144)"
	exit 4
fi

# make sure compression algorithm used is xz
SQUASHFS_FILE_BINWALK_COMPRESSION=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' compression:\K\w+' )

if [ "$SQUASHFS_FILE_BINWALK_COMPRESSION" != "xz" ]; then
	echo "output squashfs_file ($OUTPUT_SQUASHFS_FILE_TEMP) is compressed with the wrong algorithm (real: $SQUASHFS_FILE_BINWALK_COMPRESSION, expected: xz)"
	exit 5
fi

# done!
mv $OUTPUT_SQUASHFS_FILE_TEMP $SQUASHFS_FILE

echo Done.
