/*
 * Custom bare-metal STM32F2xx firmware for AmpliFi AFi-R LCD display module.
 *
 * Uses TinyUSB for USB CDC serial communication.
 * Receives pixel data over CDC and writes it to a 240x240 ST7789V LCD
 * connected via FSMC 8080 parallel interface.
 */

#include "stm32f2xx.h"
#include "tusb.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ======================================================================
 * Forward declarations
 * ====================================================================== */
void Reset_Handler(void);
void Default_Handler(void);
void OTG_FS_IRQHandler(void);
void DMA1_Stream6_IRQHandler(void);
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void);

extern uint32_t _estack;

/* ======================================================================
 * Global: SystemCoreClock (required by tinyusb DWC2 driver)
 * ====================================================================== */
uint32_t SystemCoreClock = 96000000UL;

/* ======================================================================
 * Vector table -- placed in .isr_vector by linker
 * 16 system exceptions + 68 IRQs (0..67), IRQ67 = OTG_FS
 * ====================================================================== */
__attribute__((section(".isr_vector"), used))
void (* const vector_table[16 + 68])(void) = {
    (void (*)(void))(&_estack),   /* Initial SP */
    Reset_Handler,                 /* Reset */
    NMI_Handler,                   /* NMI */
    HardFault_Handler,             /* HardFault */
    MemManage_Handler,             /* MemManage */
    BusFault_Handler,              /* BusFault */
    UsageFault_Handler,            /* UsageFault */
    0, 0, 0, 0,                   /* Reserved */
    Default_Handler,               /* SVCall */
    Default_Handler,               /* DebugMon */
    0,                             /* Reserved */
    Default_Handler,               /* PendSV */
    SysTick_Handler,               /* SysTick */
    /* IRQ 0..16: all default */
    [16 + 0 ... 16 + 16] = Default_Handler,
    /* IRQ 17: DMA1_Stream6 (audio DAC) */
    [16 + 17] = DMA1_Stream6_IRQHandler,
    /* IRQ 18..66: all default */
    [16 + 18 ... 16 + 66] = Default_Handler,
    /* IRQ 67: OTG_FS */
    [16 + 67] = OTG_FS_IRQHandler,
};

/* ======================================================================
 * Version string at fixed offset 0x200 -- bootloader reads this
 * ====================================================================== */
__attribute__((section(".version"), used))
const char firmware_version[] = "aFiDsp.AP.99.99";

/* ======================================================================
 * Tail tag at 0x080FFBF0 -- 16 bytes of 0x41
 * ====================================================================== */
__attribute__((section(".tailtag"), used))
const uint8_t tail_tag[16] = {
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41
};

/* ======================================================================
 * Externs from linker script
 * ====================================================================== */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

/* ======================================================================
 * SysTick millisecond counter (used by tusb_time_millis_api)
 * ====================================================================== */
static volatile uint32_t systick_ms;

void SysTick_Handler(void)
{
    systick_ms++;
}

/* TinyUSB requires this for timing (no RTOS mode) */
uint32_t tusb_time_millis_api(void)
{
    return systick_ms;
}

/* ======================================================================
 * Delay using systick
 * ====================================================================== */
static void delay_ms(uint32_t ms)
{
    uint32_t start = systick_ms;
    while ((systick_ms - start) < ms) {}
}

/* ======================================================================
 * System init -- configure clocks using HSI (internal 16MHz)
 *
 *   HSI=16MHz, PLLM=16, PLLN=192, PLLP=2 -> SYSCLK=96MHz
 *   PLLQ=4 -> USB=48MHz (exactly)
 * ====================================================================== */
#define SCB_VTOR  (*(volatile uint32_t *)0xE000ED08UL)

static void SystemInit_Custom(void)
{
    /* Point VTOR to our vector table */
    SCB_VTOR = 0x08020000;
    __DSB();
    __ISB();

    /* PLL from HSE (8 MHz crystal, confirmed from board: TXC 8.00)
     *   PLLM=8, PLLN=240, PLLP=2 → SYSCLK=120MHz
     *   PLLQ=5 → USB=48MHz exactly
     */

    /* Enable HSI first as safe fallback clock */
    RCC->CR |= 1;  /* HSION */
    while (!(RCC->CR & 2))  /* HSIRDY */
        ;

    /* Switch to HSI before touching PLL */
    RCC->CFGR = (RCC->CFGR & ~0x03);  /* SW = HSI */
    while ((RCC->CFGR & 0x0C) != 0x00)  /* SWS = HSI */
        ;

    /* Disable PLL */
    RCC->CR &= ~(1 << 24);  /* PLLON off */
    while (RCC->CR & (1 << 25))  /* PLLRDY clear */
        ;

    /* Enable HSE (8 MHz crystal) */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY))
        ;

    /* Flash wait states for 120MHz (3 WS) */
    FLASH_R->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN
                 | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* PLL: 8MHz HSE / 8 * 240 / 2 = 120MHz, 240/5 = 48MHz USB */
    RCC->PLLCFGR = (8 << 0)     /* PLLM = 8 */
                 | (240 << 6)    /* PLLN = 240 */
                 | (0 << 16)     /* PLLP = /2 */
                 | (1 << 22)     /* PLLSRC = HSE */
                 | (5 << 24);    /* PLLQ = 5 → 48MHz USB */

    /* Enable PLL */
    RCC->CR |= (1 << 24);
    while (!(RCC->CR & (1 << 25)))
        ;

    /* AHB=/1, APB1=/4 (max 30MHz), APB2=/2 (max 60MHz) */
    RCC->CFGR = (0 << 4) | (5 << 10) | (4 << 13);

    /* Switch to PLL */
    RCC->CFGR |= 0x02;
    while ((RCC->CFGR & 0x0C) != 0x08)
        ;

    /* SysTick at 120MHz */
    SYST_RVR = (120000000 / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07;  /* Enable, tick interrupt, processor clock */

    SystemCoreClock = 120000000UL;
}

/* ======================================================================
 * GPIO helpers
 * ====================================================================== */
static void gpio_set_af(GPIO_TypeDef *gpio, uint8_t pin, uint8_t af)
{
    /* Set mode to AF (0x02) */
    gpio->MODER &= ~(3UL << (pin * 2));
    gpio->MODER |=  (GPIO_MODER_AF << (pin * 2));

    /* Push-pull */
    gpio->OTYPER &= ~(1UL << pin);

    /* High speed */
    gpio->OSPEEDR &= ~(3UL << (pin * 2));
    gpio->OSPEEDR |=  (GPIO_OSPEEDR_HIGH << (pin * 2));

    /* No pull */
    gpio->PUPDR &= ~(3UL << (pin * 2));

    /* Set alternate function */
    uint8_t idx = (pin < 8) ? 0 : 1;
    uint8_t pos = (pin & 7) * 4;
    gpio->AFR[idx] &= ~(0x0FUL << pos);
    gpio->AFR[idx] |=  ((uint32_t)af << pos);
}

/* ======================================================================
 * FSMC init for 16-bit 8080 parallel LCD (matching original firmware)
 * ====================================================================== */
static void fsmc_init(void)
{
    /* Enable clocks: GPIOD, GPIOE, FSMC */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
    RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;
    __DSB();

    /* PORTD FSMC pins (16-bit data bus + control):
     *   PD0=D2, PD1=D3, PD4=NOE, PD5=NWE, PD7=NE1,
     *   PD8=D13, PD9=D14, PD10=D15, PD11=A16 (D/C select),
     *   PD14=D0, PD15=D1 */
    static const uint8_t pd_pins[] = {0, 1, 4, 5, 7, 8, 9, 10, 11, 14, 15};
    for (int i = 0; i < (int)(sizeof(pd_pins)/sizeof(pd_pins[0])); i++)
        gpio_set_af(GPIOD, pd_pins[i], GPIO_AF12_FSMC);

    /* PORTE FSMC pins (16-bit data bus D4-D12):
     *   PE7=D4, PE8=D5, PE9=D6, PE10=D7,
     *   PE11=D8, PE12=D9, PE13=D10, PE14=D11, PE15=D12 */
    static const uint8_t pe_pins[] = {7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < (int)(sizeof(pe_pins)/sizeof(pe_pins[0])); i++)
        gpio_set_af(GPIOE, pe_pins[i], GPIO_AF12_FSMC);

    /* FSMC Bank 1, Sub-bank 1 (NE1):
     *   BCR1: MBKEN, SRAM type, 16-bit data bus, write enable
     */
    FSMC_Bank1->BTCR[0] = (1 << 0)    /* MBKEN */
                         | (0 << 1)    /* no mux */
                         | (0 << 2)    /* SRAM */
                         | (1 << 4)    /* 16-bit data bus (MWID=01) — matches original firmware */
                         | (1 << 12);  /* WREN */

    /* BTR1 timing: tighter for faster LCD writes at 120MHz.
     * LCD needs ~60ns setup + ~100ns data hold.
     * ADDSET=2 (~17ns), DATAST=5 (~42ns), BUSTURN=1 */
    FSMC_Bank1->BTCR[1] = (2 << 0)    /* ADDSET=2 */
                         | (0 << 4)    /* ADDHLD=0 */
                         | (5 << 8)    /* DATAST=5 */
                         | (1 << 16);  /* BUSTURN=1 */
}

/* ======================================================================
 * LCD commands via FSMC
 * ====================================================================== */

/* FSMC 8080 parallel LCD.
 * Command register: A16=0 -> address 0x60000000
 * Data register:    A16=1 -> address 0x60020000 (16-bit bus: A16 at HADDR bit 17)
 * Use uint8_t writes — generates strb which FSMC handles as byte-lane access. */
#define LCD_CMD   (*(volatile uint8_t *)0x60000000UL)
#define LCD_DAT   (*(volatile uint8_t *)0x60020000UL)

static void lcd_cmd(uint8_t cmd)
{
    LCD_CMD = cmd;
}

static void lcd_data(uint8_t data)
{
    LCD_DAT = data;
}

/* ST7789V is a 240x320 controller driving a 240x240 panel.
 * The visible area may not start at row 0 — common offsets are 0 or 80.
 * Adjust these if the display is shifted: */
#define LCD_X_OFFSET  0
#define LCD_Y_OFFSET  50

static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x0 = x + LCD_X_OFFSET;
    uint16_t x1 = x + w - 1 + LCD_X_OFFSET;
    uint16_t y0 = y + LCD_Y_OFFSET;
    uint16_t y1 = y + h - 1 + LCD_Y_OFFSET;

    lcd_cmd(0x2A);  /* CASET */
    lcd_data(x0 >> 8);
    lcd_data(x0 & 0xFF);
    lcd_data(x1 >> 8);
    lcd_data(x1 & 0xFF);

    lcd_cmd(0x2B);  /* RASET */
    lcd_data(y0 >> 8);
    lcd_data(y0 & 0xFF);
    lcd_data(y1 >> 8);
    lcd_data(y1 & 0xFF);

    lcd_cmd(0x2C);  /* RAMWR */
}

/* ======================================================================
 * Backlight control via DAC (PA4 = DAC_OUT1) and/or PWM timer
 * ====================================================================== */
#define DAC_BASE   0x40007400UL
#define DAC_CR     (*(volatile uint32_t *)(DAC_BASE + 0x00))
#define DAC_DHR12R1 (*(volatile uint32_t *)(DAC_BASE + 0x08))

static void backlight_init(void)
{
    /* Enable clocks: DAC, GPIOA, GPIOB, GPIOC */
    RCC->APB1ENR |= (1 << 29);  /* DACEN */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN
                  | RCC_AHB1ENR_GPIOBEN
                  | RCC_AHB1ENR_GPIOCEN;
    __DSB();

    /* --- DAC on PA4 (channel 1) and PA5 (channel 2) --- */
    GPIOA->MODER |= (3 << (4 * 2)) | (3 << (5 * 2));  /* Analog mode */
    DAC_CR = 0;  /* Reset DAC */
    DAC_CR = (1 << 0) | (1 << 16);  /* Enable both channels */
    DAC_DHR12R1 = 4095;  /* Max brightness ch1 */
    *(volatile uint32_t *)(DAC_BASE + 0x14) = 4095;  /* Max brightness ch2 */

    /* PB6-PB9 are reserved for I2C1 (touchscreen) — NOT used for backlight.
     * Stock firmware confirms: PB6=SCL, PB7=SDA, PB8=touch reset, PB9=touch IRQ. */

    /* GPIO brute force on remaining backlight candidate pins */
    uint16_t pb_pins[] = {0, 1, 10, 11};
    for (int i = 0; i < 4; i++) {
        int p = pb_pins[i];
        GPIOB->MODER = (GPIOB->MODER & ~(3 << (p*2))) | (1 << (p*2));
        GPIOB->BSRR = (1 << p);
    }
    uint16_t pc_pins[] = {6, 7};
    for (int i = 0; i < 2; i++) {
        int p = pc_pins[i];
        GPIOC->MODER = (GPIOC->MODER & ~(3 << (p*2))) | (1 << (p*2));
        GPIOC->BSRR = (1 << p);
    }
}

static void lcd_init(void)
{
    /* Turn on backlight first */
    backlight_init();

    /* ST7789V init sequence */

    lcd_cmd(0x11);  /* Sleep Out */
    delay_ms(120);

    lcd_cmd(0x36);  /* Memory Access Control */
    lcd_data(0xC8);  /* MY=1, MX=1, BGR=1 — matches original firmware */

    lcd_cmd(0x3A);  /* Pixel format: 16-bit RGB565 */
    lcd_data(0x55);

    lcd_cmd(0xBB);  /* VCOM setting */
    lcd_data(0x20);

    lcd_cmd(0xC3);  /* VRH */
    lcd_data(0x0B);

    lcd_cmd(0xC5);
    lcd_data(0x10);

    lcd_cmd(0xC6);  /* Frame rate */
    lcd_data(0x0F);

    lcd_cmd(0x26);  /* Gamma set */
    lcd_data(0x01);

    lcd_cmd(0xD0);  /* Power control */
    lcd_data(0xA4);
    lcd_data(0xA1);

    lcd_cmd(0xE0);  /* Positive gamma correction */
    lcd_data(0xD0); lcd_data(0x01); lcd_data(0x08); lcd_data(0x0F);
    lcd_data(0x11); lcd_data(0x2A); lcd_data(0x36); lcd_data(0x55);
    lcd_data(0x44); lcd_data(0x3A); lcd_data(0x0B); lcd_data(0x06);
    lcd_data(0x11); lcd_data(0x20);

    lcd_cmd(0xE1);  /* Negative gamma correction */
    lcd_data(0xD0); lcd_data(0x02); lcd_data(0x07); lcd_data(0x0A);
    lcd_data(0x0B); lcd_data(0x18); lcd_data(0x34); lcd_data(0x43);
    lcd_data(0x4A); lcd_data(0x2B); lcd_data(0x1B); lcd_data(0x1C);
    lcd_data(0x22); lcd_data(0x1F);

    lcd_cmd(0x29);  /* Display ON */
    delay_ms(20);
}

/* ======================================================================
 * USB init -- enable clocks, configure GPIOs, init tinyusb
 * ====================================================================== */
static void usb_init(void)
{
    /* Enable GPIOA clock (PA11=DM, PA12=DP) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    __DSB();

    /* Enable USB OTG FS clock + SYSCFG clock (required by original firmware) */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    __DSB();

    /* Configure PA11, PA12 as AF10 (USB OTG FS) */
    gpio_set_af(GPIOA, 11, GPIO_AF10_OTG_FS);
    gpio_set_af(GPIOA, 12, GPIO_AF10_OTG_FS);

    /* Initialize tinyusb device stack */
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(0, &rh_init);

    /* CRITICAL: The bootloader jumps to us with interrupts disabled (CPSID I).
     * Re-enable interrupts so SysTick and USB IRQ handlers can fire. */
    __asm volatile ("cpsie i" ::: "memory");
}

/* ======================================================================
 * OTG_FS interrupt handler -- calls tinyusb DWC2 handler
 * ====================================================================== */
void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

/* ======================================================================
 * TinyUSB CDC callback -- called when data is received
 * (We process in the main loop, so this is just a stub)
 * ====================================================================== */
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
}


/* ======================================================================
 * CDC send helpers
 * ====================================================================== */
static void cdc_send(const uint8_t *data, uint32_t len)
{
    while (len > 0) {
        uint32_t avail = tud_cdc_write_available();
        if (avail == 0) {
            tud_cdc_write_flush();
            tud_task();
            continue;
        }
        uint32_t chunk = (len < avail) ? len : avail;
        uint32_t written = tud_cdc_write(data, chunk);
        data += written;
        len -= written;
    }
    tud_cdc_write_flush();
}

static void cdc_send_str(const char *s)
{
    cdc_send((const uint8_t *)s, strlen(s));
}

/* ======================================================================
 * Command parser helpers
 * ====================================================================== */

static int str_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_int(const char *s, uint32_t *out)
{
    uint32_t val = 0;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = val;
    return 1;
}

/* ======================================================================
 * Default handler for unhandled exceptions
 * ====================================================================== */
void Default_Handler(void)
{
    while (1)
        ;
}

/* ======================================================================
 * Audio subsystem -- DAC channel 2 (PA5) via TIM6 + DMA1 Stream 6
 *
 * TIM6 fires at 16 kHz, triggering DAC CH2 to load the next sample
 * from a circular DMA buffer. The DMA half-transfer and transfer-complete
 * interrupts refill each buffer half with generated waveform data.
 * ====================================================================== */

#define AUDIO_SAMPLE_RATE  16000
#define AUDIO_BUF_LEN      512   /* Circular DMA buffer: 2 halves of 256 */
#define AUDIO_HALF_LEN     (AUDIO_BUF_LEN / 2)

/* Phase increment per Hz: 2^32 / SAMPLE_RATE = 268435.456 ≈ 268435 */
#define PHASE_INC_PER_HZ   268435

/* 256-entry sine wave lookup table, 12-bit (0-4095), one full cycle */
static const uint16_t sine_table[256] = {
    2048, 2098, 2148, 2199, 2249, 2299, 2348, 2398, 2447, 2497, 2545, 2594, 2642, 2690, 2738, 2785,
    2831, 2878, 2923, 2968, 3013, 3057, 3100, 3143, 3185, 3227, 3267, 3307, 3347, 3385, 3423, 3459,
    3495, 3531, 3565, 3598, 3630, 3662, 3692, 3722, 3750, 3777, 3804, 3829, 3853, 3876, 3898, 3919,
    3939, 3958, 3975, 3992, 4007, 4021, 4034, 4045, 4056, 4065, 4073, 4080, 4085, 4089, 4093, 4094,
    4095, 4094, 4093, 4089, 4085, 4080, 4073, 4065, 4056, 4045, 4034, 4021, 4007, 3992, 3975, 3958,
    3939, 3919, 3898, 3876, 3853, 3829, 3804, 3777, 3750, 3722, 3692, 3662, 3630, 3598, 3565, 3531,
    3495, 3459, 3423, 3385, 3347, 3307, 3267, 3227, 3185, 3143, 3100, 3057, 3013, 2968, 2923, 2878,
    2831, 2785, 2738, 2690, 2642, 2594, 2545, 2497, 2447, 2398, 2348, 2299, 2249, 2199, 2148, 2098,
    2048, 1998, 1948, 1897, 1847, 1797, 1748, 1698, 1649, 1599, 1551, 1502, 1454, 1406, 1358, 1311,
    1265, 1218, 1173, 1128, 1083, 1039,  996,  953,  911,  869,  829,  789,  749,  711,  673,  637,
     601,  565,  531,  498,  466,  434,  404,  374,  346,  319,  292,  267,  243,  220,  198,  177,
     157,  138,  121,  104,   89,   75,   62,   51,   40,   31,   23,   16,   11,    7,    3,    2,
       1,    2,    3,    7,   11,   16,   23,   31,   40,   51,   62,   75,   89,  104,  121,  138,
     157,  177,  198,  220,  243,  267,  292,  319,  346,  374,  404,  434,  466,  498,  531,  565,
     601,  637,  673,  711,  749,  789,  829,  869,  911,  953,  996, 1039, 1083, 1128, 1173, 1218,
    1265, 1311, 1358, 1406, 1454, 1502, 1551, 1599, 1649, 1698, 1748, 1797, 1847, 1897, 1948, 1998,
};

/* Audio DMA buffer */
static uint16_t audio_buf[AUDIO_BUF_LEN];

/* Audio state — volatile fields are shared between ISR and main loop */
static volatile uint32_t audio_phase_inc;   /* DDS phase increment (ISR reads) */
static volatile uint16_t audio_amplitude;   /* 0-256, 256 = full scale (ISR reads) */
static uint32_t audio_phase;                /* DDS phase accumulator (ISR only) */
static uint8_t  audio_playing;

/* User master volume (0-100), default 50 */
static uint8_t audio_user_volume = 50;

/* Tone sequence for multi-step sounds */
#define AUDIO_MAX_STEPS 8
typedef struct {
    uint16_t freq;          /* Hz (0 = silence) */
    uint16_t duration_ms;
    uint8_t  volume;        /* 0-100 (relative amplitude) */
} AudioStep;
static AudioStep audio_seq[AUDIO_MAX_STEPS];
static uint8_t  audio_seq_len;
static uint8_t  audio_seq_idx;
static uint32_t audio_step_end;  /* systick_ms when current step ends */

#define DAC_DHR12R2  (*(volatile uint32_t *)(DAC_BASE + 0x14))

/* PCM streaming ring buffer (single-producer single-consumer, lock-free) */
#define STREAM_BUF_SIZE  4096   /* 16-bit samples, must be power of 2 */
#define STREAM_BUF_MASK  (STREAM_BUF_SIZE - 1)
static uint16_t stream_buf[STREAM_BUF_SIZE];
static volatile uint32_t stream_wr;   /* write index (main loop only) */
static volatile uint32_t stream_rd;   /* read index (ISR only) */
static volatile uint8_t  audio_streaming;  /* streaming mode active */
static uint8_t  stream_started;  /* DAC/DMA started for this stream */
static uint32_t stream_last_rx;  /* systick_ms of last received data */

/* Fill half of the DMA buffer with generated samples (called from ISR) */
static void audio_fill_half(uint16_t *buf, uint32_t count)
{
    /* PCM streaming: pull samples from ring buffer, apply volume */
    if (audio_streaming) {
        uint16_t amp = audio_amplitude;
        for (uint32_t i = 0; i < count; i++) {
            if (stream_rd != stream_wr) {
                uint16_t raw = stream_buf[stream_rd & STREAM_BUF_MASK];
                stream_rd++;
                int32_t s = (int32_t)raw - 2048;
                s = (s * amp) >> 8;
                buf[i] = (uint16_t)(s + 2048);
            } else {
                buf[i] = 2048;  /* underrun: DAC midpoint = silence */
            }
        }
        return;
    }

    /* Tone generation: DDS sine wave */
    uint32_t phase = audio_phase;
    uint32_t inc = audio_phase_inc;
    uint16_t amp = audio_amplitude;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t idx = (uint8_t)(phase >> 24);
        int32_t s = (int32_t)sine_table[idx] - 2048;
        s = (s * amp) >> 8;
        buf[i] = (uint16_t)(s + 2048);
        phase += inc;
    }
    audio_phase = phase;
}

void DMA1_Stream6_IRQHandler(void)
{
    uint32_t hisr = DMA1->HISR;

    if (hisr & DMA_HISR_HTIF6) {
        DMA1->HIFCR = DMA_HISR_HTIF6;
        audio_fill_half(&audio_buf[0], AUDIO_HALF_LEN);
    }
    if (hisr & DMA_HISR_TCIF6) {
        DMA1->HIFCR = DMA_HISR_TCIF6;
        audio_fill_half(&audio_buf[AUDIO_HALF_LEN], AUDIO_HALF_LEN);
    }
}

static void audio_init(void)
{
    /* Enable clocks: DMA1 (AHB1 bit 21), TIM6 (APB1 bit 4) */
    RCC->AHB1ENR |= (1 << 21);
    RCC->APB1ENR |= (1 << 4);
    __DSB();

    /* TIM6: 16 kHz trigger for DAC
     * Timer clock = APB1 * 2 = 60 MHz (APB1 prescaler /4 → timers get 2x)
     * ARR = 60000000 / 16000 - 1 = 3749 */
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = 3749;
    TIM6->CR2 = (2 << 4);  /* MMS=010: Update event → TRGO */
    TIM6->EGR = 1;         /* UG: force load of PSC/ARR shadow registers */
    TIM6->SR  = 0;

    /* Set DAC CH2 to midpoint (silence without voltage jump on amp enable) */
    DAC_DHR12R2 = 2048;

    /* PA6: speaker amplifier enable (push-pull output, start disabled) */
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << (6*2))) | (1UL << (6*2));
    GPIOA->OTYPER &= ~(1UL << 6);
    GPIOA->PUPDR  &= ~(3UL << (6*2));
    GPIOA->BSRR = (1 << (6 + 16));  /* PA6 LOW = amp off */

    /* Enable DMA1_Stream6 interrupt in NVIC */
    NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

static void audio_start_hw(void)
{
    /* DAC is already at midpoint (2048) from init/stop.
     * Enable amp now while output is silent — no voltage jump, no pop. */
    GPIOA->BSRR = (1 << 6);  /* PA6 HIGH = amp on */
    delay_ms(2);              /* Let amp settle on midpoint */

    /* Pre-fill the entire buffer */
    audio_phase = 0;
    audio_fill_half(&audio_buf[0], AUDIO_HALF_LEN);
    audio_fill_half(&audio_buf[AUDIO_HALF_LEN], AUDIO_HALF_LEN);

    /* Disable DMA stream before reconfiguration */
    DMA1_Stream6->CR = 0;
    while (DMA1_Stream6->CR & 1) {}

    /* Clear all Stream 6 interrupt flags */
    DMA1->HIFCR = DMA_HISR_TCIF6 | DMA_HISR_HTIF6 | DMA_HISR_TEIF6
                | DMA_HISR_DMEIF6 | DMA_HISR_FEIF6;

    /* Configure DMA1 Stream 6, Channel 7 → DAC CH2 (DHR12R2) */
    DMA1_Stream6->PAR  = DAC_BASE + 0x14;        /* DAC_DHR12R2 address */
    DMA1_Stream6->M0AR = (uint32_t)audio_buf;
    DMA1_Stream6->NDTR = AUDIO_BUF_LEN;
    DMA1_Stream6->FCR  = 0;                       /* Direct mode (no FIFO) */
    DMA1_Stream6->CR   = (7 << 25)   /* CHSEL = 7 (DAC2) */
                       | (1 << 13)   /* MSIZE = 16-bit */
                       | (1 << 11)   /* PSIZE = 16-bit */
                       | (1 << 10)   /* MINC  = memory increment */
                       | (1 << 8)    /* CIRC  = circular mode */
                       | (1 << 6)    /* DIR   = memory-to-peripheral */
                       | (1 << 4)    /* TCIE  = transfer complete IRQ */
                       | (1 << 3);   /* HTIE  = half transfer IRQ */

    /* Enable DMA stream */
    DMA1_Stream6->CR |= 1;

    /* Configure DAC: CH1 = software trigger (backlight), CH2 = TIM6 + DMA */
    DAC_CR = (1 << 0)    /* EN1: keep channel 1 enabled */
           | (1 << 16)   /* EN2: enable channel 2 */
           | (1 << 18)   /* TEN2: trigger enable */
           | (0 << 19)   /* TSEL2=000: TIM6 TRGO */
           | (1 << 28);  /* DMAEN2: DMA enable */

    /* Start TIM6 — audio begins playing */
    TIM6->CR1 = 1;

    audio_playing = 1;
}

static void audio_stop(void)
{
    if (!audio_playing) return;

    TIM6->CR1 = 0;          /* Stop timer */
    DMA1_Stream6->CR = 0;   /* Disable DMA */

    /* Restore DAC to midpoint before disabling amp (minimize stop pop) */
    DAC_CR = (1 << 0) | (1 << 16);
    DAC_DHR12R2 = 2048;     /* Midpoint = silence */
    delay_ms(2);

    /* Disable speaker amplifier (PA6 LOW) */
    GPIOA->BSRR = (1 << (6 + 16));

    audio_playing = 0;
    audio_seq_len = 0;
    audio_streaming = 0;
}

static void audio_start_step(void)
{
    AudioStep *step = &audio_seq[audio_seq_idx];

    audio_phase_inc = (uint32_t)step->freq * PHASE_INC_PER_HZ;
    /* Effective amplitude = step_volume * user_volume * 256 / 10000 */
    audio_amplitude = (uint16_t)((uint32_t)step->volume * audio_user_volume * 256 / 10000);
    audio_step_end = systick_ms + step->duration_ms;

    if (!audio_playing)
        audio_start_hw();
}

static void audio_play_tone(uint16_t freq, uint16_t duration_ms)
{
    audio_stop();
    audio_seq[0].freq = freq;
    audio_seq[0].duration_ms = duration_ms;
    audio_seq[0].volume = 100;
    audio_seq_len = 1;
    audio_seq_idx = 0;
    audio_start_step();
}

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static void audio_play_sequence(const AudioStep *steps, uint8_t count)
{
    audio_stop();
    if (count > AUDIO_MAX_STEPS) count = AUDIO_MAX_STEPS;
    for (uint8_t i = 0; i < count; i++)
        audio_seq[i] = steps[i];
    audio_seq_len = count;
    audio_seq_idx = 0;
    audio_start_step();
}

static void audio_update(void)
{
    if (!audio_playing || audio_streaming) return;
    if ((int32_t)(systick_ms - audio_step_end) < 0) return;

    /* Current step finished — advance to next */
    audio_seq_idx++;
    if (audio_seq_idx < audio_seq_len) {
        audio_start_step();
    } else {
        audio_stop();
    }
}

/* Built-in tone presets (matching stock firmware names) */
static const AudioStep preset_tap[]  = { {2000,  30, 80} };
static const AudioStep preset_tone[] = { {800,  200, 60} };
static const AudioStep preset_bell[] = {
    {1000, 120, 100}, {1000, 120, 70}, {1000, 120, 45}, {1000, 120, 20},
};
static const AudioStep preset_ring[] = {
    {800, 200, 80}, {0, 100, 0}, {1000, 200, 80}, {0, 100, 0},
    {800, 200, 80}, {0, 100, 0}, {1000, 200, 80},
};
static const AudioStep preset_sweep[] = {
    {400, 60, 70}, {600, 60, 75}, {800, 60, 80}, {1000, 60, 85},
    {1200, 60, 80}, {1500, 60, 75}, {2000, 60, 70},
};

/* ======================================================================
 * Touchscreen -- Cypress/Parade CYTTSP4 or FocalTech FT3308 via I2C1
 *
 * PB6 = I2C1_SCL, PB7 = I2C1_SDA (AF4, open-drain)
 * PB8 = touch reset (XRES, active LOW)
 * PB9 = touch interrupt (INT, falling edge)
 * ====================================================================== */

#define TOUCH_NONE   0
#define TOUCH_FT3308 1
#define TOUCH_PARADE 2
static uint8_t touch_type;
static uint8_t touch_addr;    /* 7-bit I2C address */
static uint8_t tp_touch_page; /* page for touch data register */
static uint8_t tp_touch_reg;  /* register for touch data */

/* --- I2C1 polling driver --- */

static void i2c_init(void)
{
    RCC->APB1ENR |= (1 << 21);  /* I2C1EN */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    __DSB();

    /* Bus recovery: release SDA if slave is holding it low.
     * Toggle SCL 9 times as GPIO, then generate STOP condition. */
    for (int pin = 6; pin <= 7; pin++) {
        GPIOB->MODER  = (GPIOB->MODER & ~(3UL << (pin*2))) | (1UL << (pin*2));
        GPIOB->OTYPER |= (1UL << pin);
        GPIOB->OSPEEDR |= (3UL << (pin*2));
        GPIOB->PUPDR  &= ~(3UL << (pin*2));
    }
    GPIOB->BSRR = (1 << 6) | (1 << 7);
    delay_ms(1);
    for (int i = 0; i < 9; i++) {
        GPIOB->BSRR = (1 << (6 + 16));
        for (volatile int d = 0; d < 120; d++) {}
        GPIOB->BSRR = (1 << 6);
        for (volatile int d = 0; d < 120; d++) {}
    }
    GPIOB->BSRR = (1 << (7 + 16));  /* SDA LOW */
    for (volatile int d = 0; d < 120; d++) {}
    GPIOB->BSRR = (1 << 6);          /* SCL HIGH */
    for (volatile int d = 0; d < 120; d++) {}
    GPIOB->BSRR = (1 << 7);          /* SDA HIGH = STOP */
    delay_ms(1);

    /* PB6 = SCL, PB7 = SDA: AF4, open-drain, no pull (matches stock fw) */
    for (int pin = 6; pin <= 7; pin++) {
        GPIOB->MODER = (GPIOB->MODER & ~(3UL << (pin*2))) | (2UL << (pin*2));
        uint8_t pos = (pin & 7) * 4;
        GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFUL << pos)) | (GPIO_AF4_I2C << pos);
    }

    /* Reset and configure I2C1 */
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = 30;
    I2C1->CCR = 150;
    I2C1->TRISE = 31;
    I2C1->OAR1 = 0;
    I2C1->CR1 = I2C_CR1_PE;
}

static int i2c_wait(volatile uint32_t *reg, uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start = systick_ms;
    while (!(*reg & flag)) {
        if ((systick_ms - start) > timeout_ms) return -1;
    }
    return 0;
}

static int i2c_wait_not(volatile uint32_t *reg, uint32_t flag, uint32_t timeout_ms)
{
    uint32_t start = systick_ms;
    while (*reg & flag) {
        if ((systick_ms - start) > timeout_ms) return -1;
    }
    return 0;
}

static int tp_safe_read(uint8_t addr, uint8_t *buf, uint32_t len, uint32_t tmo);

/* HAL_I2C_Mem_Write equivalent: START → addr+W → memAddr → data... → STOP
 * Matches stock firmware register-level sequence exactly. */
static int i2c_mem_write(uint8_t addr, uint8_t memAddr, const uint8_t *data, uint32_t len)
{
    /* Wait for bus idle */
    if (i2c_wait_not(&I2C1->SR2, (1 << 1), 100)) return -1;  /* BUSY */

    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, 100)) goto fail;

    I2C1->DR = (addr << 1);  /* addr + W */
    if (i2c_wait(&I2C1->SR1, I2C_SR1_ADDR, 100)) goto fail;
    if (I2C1->SR1 & I2C_SR1_AF) goto fail;

    /* Clear ADDR with interrupts disabled (timing-critical) */
    __asm volatile ("cpsid i" ::: "memory");
    (void)I2C1->SR1;
    (void)I2C1->SR2;
    __asm volatile ("cpsie i" ::: "memory");

    /* Send register address */
    if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE, 100)) goto fail;
    I2C1->DR = memAddr;

    /* Send data bytes */
    for (uint32_t i = 0; i < len; i++) {
        if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE, 100)) goto fail;
        I2C1->DR = data[i];
    }

    /* Wait for last byte to finish */
    if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF, 100)) goto fail;
    I2C1->CR1 |= I2C_CR1_STOP;

    return 0;

fail:
    I2C1->SR1 &= ~I2C_SR1_AF;
    I2C1->CR1 |= I2C_CR1_STOP;
    return -1;
}

/* HAL_I2C_Mem_Read equivalent: START → addr+W → memAddr → RESTART → addr+R → data... → STOP */
static int i2c_mem_read(uint8_t addr, uint8_t memAddr, uint8_t *data, uint32_t len)
{
    if (len == 0) return 0;
    if (i2c_wait_not(&I2C1->SR2, (1 << 1), 100)) return -1;

    /* Write phase: send register address */
    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, 100)) goto fail;
    I2C1->DR = (addr << 1);
    if (i2c_wait(&I2C1->SR1, I2C_SR1_ADDR, 100)) goto fail;
    if (I2C1->SR1 & I2C_SR1_AF) goto fail;

    __asm volatile ("cpsid i" ::: "memory");
    (void)I2C1->SR1;
    (void)I2C1->SR2;
    __asm volatile ("cpsie i" ::: "memory");

    if (i2c_wait(&I2C1->SR1, I2C_SR1_TXE, 100)) goto fail;
    I2C1->DR = memAddr;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF, 100)) goto fail;

    /* Read phase */
    return tp_safe_read(addr, data, len, 500);

fail:
    I2C1->SR1 &= ~I2C_SR1_AF;
    I2C1->CR1 |= I2C_CR1_STOP;
    return -1;
}

/* Legacy wrapper for existing callers */
static int i2c_write_read(uint8_t addr, const uint8_t *wr, uint32_t wr_len,
                           uint8_t *rd, uint32_t rd_len)
{
    if (wr_len == 1 && rd_len > 0)
        return i2c_mem_read(addr, wr[0], rd, rd_len);
    if (wr_len > 0 && rd_len == 0)
        return i2c_mem_write(addr, wr[0], wr + 1, wr_len - 1);
    if (wr_len == 0 && rd_len > 0)
        return tp_safe_read(addr, rd, rd_len, 500);
    return -1;
}

static int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, uint32_t len)
{
    return i2c_mem_read(addr, reg, buf, len);
}

/* Errata-safe I2C read for N bytes (handles N=1, N=2 POS/BTF, and N>2) */
static int tp_safe_read(uint8_t addr, uint8_t *buf, uint32_t len, uint32_t tmo)
{
    if (len == 0) return 0;
    I2C1->SR1 &= ~I2C_SR1_AF;
    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK | I2C_CR1_START;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, tmo)) goto fail;
    I2C1->DR = (addr << 1) | 1;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_ADDR, tmo)) goto fail;

    if (len == 1) {
        I2C1->CR1 &= ~I2C_CR1_ACK;
        (void)I2C1->SR2;
        I2C1->CR1 |= I2C_CR1_STOP;
        if (i2c_wait(&I2C1->SR1, I2C_SR1_RXNE, tmo)) goto fail;
        buf[0] = (uint8_t)I2C1->DR;
    } else if (len == 2) {
        I2C1->CR1 &= ~I2C_CR1_ACK;
        I2C1->CR1 |= I2C_CR1_POS;
        (void)I2C1->SR2;
        if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF, tmo)) goto fail;
        I2C1->CR1 |= I2C_CR1_STOP;
        buf[0] = (uint8_t)I2C1->DR;
        buf[1] = (uint8_t)I2C1->DR;
    } else {
        (void)I2C1->SR2;
        for (uint32_t i = 0; i < len; i++) {
            if (i == len - 3) {
                /* Wait for BTF (byte N-2 in DR, N-1 in shift reg) */
                if (i2c_wait(&I2C1->SR1, I2C_SR1_BTF, tmo)) goto fail;
                I2C1->CR1 &= ~I2C_CR1_ACK;
                buf[i] = (uint8_t)I2C1->DR;  /* byte N-3 */
                i++;
                I2C1->CR1 |= I2C_CR1_STOP;
                buf[i] = (uint8_t)I2C1->DR;  /* byte N-2 */
                i++;
                if (i2c_wait(&I2C1->SR1, I2C_SR1_RXNE, tmo)) goto fail;
                buf[i] = (uint8_t)I2C1->DR;  /* byte N-1 (last) */
                break;
            }
            if (i2c_wait(&I2C1->SR1, I2C_SR1_RXNE, tmo)) goto fail;
            buf[i] = (uint8_t)I2C1->DR;
        }
    }
    I2C1->CR1 &= ~I2C_CR1_POS;
    return 0;

fail:
    I2C1->CR1 &= ~(I2C_CR1_ACK | I2C_CR1_POS);
    I2C1->CR1 |= I2C_CR1_STOP;
    I2C1->SR1 &= ~I2C_SR1_AF;
    return -1;
}

/* Probe: check if address ACKs, then reset I2C to clear any error state */
static int i2c_probe(uint8_t addr)
{
    I2C1->SR1 &= ~I2C_SR1_AF;
    I2C1->CR1 |= I2C_CR1_START;
    if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, 5)) { I2C1->CR1 |= I2C_CR1_STOP; return -1; }
    I2C1->DR = (addr << 1);
    uint32_t t0 = systick_ms;
    while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF))) {
        if ((systick_ms - t0) > 5) break;
    }
    int ok = (I2C1->SR1 & I2C_SR1_ADDR) ? 0 : -1;
    if (ok == 0) (void)I2C1->SR2;
    I2C1->SR1 &= ~I2C_SR1_AF;
    I2C1->CR1 |= I2C_CR1_STOP;
    delay_ms(1);

    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = 30;
    I2C1->CCR = 150;
    I2C1->TRISE = 31;
    I2C1->CR1 = I2C_CR1_PE;
    return ok;
}

/* --- Touch controller --- */

static void touch_reset(void)
{
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << (8*2))) | (1UL << (8*2));
    GPIOB->OTYPER &= ~(1UL << 8);
    GPIOB->BSRR = (1 << (8 + 16));  /* PB8 LOW */
    delay_ms(20);
    GPIOB->BSRR = (1 << 8);         /* PB8 HIGH */
    delay_ms(500);
}

/* Page-select I2C helpers: set page register (0xFF) before each access */
static int tp_page_write(uint8_t addr, uint8_t page, uint8_t reg,
                          const uint8_t *data, uint32_t len)
{
    if (i2c_mem_write(addr, 0xFF, &page, 1) != 0) return -1;
    return i2c_mem_write(addr, reg, data, len);
}

static int tp_page_read(uint8_t addr, uint8_t page, uint8_t reg,
                         uint8_t *data, uint32_t len)
{
    if (i2c_mem_write(addr, 0xFF, &page, 1) != 0) return -1;
    return i2c_mem_read(addr, reg, data, len);
}

/* Scan descriptor entries to find the touch data register.
 * Stock firmware scans pages 0-4, registers 0xE9 down by 6 to 0x05,
 * reading 6-byte entries. Type 0x11 = touch data descriptor.
 * Saves the page + register address for subsequent touch reads. */
static int tp_discover_touch_reg(uint8_t addr)
{
    for (uint8_t page = 0; page <= 4; page++) {
        for (int reg = 0xE9; reg >= 0x05; reg -= 6) {
            uint8_t desc[6];
            if (tp_page_read(addr, page, (uint8_t)reg, desc, 6) != 0)
                continue;
            if (desc[5] == 0x11) {
                tp_touch_page = page;
                tp_touch_reg = desc[3];
                return 0;
            }
        }
    }
    return -1;
}

static void touch_init(void)
{
    touch_type = TOUCH_NONE;
    i2c_init();
    touch_reset();

    /* PB9 = touch INT: input with pull-up */
    GPIOB->MODER &= ~(3UL << (9*2));
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3UL << (9*2))) | (1UL << (9*2));

    /* Try app mode first (firmware already loaded) */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (i2c_probe(0x20) == 0) {
            touch_type = TOUCH_PARADE;
            touch_addr = 0x20;
            /* Scan descriptors to find the touch data register */
            tp_discover_touch_reg(0x20);
            return;
        }
        delay_ms(50);
    }

    /* Try bootloader mode (detect only — no automatic firmware upload) */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (i2c_probe(0x08) == 0) {
            touch_type = TOUCH_PARADE;
            touch_addr = 0x08;  /* Bootloader mode — limited functionality */
            return;
        }
        delay_ms(50);
    }

    /* Try FocalTech FT3308 */
    if (i2c_probe(0x38) == 0) {
        touch_type = TOUCH_FT3308;
        touch_addr = 0x38;
    }
}

/* Format and send a touch event: "tp <x>,<y>,<event>\r\n" */
static void touch_send_event(uint16_t x, uint16_t y, const char *ev_str)
{
    char out[32];
    int pos = 0;
    out[pos++] = 't'; out[pos++] = 'p'; out[pos++] = ' ';
    { uint16_t v = x; char d[5]; int n = 0;
      if (v == 0) d[n++] = '0';
      else while (v > 0) { d[n++] = '0' + v % 10; v /= 10; }
      while (n > 0) out[pos++] = d[--n];
    }
    out[pos++] = ',';
    { uint16_t v = y; char d[5]; int n = 0;
      if (v == 0) d[n++] = '0';
      else while (v > 0) { d[n++] = '0' + v % 10; v /= 10; }
      while (n > 0) out[pos++] = d[--n];
    }
    out[pos++] = ',';
    for (const char *s = ev_str; *s; s++) out[pos++] = *s;
    out[pos++] = '\r'; out[pos++] = '\n'; out[pos] = 0;
    cdc_send_str(out);
}

static uint32_t touch_backoff_until;  /* don't poll until this systick_ms */
static uint8_t  touch_was_down;       /* previous touch state for up/move */

static void touch_poll(void)
{
    if (touch_type == TOUCH_NONE) return;
    if (GPIOB->IDR & (1 << 9)) return;  /* INT HIGH = no data */
    if ((int32_t)(systick_ms - touch_backoff_until) < 0) return;

    if (touch_type == TOUCH_FT3308) {
        uint8_t buf[7];
        if (i2c_read_reg(touch_addr, 0x02, buf, 7) != 0) {
            touch_backoff_until = systick_ms + 500;
            return;
        }
        uint8_t n_points = buf[0] & 0x0F;
        if (n_points == 0 || n_points > 2) return;
        uint8_t event = (buf[1] >> 6) & 0x03;
        uint16_t x = ((buf[1] & 0x0F) << 8) | buf[2];
        uint16_t y = ((buf[3] & 0x0F) << 8) | buf[4];
        const char *ev = (event == 0) ? "down" : (event == 1) ? "up" : "move";
        touch_send_event(x, y, ev);
    }
    else if (touch_type == TOUCH_PARADE) {
        /* Parade: read 6-byte touch record from discovered register.
         * Format: [0]=flags, [1]=id, [2]=Xhi, [3]=Yhi, [4]=XYlo, [5]=pressure
         * X = (buf[2]<<4) | (buf[4] & 0x0F),  12-bit 0-4095
         * Y = (buf[3]<<4) | (buf[4]>>4),       12-bit 0-4095 */
        uint8_t buf[6];
        if (tp_page_read(touch_addr, tp_touch_page, tp_touch_reg, buf, 6) != 0) {
            touch_backoff_until = systick_ms + 500;
            return;
        }
        if (!(buf[0] & 0x01)) {
            if (touch_was_down) {
                touch_send_event(0, 0, "up");
                touch_was_down = 0;
            }
            return;
        }
        uint16_t x = ((uint16_t)buf[2] << 4) | (buf[4] & 0x0F);
        uint16_t y = ((uint16_t)buf[3] << 4) | ((buf[4] >> 4) & 0x0F);
        const char *ev = touch_was_down ? "move" : "down";
        touch_send_event(x, y, ev);
        touch_was_down = 1;
    }
}

/* ======================================================================
 * Main
 * ====================================================================== */

/* Command line buffer */
#define CMD_BUF_SIZE 1200
static char cmd_buf[CMD_BUF_SIZE];
static uint32_t cmd_len;

int main(void)
{
    /* Init USB and tinyusb stack */
    usb_init();

    /* Init the display hardware */
    fsmc_init();
    lcd_init();

    /* Init audio subsystem (TIM6 + DMA, not started until a tone plays) */
    audio_init();

    /* Init touchscreen (I2C1, probes for Parade or FT3308 controller) */
    touch_init();

    /* Fill screen black — two byte writes per pixel */
    lcd_set_window(0, 0, 240, 240);
    for (int i = 0; i < 240 * 240; i++) {
        lcd_data(0);
        lcd_data(0);
    }

    cmd_len = 0;

    while (1) {
        /* Run tinyusb device task (handles USB events) */
        tud_task();

        /* Advance audio sequencer (check step durations) */
        audio_update();

        touch_poll();

        /* PCM streaming mode: bulk-read USB data into ring buffer */
        if (audio_streaming) {
            uint32_t avail = tud_cdc_available();
            if (avail >= 2) {
                uint8_t tmp[512];
                uint32_t space = STREAM_BUF_SIZE - (stream_wr - stream_rd);
                uint32_t to_read = avail;
                if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
                if (to_read / 2 > space) to_read = space * 2;
                to_read &= ~1U;  /* even byte count */
                if (to_read > 0) {
                    uint32_t got = tud_cdc_read(tmp, to_read);
                    for (uint32_t i = 0; i + 1 < got; i += 2) {
                        int16_t s = (int16_t)(tmp[i] | ((uint16_t)tmp[i+1] << 8));
                        stream_buf[stream_wr & STREAM_BUF_MASK] =
                            (uint16_t)((s + 32768) >> 4);  /* s16le → 12-bit unsigned */
                        stream_wr++;
                    }
                    stream_last_rx = systick_ms;
                }
                /* Start DAC/DMA once ring buffer is half full (pre-buffer) */
                if (!stream_started &&
                    (stream_wr - stream_rd) >= STREAM_BUF_SIZE / 2) {
                    audio_start_hw();
                    stream_started = 1;
                }
            }
            /* Timeout: stop after 500ms of silence AND buffer drained */
            if ((systick_ms - stream_last_rx) > 500 &&
                stream_rd == stream_wr && stream_started) {
                audio_stop();
            }
            continue;
        }

        /* Check for received characters */
        while (tud_cdc_available()) {
            uint8_t c;
            tud_cdc_read(&c, 1);

            /* Binary frame protocol: 0xFF + 115200 bytes of RGB565.
             * Fastest path — no text parsing, no response, inlined LCD writes. */
            if (c == 0xFF && cmd_len == 0) {
                lcd_set_window(0, 0, 240, 240);
                volatile uint8_t *dat = &LCD_DAT;
                uint32_t remaining = 240 * 240 * 2;
                uint32_t last_activity = systick_ms;
                while (remaining > 0) {
                    tud_task();
                    uint32_t avail = tud_cdc_available();
                    if (avail > 0) {
                        uint8_t tmp[2048];
                        uint32_t to_read = avail;
                        if (to_read > remaining) to_read = remaining;
                        if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
                        uint32_t got = tud_cdc_read(tmp, to_read);
                        /* Inline LCD writes — avoid function call per byte */
                        for (uint32_t i = 0; i < got; i++) {
                            *dat = tmp[i];
                        }
                        remaining -= got;
                        last_activity = systick_ms;
                    }
                    if ((systick_ms - last_activity) > 500) break;
                }
                continue;
            }

            /* Handle line termination */
            if (c == '\r' || c == '\n') {
                if (cmd_len == 0) continue;  /* Skip empty lines */

                cmd_buf[cmd_len] = '\0';

                /* ---- Process command ---- */

                if (str_eq(cmd_buf, "fwversion")) {
                    cdc_send_str("[info:] aFiDsp.AP.99.99\r\n");
                }
                else if (str_eq(cmd_buf, "mode")) {
                    cdc_send_str("[info:] APP\r\n");
                }
                else if (str_eq(cmd_buf, "status")) {
                    cdc_send_str("[info:] 0x00000000\r\n");
                }
                else if (str_eq(cmd_buf, "listcmd")) {
                    cdc_send_str("[info:] fwversion\r\n");
                    cdc_send_str("[info:] mode\r\n");
                    cdc_send_str("[info:] status\r\n");
                    cdc_send_str("[info:] listcmd\r\n");
                    cdc_send_str("[info:] storedfwhash\r\n");
                    cdc_send_str("[info:] blversion\r\n");
                    cdc_send_str("[info:] hostmsg\r\n");
                    cdc_send_str("[info:] rawfb\r\n");
                    cdc_send_str("[info:] rawrow\r\n");
                    cdc_send_str("[info:] tone\r\n");
                    cdc_send_str("[info:] audiostop\r\n");
                    cdc_send_str("[info:] audiotest\r\n");
                    cdc_send_str("[info:] volume\r\n");
                    cdc_send_str("[info:] pcmstream\r\n");
                    cdc_send_str("[info:] tpstatus\r\n");
                    cdc_send_str("[info:] tpread\r\n");
                    cdc_send_str("[info:] i2cscan\r\n");
                }
                else if (str_eq(cmd_buf, "storedfwhash")) {
                    cdc_send_str("[info:] 00000000000000000000000000000000\r\n");
                }
                else if (str_eq(cmd_buf, "blversion")) {
                    cdc_send_str("[info:] aFiDsp.LD.01.32\r\n");
                }
                else if (str_eq(cmd_buf, "blreboot")) {
                    cdc_send_str("[info:] erasing hash and rebooting\r\n");
                    delay_ms(100);
                    /* Corrupt the stored firmware hash at 0x080FFC00 by programming
                     * zeros over it. The bootloader compares this hash against a
                     * computed MD5 on boot — a mismatch forces upgrade mode.
                     * Flash bits can only be cleared (1→0), so writing 0x00000000
                     * over the existing hash guarantees corruption without a sector erase. */

                    /* Unlock flash */
                    FLASH_R->KEYR = 0x45670123;
                    FLASH_R->KEYR = 0xCDEF89AB;

                    /* Wait for flash not busy */
                    while (FLASH_R->SR & (1 << 16)) {}

                    /* Program mode: write 0x00000000 to 0x080FFC00 (word program) */
                    FLASH_R->CR = (1 << 0) | (2 << 8);  /* PG=1, PSIZE=word */
                    *(volatile uint32_t *)0x080FFC00UL = 0x00000000UL;
                    while (FLASH_R->SR & (1 << 16)) {}

                    /* Lock flash */
                    FLASH_R->CR = (1 << 31);  /* LOCK */

                    /* Now reset — bootloader will see corrupted hash and enter BLD mode */
                    *(volatile uint32_t *)0xE000ED0CUL = 0x05FA0004UL;
                    while (1) {}
                }
                else if (str_eq(cmd_buf, "reboot")) {
                    cdc_send_str("[info:] rebooting\r\n");
                    delay_ms(100);
                    *(volatile uint32_t *)0xE000ED0CUL = 0x05FA0004UL;
                    while (1) {}
                }
                else if (str_eq(cmd_buf, "bench")) {
                    /* Benchmark: receive 115200 bytes, discard, report time */
                    cdc_send_str("ok\r\n");
                    uint32_t remaining = 115200;
                    uint32_t t0 = systick_ms;
                    while (remaining > 0) {
                        tud_task();
                        uint32_t avail = tud_cdc_available();
                        if (avail > 0) {
                            uint8_t tmp[2048];
                            uint32_t to_read = avail;
                            if (to_read > remaining) to_read = remaining;
                            if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
                            uint32_t got = tud_cdc_read(tmp, to_read);
                            remaining -= got;
                        }
                    }
                    uint32_t elapsed = systick_ms - t0;
                    /* Report: "NNNms\r\n" */
                    char buf[20];
                    int pos = 0;
                    uint32_t v = elapsed;
                    if (v == 0) { buf[pos++] = '0'; }
                    else {
                        char digits[10]; int d = 0;
                        while (v > 0) { digits[d++] = '0' + (v % 10); v /= 10; }
                        while (d > 0) buf[pos++] = digits[--d];
                    }
                    buf[pos++] = 'm'; buf[pos++] = 's';
                    buf[pos++] = '\r'; buf[pos++] = '\n'; buf[pos] = 0;
                    cdc_send_str(buf);
                }
                else if (str_starts_with(cmd_buf, "hostmsg=")) {
                    cdc_send_str("{\"status\":\"ok\"}\r\n");
                }
                else if (str_eq(cmd_buf, "rawfb")) {
                    /* Raw framebuffer mode: receive 115200 bytes of RGB565 */
                    cdc_send_str("ok 115200\r\n");

                    lcd_set_window(0, 0, 240, 240);

                    uint32_t remaining = 240 * 240 * 2;
                    uint32_t last_activity = systick_ms;
                    while (remaining > 0) {
                        tud_task();
                        uint32_t avail = tud_cdc_available();
                        if (avail > 0) {
                            uint8_t tmp[64];
                            uint32_t to_read = avail;
                            if (to_read > remaining) to_read = remaining;
                            if (to_read > sizeof(tmp)) to_read = sizeof(tmp);
                            uint32_t got = tud_cdc_read(tmp, to_read);
                            for (uint32_t i = 0; i < got; i++) {
                                lcd_data(tmp[i]);
                            }
                            remaining -= got;
                            last_activity = systick_ms;
                        }
                        /* Timeout after 3 seconds of no data — recover from interrupted transfers */
                        if ((systick_ms - last_activity) > 3000) {
                            cdc_send_str("timeout\r\n");
                            /* Drain any remaining bytes in CDC buffer */
                            while (tud_cdc_available()) {
                                uint8_t discard[64];
                                tud_cdc_read(discard, sizeof(discard));
                            }
                            break;
                        }
                    }

                    if (remaining == 0)
                        cdc_send_str("done\r\n");
                }
                else if (str_starts_with(cmd_buf, "rawrow=")) {
                    /* rawrow=N,HEXDATA */
                    const char *p = cmd_buf + 7;
                    uint32_t row;
                    if (parse_int(p, &row) && row < 240) {
                        /* Skip to comma */
                        while (*p && *p != ',') p++;
                        if (*p == ',') {
                            p++;
                            /* Parse 960 hex chars = 480 bytes */
                            lcd_set_window(0, (uint16_t)row, 240, 1);
                            for (int i = 0; i < 480; i++) {
                                int h = hex_digit(*p++);
                                int l = hex_digit(*p++);
                                if (h < 0 || l < 0) break;
                                lcd_data((uint8_t)((h << 4) | l));
                            }
                        }
                    }
                }
                /* ---- Audio commands ---- */
                else if (str_starts_with(cmd_buf, "tone=")) {
                    /* tone=<freq>,<duration_ms> — play a sine wave */
                    const char *p = cmd_buf + 5;
                    uint32_t freq, dur;
                    if (parse_int(p, &freq)) {
                        while (*p >= '0' && *p <= '9') p++;
                        if (*p == ',' && parse_int(p + 1, &dur)) {
                            if (freq > 0 && freq <= 8000 && dur > 0 && dur <= 10000) {
                                audio_play_tone((uint16_t)freq, (uint16_t)dur);
                                cdc_send_str("ok\r\n");
                            } else {
                                cdc_send_str("err range\r\n");
                            }
                        }
                    }
                }
                else if (str_eq(cmd_buf, "audiostop")) {
                    audio_stop();
                    cdc_send_str("ok\r\n");
                }
                else if (str_starts_with(cmd_buf, "pcmstream")) {
                    /* pcmstream or pcmstream=<rate>
                     * Enters raw PCM streaming mode. Send signed 16-bit LE
                     * mono samples. Stops on 500ms data timeout. */
                    uint32_t rate = 22050;  /* default */
                    if (cmd_buf[9] == '=')
                        parse_int(cmd_buf + 10, &rate);
                    if (rate >= 8000 && rate <= 48000) {
                        audio_stop();
                        TIM6->CR1 = 0;
                        TIM6->ARR = (60000000 / rate) - 1;
                        TIM6->EGR = 1;
                        TIM6->SR  = 0;
                        stream_wr = 0;
                        stream_rd = 0;
                        stream_started = 0;
                        stream_last_rx = systick_ms;
                        audio_amplitude = (uint16_t)(audio_user_volume * 256 / 100);
                        audio_streaming = 1;
                        cdc_send_str("ok\r\n");
                    } else {
                        cdc_send_str("err rate\r\n");
                    }
                }
                else if (str_starts_with(cmd_buf, "audiotest=")) {
                    const char *name = cmd_buf + 10;
                    int ok = 1;
                    if (str_eq(name, "tap"))
                        audio_play_sequence(preset_tap, ARRAY_LEN(preset_tap));
                    else if (str_eq(name, "tone"))
                        audio_play_sequence(preset_tone, ARRAY_LEN(preset_tone));
                    else if (str_eq(name, "bell"))
                        audio_play_sequence(preset_bell, ARRAY_LEN(preset_bell));
                    else if (str_eq(name, "ring"))
                        audio_play_sequence(preset_ring, ARRAY_LEN(preset_ring));
                    else if (str_eq(name, "sweep"))
                        audio_play_sequence(preset_sweep, ARRAY_LEN(preset_sweep));
                    else
                        ok = 0;
                    cdc_send_str(ok ? "ok\r\n" : "err unknown\r\n");
                }
                else if (str_starts_with(cmd_buf, "volume=")) {
                    uint32_t vol;
                    if (parse_int(cmd_buf + 7, &vol) && vol <= 100) {
                        audio_user_volume = (uint8_t)vol;
                        /* Update amplitude for streaming (tone steps recalculate on next start) */
                        audio_amplitude = (uint16_t)(vol * 256 / 100);
                        cdc_send_str("ok\r\n");
                    }
                }
                /* ---- Touch diagnostic commands ---- */
                else if (str_eq(cmd_buf, "i2cscan")) {
                    cdc_send_str("Scanning I2C1...\r\n");
                    int found = 0;
                    for (uint8_t a = 1; a < 0x78; a++) {
                        I2C1->SR1 &= ~I2C_SR1_AF;
                        I2C1->CR1 |= I2C_CR1_START;
                        if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, 2)) {
                            I2C1->CR1 |= I2C_CR1_STOP; continue;
                        }
                        I2C1->DR = (a << 1);
                        uint32_t t0 = systick_ms;
                        while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)))
                            if ((systick_ms - t0) > 2) break;
                        if (I2C1->SR1 & I2C_SR1_ADDR) {
                            (void)I2C1->SR2;
                            I2C1->CR1 |= I2C_CR1_STOP;
                            char buf[8]; buf[0]='0'; buf[1]='x';
                            buf[2]="0123456789abcdef"[(a>>4)&0xF];
                            buf[3]="0123456789abcdef"[a&0xF];
                            buf[4]='\r'; buf[5]='\n'; buf[6]=0;
                            cdc_send_str(buf);
                            found++;
                        } else {
                            I2C1->SR1 &= ~I2C_SR1_AF;
                            I2C1->CR1 |= I2C_CR1_STOP;
                        }
                    }
                    if (!found) cdc_send_str("no devices\r\n");
                    cdc_send_str("done\r\n");
                }
                else if (str_eq(cmd_buf, "tpstatus")) {
                    const char *name = (touch_type == TOUCH_FT3308) ? "FT3308" :
                                       (touch_type == TOUCH_PARADE) ? "Parade" : "none";
                    cdc_send_str("[info:] touch=");
                    cdc_send_str(name);
                    cdc_send_str("\r\n");
                }
                else if (str_eq(cmd_buf, "tpread")) {
                    i2c_init();  /* Full bus recovery + reinit */

                    cdc_send_str("INT=");
                    cdc_send_str((GPIOB->IDR & (1 << 9)) ? "HIGH" : "LOW");
                    cdc_send_str("\r\n");

                    uint8_t addr = (touch_type != TOUCH_NONE) ? touch_addr : 0x20;

                    /* Test 1: addr+W ACK check (same as probe) */
                    cdc_send_str("W-ACK: ");
                    if (i2c_probe(addr) == 0)
                        cdc_send_str("yes\r\n");
                    else
                        cdc_send_str("no\r\n");

                    /* Test 2: addr+R ACK check */
                    cdc_send_str("R-ACK: ");
                    {
                        I2C1->SR1 &= ~I2C_SR1_AF;
                        I2C1->CR1 |= I2C_CR1_START;
                        int ok = -1;
                        if (i2c_wait(&I2C1->SR1, I2C_SR1_SB, 5) == 0) {
                            I2C1->DR = (addr << 1) | 1;  /* addr+R */
                            uint32_t t0 = systick_ms;
                            while (!(I2C1->SR1 & (I2C_SR1_ADDR | I2C_SR1_AF)))
                                if ((systick_ms - t0) > 5) break;
                            if (I2C1->SR1 & I2C_SR1_ADDR) {
                                (void)I2C1->SR2;
                                ok = 0;
                            }
                        }
                        I2C1->CR1 &= ~I2C_CR1_ACK;
                        I2C1->CR1 |= I2C_CR1_STOP;
                        I2C1->SR1 &= ~I2C_SR1_AF;
                        /* Drain any byte */
                        if (ok == 0 && (I2C1->SR1 & I2C_SR1_RXNE))
                            (void)I2C1->DR;
                        cdc_send_str(ok == 0 ? "yes\r\n" : "no\r\n");
                        i2c_init();  /* Recover */
                    }

                    /* Show discovered touch register */
                    { char r[24] = "touch_reg: page=X reg=XX\r\n";
                      r[16] = "0123456789abcdef"[tp_touch_page & 0xF];
                      r[22] = "0123456789abcdef"[(tp_touch_reg>>4)&0xF];
                      r[23] = "0123456789abcdef"[tp_touch_reg&0xF];
                      cdc_send_str(r);
                    }
                    /* Read 6 bytes from discovered register */
                    cdc_send_str("touch: ");
                    { uint8_t buf[6];
                      if (tp_page_read(addr, tp_touch_page, tp_touch_reg, buf, 6) == 0) {
                          for (int i = 0; i < 6; i++) {
                              char h[4];
                              h[0]="0123456789abcdef"[(buf[i]>>4)&0xF];
                              h[1]="0123456789abcdef"[buf[i]&0xF];
                              h[2]=(i<5)?' ':'\r'; h[3]=(i<5)?0:'\n';
                              cdc_send((const uint8_t*)h, (i<5)?3:4);
                          }
                      } else { cdc_send_str("fail\r\n"); i2c_init(); }
                    }
                }
                /* else: unknown command, ignore */

                cmd_len = 0;
            } else {
                /* Accumulate character */
                if (cmd_len < CMD_BUF_SIZE - 1) {
                    cmd_buf[cmd_len++] = (char)c;
                }
            }
        }
    }

    return 0;
}

/* ======================================================================
 * Reset handler (entry point from vector table)
 * ====================================================================== */
void Reset_Handler(void)
{
    /* Copy .data from flash to RAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    /* Zero .bss */
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

    /* Init clocks */
    SystemInit_Custom();

    /* Call main */
    main();

    /* Should never return */
    while (1)
        ;
}
