# Firmware Tools

## HOW-TO

1. Install the dependencies ``squashfs-tools``, ``binwalk``, ``xxxd``, ``cksfv``.

2. Download the manufacturer's firmware from ``http://riga.corp.ubnt.com/file/stable/AFi-R.qca956x.v{VERSION}.bin``.
    * ``{VERSION}`` can be found in the information section of the router's mobile application, for example ``3.1.2.0-g5de4b81633``.

3. ``./unpack_firmware.sh vanilla.bin vanilla.bin.squashfs``

    * ``vanilla.bin`` is your firmware file downloaded from the manufacturer.

4. ``./unpack_squashfs.sh vanilla.bin.squashfs``

5. Make your changes to the firmware files in ``squashfs-root``.

6. ``./pack_squashfs.sh vanilla.bin_custom.squashfs``

7. ``./make_firmware.sh vanilla.bin vanilla.bin_custom.squashfs vanilla_custom.bin``

8. ``vanilla_custom.bin`` is your custom firmware!

## unpack_firmware.sh

``unpack_firmware.sh`` extracts the OpenWRT firmware, which is in the format of a SquashFS file, from the manufacturer's firmware file.

##### Arguments

|Name                |Explanation                                             |
|--------------------|--------------------------------------------------------|
|vanilla_firmware    |Path to the manufacturer's firmware                     |       
|output_squashfs_file|The path for the resulting SquashFS file we've extracted|

##### Return codes

|Code |Explanation                                                    |
|-----|---------------------------------------------------------------|
|0    |Successfully extracted the OpenWRT firmware SquashFS file      |       
|1    |Invalid arguments or file doesn't exist                        |
|2    |Output file contains no or multiple SquashFS filesystems       |
|3    |SquashFS filesystem header and wc reports different sizes      |
|4    |SquashFS filesystem has wrong blocksize (expects 262144)       |
|5    |SquashFS filesystem is not compressed with xz                  |
|6    |vanilla_firmware contains no or multiple SquashFS filesystems  |

## unpack_squashfs.sh

``unpack_squashfs.sh`` extracts the SquashFS filesystem using ``unsquashfs`` to the ``squashfs-root`` folder.

##### Arguments

|Name                |Explanation                                             |
|--------------------|--------------------------------------------------------|
|squashfs_file       |Path to the SquashFS file to extract                    |       

##### Return codes

|Code |Explanation                                                    |
|-----|---------------------------------------------------------------|
|0    |Successfully unsquashed the SquashFS file                      |       
|1    |Invalid arguments                                              |

## pack_squashfs.sh

``pack_squashfs.sh`` makes a SquashFS filesystem using ``mksquashfs`` from the contents of the ``squashfs-root`` folder.

##### Arguments

|Name                |Explanation                                             |
|--------------------|--------------------------------------------------------|
|out_squashfs_file   |The path for the resulting SquashFS file                |       

##### Return codes

|Code |Explanation                                                    |
|-----|---------------------------------------------------------------|
|0    |Successfully squashed ``squashfs-root`` into a SquashFS file   |       
|1    |Invalid arguments or ``squashfs-root`` doesn't exist           |

## make_firmware.sh

``make_firmware.sh`` takes the manufacturer's firmware and replaces the current OpenWRT firmware SquashFS file in it with the one passed to the script.

##### Arguments

|Name                |Explanation                                             |
|--------------------|--------------------------------------------------------|
|vanilla_firmware    |Path to the manufacturer's firmware                     |       
|squashfs_file       |Path to an OpenWRT firmware SquashFS file               |
|output_firmware     |The path for the resulting firmware                     |

##### Return codes

|Code |Explanation                                                         |
|-----|--------------------------------------------------------------------|
|0    |Successfully created the firmware                                   |       
|1    |Invalid arguments or file doesn't exist                             |
|2    |squashfs_file contains no or multiple SquashFS filesystems          |
|3    |squashfs_file filesystem header and wc reports different sizes      |
|4    |squashfs_file filesystem has wrong blocksize (expects 262144)       |
|5    |squashfs_file filesystem is not compressed with xz                  |
|6    |vanilla_firmware contains no or multiple SquashFS filesystems       |