/* Minimal STM32F2xx register definitions for AFi-R LCD firmware */
#pragma once

#include <stdint.h>

/* ---- Base addresses ---- */
#define PERIPH_BASE       0x40000000UL
#define APB1_BASE         PERIPH_BASE
#define APB2_BASE         (PERIPH_BASE + 0x00010000UL)
#define AHB1_BASE         (PERIPH_BASE + 0x00020000UL)
#define AHB2_BASE         (PERIPH_BASE + 0x10000000UL)

/* ---- RCC ---- */
#define RCC_BASE          (AHB1_BASE + 0x3800UL)
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t PLLCFGR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t AHB1RSTR;
    volatile uint32_t AHB2RSTR;
    volatile uint32_t AHB3RSTR;
    uint32_t RESERVED0;
    volatile uint32_t APB1RSTR;
    volatile uint32_t APB2RSTR;
    uint32_t RESERVED1[2];
    volatile uint32_t AHB1ENR;
    volatile uint32_t AHB2ENR;
    volatile uint32_t AHB3ENR;
    uint32_t RESERVED2;
    volatile uint32_t APB1ENR;
    volatile uint32_t APB2ENR;
    uint32_t RESERVED3[2];
    volatile uint32_t AHB1LPENR;
    volatile uint32_t AHB2LPENR;
    volatile uint32_t AHB3LPENR;
    uint32_t RESERVED4;
    volatile uint32_t APB1LPENR;
    volatile uint32_t APB2LPENR;
    uint32_t RESERVED5[2];
    volatile uint32_t BDCR;
    volatile uint32_t CSR;
    uint32_t RESERVED6[2];
    volatile uint32_t SSCGR;
    volatile uint32_t PLLI2SCFGR;
} RCC_TypeDef;
#define RCC               ((RCC_TypeDef *)RCC_BASE)

/* RCC bits */
#define RCC_CR_HSEON      (1 << 16)
#define RCC_CR_HSERDY     (1 << 17)
#define RCC_CR_PLLON      (1 << 24)
#define RCC_CR_PLLRDY     (1 << 25)
#define RCC_CFGR_SW_PLL   0x02
#define RCC_CFGR_SWS_PLL  0x08
#define RCC_AHB1ENR_GPIOAEN  (1 << 0)
#define RCC_AHB1ENR_GPIOBEN  (1 << 1)
#define RCC_AHB1ENR_GPIOCEN  (1 << 2)
#define RCC_AHB1ENR_GPIODEN  (1 << 3)
#define RCC_AHB1ENR_GPIOEEN  (1 << 4)
#define RCC_AHB3ENR_FSMCEN   (1 << 0)
#define RCC_AHB2ENR_OTGFSEN  (1 << 7)
#define RCC_APB2ENR_SYSCFGEN (1 << 14)

/* ---- GPIO ---- */
#define GPIOA_BASE        (AHB1_BASE + 0x0000UL)
#define GPIOB_BASE        (AHB1_BASE + 0x0400UL)
#define GPIOC_BASE        (AHB1_BASE + 0x0800UL)
#define GPIOD_BASE        (AHB1_BASE + 0x0C00UL)
#define GPIOE_BASE        (AHB1_BASE + 0x1000UL)
typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} GPIO_TypeDef;
#define GPIOA             ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB             ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC             ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD             ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOE             ((GPIO_TypeDef *)GPIOE_BASE)

#define GPIO_MODER_AF     0x02
#define GPIO_MODER_OUT    0x01
#define GPIO_OSPEEDR_HIGH 0x03
#define GPIO_AF12_FSMC    12
#define GPIO_AF10_OTG_FS  10

/* ---- FLASH ---- */
#define FLASH_BASE_R      (AHB1_BASE + 0x3C00UL)
typedef struct {
    volatile uint32_t ACR;
    volatile uint32_t KEYR;
    volatile uint32_t OPTKEYR;
    volatile uint32_t SR;
    volatile uint32_t CR;
    volatile uint32_t OPTCR;
} FLASH_TypeDef;
#define FLASH_R           ((FLASH_TypeDef *)FLASH_BASE_R)
#define FLASH_ACR_LATENCY_3WS  3
#define FLASH_ACR_PRFTEN  (1 << 8)
#define FLASH_ACR_ICEN    (1 << 9)
#define FLASH_ACR_DCEN    (1 << 10)

/* ---- FSMC ---- */
#define FSMC_Bank1_R_BASE 0xA0000000UL
typedef struct {
    volatile uint32_t BTCR[8];  /* BCR1/BTR1 .. BCR4/BTR4 */
} FSMC_Bank1_TypeDef;
#define FSMC_Bank1        ((FSMC_Bank1_TypeDef *)FSMC_Bank1_R_BASE)

/* FSMC LCD addresses (directly memory-mapped) */
#define LCD_CMD_ADDR      ((volatile uint16_t *)0x60000000UL)
#define LCD_DATA_ADDR     ((volatile uint16_t *)0x60020000UL)

/* ---- I2C1 ---- */
#define I2C1_BASE         (APB1_BASE + 0x5400UL)
typedef struct {
    volatile uint32_t CR1;    /* +0x00 */
    volatile uint32_t CR2;    /* +0x04 */
    volatile uint32_t OAR1;   /* +0x08 */
    volatile uint32_t OAR2;   /* +0x0C */
    volatile uint32_t DR;     /* +0x10 */
    volatile uint32_t SR1;    /* +0x14 */
    volatile uint32_t SR2;    /* +0x18 */
    volatile uint32_t CCR;    /* +0x1C */
    volatile uint32_t TRISE;  /* +0x20 */
} I2C_TypeDef;
#define I2C1              ((I2C_TypeDef *)I2C1_BASE)

/* I2C CR1 bits */
#define I2C_CR1_PE        (1 << 0)
#define I2C_CR1_START     (1 << 8)
#define I2C_CR1_STOP      (1 << 9)
#define I2C_CR1_ACK       (1 << 10)
#define I2C_CR1_POS       (1 << 11)
#define I2C_CR1_SWRST     (1 << 15)
/* I2C SR1 bits */
#define I2C_SR1_SB        (1 << 0)
#define I2C_SR1_ADDR      (1 << 1)
#define I2C_SR1_BTF       (1 << 2)
#define I2C_SR1_RXNE      (1 << 6)
#define I2C_SR1_TXE       (1 << 7)
#define I2C_SR1_AF        (1 << 10)

#define GPIO_AF4_I2C      4

/* ---- TIM6 (basic timer for DAC triggering) ---- */
#define TIM6_BASE         (APB1_BASE + 0x1000UL)
typedef struct {
    volatile uint32_t CR1;    /* +0x00 */
    volatile uint32_t CR2;    /* +0x04 */
    uint32_t RESERVED0;       /* +0x08 */
    volatile uint32_t DIER;   /* +0x0C */
    volatile uint32_t SR;     /* +0x10 */
    volatile uint32_t EGR;    /* +0x14 */
    uint32_t RESERVED1[3];    /* +0x18..0x20 */
    volatile uint32_t CNT;    /* +0x24 */
    volatile uint32_t PSC;    /* +0x28 */
    volatile uint32_t ARR;    /* +0x2C */
} TIM_Basic_TypeDef;
#define TIM6              ((TIM_Basic_TypeDef *)TIM6_BASE)

/* ---- DMA1 ---- */
#define DMA1_BASE         (AHB1_BASE + 0x6000UL)
typedef struct {
    volatile uint32_t LISR;
    volatile uint32_t HISR;
    volatile uint32_t LIFCR;
    volatile uint32_t HIFCR;
} DMA_TypeDef;
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t NDTR;
    volatile uint32_t PAR;
    volatile uint32_t M0AR;
    volatile uint32_t M1AR;
    volatile uint32_t FCR;
} DMA_Stream_TypeDef;
#define DMA1              ((DMA_TypeDef *)DMA1_BASE)
#define DMA1_Stream6      ((DMA_Stream_TypeDef *)(DMA1_BASE + 0x10UL + 6 * 0x18UL))
/* DMA1 Stream 6 flags in HISR/HIFCR */
#define DMA_HISR_TCIF6    (1UL << 21)
#define DMA_HISR_HTIF6    (1UL << 20)
#define DMA_HISR_TEIF6    (1UL << 19)
#define DMA_HISR_DMEIF6   (1UL << 18)
#define DMA_HISR_FEIF6    (1UL << 16)
#define DMA1_Stream6_IRQn 17

/* ---- USB OTG FS ---- */
#define USB_OTG_FS_BASE   0x50000000UL

typedef struct {
    volatile uint32_t GOTGCTL;
    volatile uint32_t GOTGINT;
    volatile uint32_t GAHBCFG;
    volatile uint32_t GUSBCFG;
    volatile uint32_t GRSTCTL;
    volatile uint32_t GINTSTS;
    volatile uint32_t GINTMSK;
    volatile uint32_t GRXSTSR;
    volatile uint32_t GRXSTSP;
    volatile uint32_t GRXFSIZ;
    volatile uint32_t DIEPTXF0_HNPTXFSIZ;
    volatile uint32_t HNPTXSTS;
    uint32_t RESERVED0[2];
    volatile uint32_t GCCFG;
    volatile uint32_t CID;
    uint32_t RESERVED1[48];
    volatile uint32_t HPTXFSIZ;
    volatile uint32_t DIEPTXF[3];  /* DIEPTXF1..3 */
} USB_OTG_GlobalTypeDef;

typedef struct {
    volatile uint32_t DCFG;
    volatile uint32_t DCTL;
    volatile uint32_t DSTS;
    uint32_t RESERVED0;
    volatile uint32_t DIEPMSK;
    volatile uint32_t DOEPMSK;
    volatile uint32_t DAINT;
    volatile uint32_t DAINTMSK;
    uint32_t RESERVED1[2];
    volatile uint32_t DVBUSDIS;
    volatile uint32_t DVBUSPULSE;
    uint32_t RESERVED2;
    volatile uint32_t DIEPEMPMSK;
} USB_OTG_DeviceTypeDef;

typedef struct {
    volatile uint32_t DIEPCTL;
    uint32_t RESERVED0;
    volatile uint32_t DIEPINT;
    uint32_t RESERVED1;
    volatile uint32_t DIEPTSIZ;
    volatile uint32_t DIEPDMA;
    volatile uint32_t DTXFSTS;
    uint32_t RESERVED2;
} USB_OTG_INEPTypeDef;

typedef struct {
    volatile uint32_t DOEPCTL;
    uint32_t RESERVED0;
    volatile uint32_t DOEPINT;
    uint32_t RESERVED1;
    volatile uint32_t DOEPTSIZ;
    volatile uint32_t DOEPDMA;
    uint32_t RESERVED2[2];
} USB_OTG_OUTEPTypeDef;

#define USB_OTG_FS        ((USB_OTG_GlobalTypeDef *)USB_OTG_FS_BASE)
#define USB_OTG_DEV       ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_BASE + 0x800UL))
#define USB_OTG_INEP(n)   ((USB_OTG_INEPTypeDef *)(USB_OTG_FS_BASE + 0x900UL + (n)*0x20UL))
#define USB_OTG_OUTEP(n)  ((USB_OTG_OUTEPTypeDef *)(USB_OTG_FS_BASE + 0xB00UL + (n)*0x20UL))
#define USB_OTG_FIFO(n)   ((volatile uint32_t *)(USB_OTG_FS_BASE + 0x1000UL + (n)*0x1000UL))
#define USB_OTG_PCGCCTL   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0xE00UL))

/* USB OTG bits */
#define USB_OTG_GAHBCFG_GINT       (1 << 0)
#define USB_OTG_GUSBCFG_FDMOD      (1 << 30)
#define USB_OTG_GUSBCFG_PHYSEL     (1 << 6)
#define USB_OTG_GRSTCTL_CSRST      (1 << 0)
#define USB_OTG_GRSTCTL_RXFFLSH    (1 << 4)
#define USB_OTG_GRSTCTL_TXFFLSH    (1 << 5)
#define USB_OTG_GCCFG_PWRDWN       (1 << 16)
#define USB_OTG_GCCFG_NOVBUSSENS   (1 << 21)
#define USB_OTG_GINTSTS_USBRST     (1 << 12)
#define USB_OTG_GINTSTS_ENUMDNE    (1 << 13)
#define USB_OTG_GINTSTS_RXFLVL     (1 << 4)
#define USB_OTG_GINTSTS_IEPINT     (1 << 18)
#define USB_OTG_GINTSTS_OEPINT     (1 << 19)
#define USB_OTG_GINTSTS_SOF        (1 << 3)
#define USB_OTG_GINTMSK_RXFLVLM   (1 << 4)
#define USB_OTG_GINTMSK_USBRSTM   (1 << 12)
#define USB_OTG_GINTMSK_ENUMDNEM  (1 << 13)
#define USB_OTG_GINTMSK_IEPINT    (1 << 18)
#define USB_OTG_GINTMSK_OEPINT    (1 << 19)
#define USB_OTG_DCFG_DSPD_FS      0x03
#define USB_OTG_DOEPCTL_EPENA     (1 << 31)
#define USB_OTG_DOEPCTL_CNAK      (1 << 26)
#define USB_OTG_DOEPCTL_SNAK      (1 << 27)
#define USB_OTG_DIEPCTL_EPENA     (1 << 31)
#define USB_OTG_DIEPCTL_CNAK      (1 << 26)
#define USB_OTG_DIEPCTL_SNAK      (1 << 27)
#define USB_OTG_DIEPCTL_STALL     (1 << 21)
#define USB_OTG_DIEPCTL_TXFNUM_Pos 22
#define USB_OTG_DIEPCTL_EPTYP_Pos  18
#define USB_OTG_DIEPCTL_MPSIZ_Pos  0
#define USB_OTG_DOEPCTL_EPTYP_Pos  18
#define USB_OTG_DOEPCTL_MPSIZ_Pos  0

/* SysTick */
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018UL)

/* NVIC */
#define NVIC_ISER(n)  (*(volatile uint32_t *)(0xE000E100UL + 4*(n)))
#define OTG_FS_IRQn   67

/* ---- Compatibility defines required by tinyusb DWC2 driver ---- */

/* STM32F2xx USB OTG FS parameters */
#define USB_OTG_FS_PERIPH_BASE     USB_OTG_FS_BASE
#define USB_OTG_FS_MAX_IN_ENDPOINTS  4
#define USB_OTG_FS_TOTAL_FIFO_SIZE   1280

/* STM32F2xx USB OTG HS parameters */
#define USB_OTG_HS_PERIPH_BASE     0x40040000UL
#define USB_OTG_HS_MAX_IN_ENDPOINTS  5
#define USB_OTG_HS_TOTAL_FIFO_SIZE   4096

/* NVIC / IRQ types for CMSIS compatibility */
typedef int IRQn_Type;
#define OTG_FS_IRQn   67
#define OTG_HS_IRQn   77

static inline void NVIC_EnableIRQ(IRQn_Type irq) {
    volatile uint32_t *iser = (volatile uint32_t *)0xE000E100UL;
    iser[irq / 32] = (1UL << (irq % 32));
}

static inline void NVIC_DisableIRQ(IRQn_Type irq) {
    volatile uint32_t *icer = (volatile uint32_t *)0xE000E180UL;
    icer[irq / 32] = (1UL << (irq % 32));
}

/* SystemCoreClock -- required by tinyusb for turnaround time calculation */
extern uint32_t SystemCoreClock;

/* Misc */
#define __WFI()  __asm volatile ("wfi")
#define __DSB()  __asm volatile ("dsb" ::: "memory")
#define __ISB()  __asm volatile ("isb" ::: "memory")
#define __NOP()  __asm volatile ("nop")
