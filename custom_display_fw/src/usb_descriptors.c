/*
 * USB Descriptors for AFi-R LCD display module
 * CDC ACM device matching original firmware VID/PID
 *
 * Implements tinyusb descriptor callbacks:
 *   tud_descriptor_device_cb()
 *   tud_descriptor_configuration_cb()
 *   tud_descriptor_string_cb()
 */

#include "tusb.h"

/*----------------------------------------------------------------
 * Device Descriptor
 *----------------------------------------------------------------*/
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    /* CDC class defined at interface level (using IAD) */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x0483,   /* STMicroelectronics */
    .idProduct          = 0x5740,   /* Virtual ComPort */
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

/* Invoked when received GET DEVICE DESCRIPTOR */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/*----------------------------------------------------------------
 * Configuration Descriptor
 *----------------------------------------------------------------*/

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

/* Endpoint numbers matching original firmware layout:
 *   EP2 IN  (0x82) = CDC notification (interrupt)
 *   EP1 OUT (0x01) = CDC data out (bulk)
 *   EP1 IN  (0x81) = CDC data in (bulk)
 */
#define EPNUM_CDC_NOTIF   0x82
#define EPNUM_CDC_OUT     0x01
#define EPNUM_CDC_IN      0x81

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static uint8_t const desc_configuration[] = {
    /* Config number, interface count, string index, total length, attribute, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    /* Interface number, string index, EP notification address and size,
     * EP data address (out, in) and size */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

/* Invoked when received GET CONFIGURATION DESCRIPTOR */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/*----------------------------------------------------------------
 * String Descriptors
 *----------------------------------------------------------------*/

static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  /* 0: Supported language = English (0x0409) */
    "Ubiquiti",                     /* 1: Manufacturer */
    "STM32 Virtual ComPort",        /* 2: Product */
    "000000000001",                 /* 3: Serial number */
    "AFi-R CDC",                    /* 4: CDC interface */
};

static uint16_t _desc_str[33];

/* Invoked when received GET STRING DESCRIPTOR request */
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        _desc_str[1] = 0x0409;  /* English */
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = 0;
        for (; str[chr_count] && chr_count < 32; chr_count++) {
            _desc_str[1 + chr_count] = str[chr_count];
        }
    }

    /* First word: length (including header) and string descriptor type */
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}
