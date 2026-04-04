/*
 * TinyUSB configuration for AFi-R LCD display module
 * STM32F2xx with USB OTG FS (DWC2), CDC device only
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------
 * COMMON CONFIGURATION
 *----------------------------------------------------------------*/

/* MCU: STM32F2 uses Synopsys DWC2 OTG IP */
#define CFG_TUSB_MCU              OPT_MCU_STM32F2

/* RHPort0 is USB OTG FS in device mode, full speed */
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* No RTOS */
#define CFG_TUSB_OS               OPT_OS_NONE

/* Debug level: 0 = none */
#define CFG_TUSB_DEBUG            0

/* USB DMA: STM32F2 OTG FS does not have DMA */
#define CFG_TUD_MEM_DCACHE_ENABLE 0

/* Memory section and alignment for endpoint buffers */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))

/*----------------------------------------------------------------
 * DEVICE CONFIGURATION
 *----------------------------------------------------------------*/

/* EP0 max packet size */
#define CFG_TUD_ENDPOINT0_SIZE    64

/*--- Class driver counts ---*/
#define CFG_TUD_CDC               1
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

/*--- CDC FIFO sizes — large RX buffer minimizes USB NAKs during pixel streaming ---*/
#define CFG_TUD_CDC_RX_BUFSIZE    16384
#define CFG_TUD_CDC_TX_BUFSIZE    256

/* EP buffer size for CDC bulk endpoints */
#define CFG_TUD_CDC_EP_BUFSIZE    64

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
