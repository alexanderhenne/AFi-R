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
    /* IRQ 0..66: all default */
    [16 + 0 ... 16 + 66] = Default_Handler,
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
    /* Enable clocks: DAC, TIM4, GPIOA, GPIOB, GPIOC */
    RCC->APB1ENR |= (1 << 29)   /* DACEN */
                  | (1 << 2);    /* TIM4EN */
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

    /* --- TIM4 PWM on PB6/PB7/PB8/PB9 (CH1-CH4) --- */
    /* Configure PB6-PB9 as AF2 (TIM4) */
    for (int pin = 6; pin <= 9; pin++) {
        gpio_set_af(GPIOB, pin, 2);  /* AF2 = TIM4 */
    }
    /* TIM4: PWM mode 1, max duty cycle, ~1kHz at 120MHz/4=30MHz APB1 timer */
    #define TIM4_BASE 0x40000800UL
    volatile uint32_t *TIM4 = (volatile uint32_t *)TIM4_BASE;
    TIM4[0x00/4] = 0;        /* CR1: disabled */
    TIM4[0x2C/4] = 29999;    /* ARR: period */
    TIM4[0x18/4] = 0x6868;   /* CCMR1: PWM mode 1 on CH1 and CH2 */
    TIM4[0x1C/4] = 0x6868;   /* CCMR2: PWM mode 1 on CH3 and CH4 */
    TIM4[0x20/4] = 0x1111;   /* CCER: enable all 4 channels */
    TIM4[0x34/4] = 29999;    /* CCR1: max duty */
    TIM4[0x38/4] = 29999;    /* CCR2: max duty */
    TIM4[0x3C/4] = 29999;    /* CCR3: max duty */
    TIM4[0x40/4] = 29999;    /* CCR4: max duty */
    TIM4[0x00/4] = 1;        /* CR1: enable */

    /* --- Also try GPIO brute force on common backlight pins --- */
    /* Set PB0, PB1, PB10, PB11, PC6, PC7 as output high */
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
