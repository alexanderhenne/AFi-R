#!/bin/bash

VANILLA_FIRMWARE=$1
SQUASHFS_FILE=$2
OUTPUT_FIRMWARE=$3

if [ ! $VANILLA_FIRMWARE ] || [ ! $SQUASHFS_FILE ] || [ ! $OUTPUT_FIRMWARE ]; then
	echo "$0 vanilla_firmware squashfs_file output_firmware"
	exit 1
fi

if [ ! -f $VANILLA_FIRMWARE ]; then
	echo "vanilla_firmware file can't be accessed"
	exit 1
fi
if [ ! -f $SQUASHFS_FILE ]; then
	echo "squashfs_file can't be accessed"
	exit 1
fi

# analyze squashfs file
SQUASHFS_FILE_BINWALK=$( binwalk $SQUASHFS_FILE | grep "Squashfs filesystem" )

# make sure squashfs file contains 1 squashfs filesystem
if [ $( echo "$SQUASHFS_FILE_BINWALK" | wc -l ) != 1 ]; then
	echo "squashfs_file ($SQUASHFS_FILE) does not contain a squashfs filesystem or contains multiple."	
	exit 2
fi

# make sure squashfs header says the filesize is the same as the wc filesize
SQUASHFS_FILE_BINWALK_SIZE=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' size: \K\w+' )
SQUASHFS_FILE_WC_SIZE=$( wc -c < $SQUASHFS_FILE )

if [ "$SQUASHFS_FILE_BINWALK_SIZE" != "$SQUASHFS_FILE_WC_SIZE" ]; then
	echo "squashfs_file ($SQUASHFS_FILE) filesystem header and wc reports different sizes! ($SQUASHFS_FILE_BINWALK_SIZE vs $SQUASHFS_FILE_WC_SIZE)"
	exit 3
fi

# make sure blocksize is 262144 bytes
SQUASHFS_FILE_BINWALK_BLOCKSIZE=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' blocksize: \K\w+' )

if [ "$SQUASHFS_FILE_BINWALK_BLOCKSIZE" != "262144" ]; then
	echo "squashfs_file ($SQUASHFS_FILE) has wrong blocksize (real: $SQUASHFS_FILE_BINWALK_BLOCKSIZE, expected: 262144)"
	exit 4
fi

# make sure compression algorithm used is xz
SQUASHFS_FILE_BINWALK_COMPRESSION=$( echo "$SQUASHFS_FILE_BINWALK" | grep -oP ' compression:\K\w+' )

if [ "$SQUASHFS_FILE_BINWALK_COMPRESSION" != "xz" ]; then
	echo "squashfs_file ($SQUASHFS_FILE) is compressed with the wrong algorithm (real: $SQUASHFS_FILE_BINWALK_COMPRESSION, expected: xz)"
	exit 5
fi

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

OUTPUT_FIRMWARE_TEMP=$OUTPUT_FIRMWARE.tmp

# delete working files
[ -f $OUTPUT_FIRMWARE ] && rm $OUTPUT_FIRMWARE
[ -f $OUTPUT_FIRMWARE_TEMP ] && rm $OUTPUT_FIRMWARE_TEMP
[ -f $OUTPUT_FIRMWARE_TEMP.1 ] && rm $OUTPUT_FIRMWARE_TEMP.1
[ -f $OUTPUT_FIRMWARE_TEMP.2 ] && rm $OUTPUT_FIRMWARE_TEMP.2

# copy vanilla to working file
cp $VANILLA_FIRMWARE $OUTPUT_FIRMWARE_TEMP

$( { { dd bs=1 count=$VANILLA_FIRMWARE_BINWALK_OFFSET; } <$VANILLA_FIRMWARE; cat $SQUASHFS_FILE; \
{ dd bs=1 skip=$((VANILLA_FIRMWARE_BINWALK_OFFSET+VANILLA_FIRMWARE_BINWALK_SIZE)); } < $VANILLA_FIRMWARE; } \
> $OUTPUT_FIRMWARE_TEMP )

# update firmware partition header size
FIRMWARE_PARTITION_HEADER=$( binwalk $OUTPUT_FIRMWARE_TEMP | grep "name: \"PARTfirmware\"" )
FIRMWARE_PARTITION_SIZE=$( echo "$FIRMWARE_PARTITION_HEADER" | grep -oP " data size: \K\w+" )
FIRMWARE_PARTITION_HEADER_SIZE=$( echo "$FIRMWARE_PARTITION_HEADER" | grep -oP " header size: \K\w+" )
FIRMWARE_PARTITION_HEADER_OFFSET=$( echo "$FIRMWARE_PARTITION_HEADER" | grep -oP "^\d+" )

FIRMWARE_PARTITION_SIZE_OFFSET=$((FIRMWARE_PARTITION_HEADER_OFFSET+FIRMWARE_PARTITION_HEADER_SIZE-4-3)) # 4 padding from the end, size is 3 bytes

NEW_FIRMWARE_PARTITION_SIZE=$((FIRMWARE_PARTITION_SIZE+SQUASHFS_FILE_WC_SIZE-VANILLA_FIRMWARE_BINWALK_SIZE))

$( { { dd bs=1 count=$FIRMWARE_PARTITION_SIZE_OFFSET; } <$OUTPUT_FIRMWARE_TEMP; \
printf "0: %.8x" $NEW_FIRMWARE_PARTITION_SIZE | xxd -r -g0 | tail -c3; \
{ dd bs=1 skip=$(($FIRMWARE_PARTITION_SIZE_OFFSET+3)); } <$OUTPUT_FIRMWARE_TEMP; } > $OUTPUT_FIRMWARE_TEMP.1 )

rm $OUTPUT_FIRMWARE_TEMP
mv $OUTPUT_FIRMWARE_TEMP.1 $OUTPUT_FIRMWARE_TEMP

# update firmware partition crc32 checksum
FIRMWARE_PARTITION_CHECKSUM_OFFSET=$((FIRMWARE_PARTITION_HEADER_OFFSET+NEW_FIRMWARE_PARTITION_SIZE+FIRMWARE_PARTITION_HEADER_SIZE))

echo $FIRMWARE_PARTITION_CHECKSUM_OFFSET

# $OUTPUT_FIRMWARE_TEMP.1 is the firmware file (to checksum) after this command
$( dd bs=1 skip=$FIRMWARE_PARTITION_HEADER_OFFSET count=$((FIRMWARE_PARTITION_CHECKSUM_OFFSET-FIRMWARE_PARTITION_HEADER_OFFSET)) <$OUTPUT_FIRMWARE_TEMP \
> $OUTPUT_FIRMWARE_TEMP.1 )

FIRMWARE_PARTITION_CRC32_CHECKSUM=$( cksfv -c $OUTPUT_FIRMWARE_TEMP.1 | grep -v "^;" | grep -oP " \K\w+" )

# done checksumming, so remove the file
rm $OUTPUT_FIRMWARE_TEMP.1

$( { { dd bs=1 count=$FIRMWARE_PARTITION_CHECKSUM_OFFSET; } <$OUTPUT_FIRMWARE_TEMP; \
echo "$FIRMWARE_PARTITION_CRC32_CHECKSUM" | xxd -r -p; \
{ dd bs=1 skip=$(($FIRMWARE_PARTITION_CHECKSUM_OFFSET+4)); } <$OUTPUT_FIRMWARE_TEMP; \
} > $OUTPUT_FIRMWARE_TEMP.1 )

rm $OUTPUT_FIRMWARE_TEMP
mv $OUTPUT_FIRMWARE_TEMP.1 $OUTPUT_FIRMWARE_TEMP

# done!
mv $OUTPUT_FIRMWARE_TEMP $OUTPUT_FIRMWARE

echo Done.
