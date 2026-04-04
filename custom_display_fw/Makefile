# Makefile for AFi-R custom LCD firmware with TinyUSB CDC
# Builds a bare-metal STM32F2xx binary for the display module

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

TARGET  = lcd_firmware

# Required output size: 916480 bytes (0xDFC00)
PAD_SIZE = 916480

# TinyUSB source location
TINYUSB = /tmp/tinyusb/src

# Project directory
PROJ_DIR = src

# --- Source files ---

# Our application sources
APP_SRCS = \
	$(PROJ_DIR)/main.c \
	$(PROJ_DIR)/usb_descriptors.c

# TinyUSB core sources
TUSB_SRCS = \
	$(TINYUSB)/tusb.c \
	$(TINYUSB)/device/usbd.c \
	$(TINYUSB)/device/usbd_control.c \
	$(TINYUSB)/class/cdc/cdc_device.c \
	$(TINYUSB)/common/tusb_fifo.c \
	$(TINYUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
	$(TINYUSB)/portable/synopsys/dwc2/dwc2_common.c

SRCS = $(APP_SRCS) $(TUSB_SRCS)

# --- Include paths ---
INCLUDES = \
	-I$(PROJ_DIR) \
	-I$(TINYUSB)

# --- Compiler flags ---
CFLAGS  = -mcpu=cortex-m3 -mthumb -Os
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-type-limits
CFLAGS += -fno-common -ffunction-sections -fdata-sections
CFLAGS += -nostartfiles -ffreestanding
CFLAGS += -DSTM32F205xx
CFLAGS += -DCFG_TUSB_MCU=OPT_MCU_STM32F2
CFLAGS += $(INCLUDES)

# --- Linker flags ---
LDFLAGS = -mcpu=cortex-m3 -mthumb
LDFLAGS += -T $(PROJ_DIR)/linker.ld
LDFLAGS += -nostartfiles
LDFLAGS += --specs=nosys.specs --specs=nano.specs
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--print-memory-usage

# --- Object files (put everything in build/) ---
BUILD_DIR = build

# Convert source paths to object paths in build dir
APP_OBJS  = $(patsubst $(PROJ_DIR)/%.c, $(BUILD_DIR)/%.o, $(APP_SRCS))
TUSB_OBJS = $(patsubst $(TINYUSB)/%.c, $(BUILD_DIR)/tusb/%.o, $(TUSB_SRCS))
OBJS      = $(APP_OBJS) $(TUSB_OBJS)

.PHONY: all clean

all: $(TARGET).bin
	@echo ""
	@echo "=== Build complete ==="
	@ls -la $(TARGET).bin
	@echo -n "MD5: " && md5sum $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

# Application source files
$(BUILD_DIR)/%.o: $(PROJ_DIR)/%.c $(PROJ_DIR)/stm32f2xx.h $(PROJ_DIR)/tusb_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# TinyUSB source files
$(BUILD_DIR)/tusb/%.o: $(TINYUSB)/%.c $(PROJ_DIR)/tusb_config.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary --gap-fill 0xFF $< $@
	@# Pad to exactly $(PAD_SIZE) bytes with 0xFF
	@FSIZE=$$(stat -c%s $@) ; \
	if [ $$FSIZE -lt $(PAD_SIZE) ]; then \
		dd if=/dev/zero bs=1 count=$$(($(PAD_SIZE) - $$FSIZE)) 2>/dev/null | tr '\000' '\377' >> $@ ; \
	elif [ $$FSIZE -gt $(PAD_SIZE) ]; then \
		echo "ERROR: Binary too large ($$FSIZE > $(PAD_SIZE) bytes)!" ; \
		exit 1 ; \
	fi
	@echo "Padded to $(PAD_SIZE) bytes"

clean:
	rm -rf $(BUILD_DIR) $(TARGET).elf $(TARGET).bin
