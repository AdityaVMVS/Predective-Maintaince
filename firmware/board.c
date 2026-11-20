#include "board.h"
#include "stm32f767xx.h"

static TaskHandle_t acquisition;
static volatile bool dma_ok;
static uint8_t tx[7] __attribute__((section(".dma"), aligned(32)));
static uint8_t rx[7] __attribute__((section(".dma"), aligned(32)));

static void alternate(GPIO_TypeDef *gpio, unsigned pin, unsigned af)
{
    gpio->MODER = (gpio->MODER & ~(3u << (2 * pin))) | 2u << (2 * pin);
    gpio->OSPEEDR |= 2u << (2 * pin);
    unsigned shift = 4 * (pin % 8);
    gpio->AFR[pin / 8] = (gpio->AFR[pin / 8] & ~(15u << shift)) | af << shift;
}

void board_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                   RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_DMA2EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    GPIOA->BSRR = 1u << 4;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 8)) | 1u << 8;
    alternate(GPIOA, 5, 5);
    alternate(GPIOA, 6, 5);
    alternate(GPIOB, 5, 5);
    GPIOB->MODER = (GPIOB->MODER & ~((3u << 2) | (3u << 14) | (3u << 28))) |
                   (1u << 14) | (1u << 28);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << 2)) | (2u << 2);
    alternate(GPIOD, 8, 7);
    alternate(GPIOD, 9, 7);
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPOL |
               SPI_CR1_CPHA | SPI_CR1_BR_0;
    SPI1->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    SPI1->CR1 |= SPI_CR1_SPE;
    USART3->BRR = 139;
    USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xc5acce55u;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t board_cycles(void) { return DWT->CYCCNT; }

static void pause_poll(void)
{
    __NOP();
}

static bool wait_spi(uint32_t flag, bool set)
{
    uint32_t start = board_cycles();
    unsigned spins = 32000;
    while (((SPI1->SR & flag) != 0) != set) {
        if (!--spins || board_cycles() - start > 16000u) return false;
    }
    return true;
}

static void chip_select(bool active)
{
    GPIOA->BSRR = 1u << (active ? 20 : 4);
}

static void spi_recover(void)
{
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    while (SPI1->SR & SPI_SR_RXNE) (void)*(volatile uint8_t *)&SPI1->DR;
    (void)SPI1->SR;
    SPI1->CR1 |= SPI_CR1_SPE;
}

static bool register_transfer(uint8_t command, uint8_t value, uint8_t *output)
{
    chip_select(true);
    bool ok = true;
    uint8_t received = 0;
    for (unsigned i = 0; i < 2 && ok; ++i) {
        ok = wait_spi(SPI_SR_TXE, true);
        if (!ok) break;
        *(volatile uint8_t *)&SPI1->DR = i ? value : command;
        ok = wait_spi(SPI_SR_RXNE, true);
        if (ok) received = *(volatile uint8_t *)&SPI1->DR;
    }
    ok = ok && wait_spi(SPI_SR_BSY, false);
    chip_select(false);
    if (!ok) spi_recover();
    if (output) *output = received;
    return ok;
}

static bool read_reg(uint8_t reg, uint8_t *out) { return register_transfer(reg | 0x80, 0, out); }
static bool write_reg(uint8_t reg, uint8_t value) { return register_transfer(reg, value, NULL); }

bool sensor_init(void)
{
    uint8_t id, ignored;
    return read_reg(0x00, &id) && id == 0xe5 &&
           write_reg(0x2d, 0) && write_reg(0x2e, 0) && write_reg(0x38, 0) &&
           write_reg(0x2c, 0x0e) && write_reg(0x31, 0x0a) && write_reg(0x2f, 0) &&
           write_reg(0x38, 0x90) && read_reg(0x30, &ignored) &&
           write_reg(0x2e, 3) && write_reg(0x2d, 8);
}

bool sensor_status(uint8_t *source, uint8_t *count)
{
    uint8_t id, status;
    if (!read_reg(0, &id) || id != 0xe5 ||
        !read_reg(0x30, source) || !read_reg(0x39, &status)) return false;
    *count = status & 0x3fu;
    return *count <= 32u;
}

void sensor_interrupts(TaskHandle_t owner)
{
    acquisition = owner;
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~(15u << 4)) | (1u << 4);
    EXTI->RTSR |= 1u << 1;
    EXTI->PR = 1u << 1;
    EXTI->IMR |= 1u << 1;
    NVIC_SetPriority(EXTI1_IRQn, 6);
    NVIC_SetPriority(DMA2_Stream0_IRQn, 6);
    NVIC_SetPriority(DMA2_Stream3_IRQn, 6);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

void EXTI1_IRQHandler(void)
{
    EXTI->PR = 1u << 1;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveIndexedFromISR(acquisition, WATERMARK_EVENT, &wake);
    portYIELD_FROM_ISR(wake);
}

static void notify_dma(bool ok)
{
    /* A late RX success must not mask a TX error already reported. */
    dma_ok = dma_ok && ok;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveIndexedFromISR(acquisition, DMA_EVENT, &wake);
    portYIELD_FROM_ISR(wake);
}

#define DMA_RX_FLAGS 0x3du
#define DMA_TX_FLAGS (0x3du << 22)
void DMA2_Stream0_IRQHandler(void)
{
    uint32_t flags = DMA2->LISR & DMA_RX_FLAGS;
    DMA2->LIFCR = DMA_RX_FLAGS;
    if (flags & (DMA_LISR_TCIF0 | DMA_LISR_TEIF0 | DMA_LISR_DMEIF0)) {
        notify_dma((flags & (DMA_LISR_FEIF0 | DMA_LISR_TEIF0 | DMA_LISR_DMEIF0)) == 0);
    }
}

void DMA2_Stream3_IRQHandler(void)
{
    uint32_t flags = DMA2->LISR & DMA_TX_FLAGS;
    DMA2->LIFCR = DMA_TX_FLAGS;
    if (flags & (DMA_LISR_TEIF3 | DMA_LISR_DMEIF3 | DMA_LISR_FEIF3)) notify_dma(false);
}

bool sensor_read(Sample *sample)
{
    (void)ulTaskNotifyTakeIndexed(DMA_EVENT, pdTRUE, 0);
    dma_ok = true;
    tx[0] = 0xf2; /* Read and increment from DATAX0. */
    for (unsigned i = 1; i < sizeof(tx); ++i) tx[i] = 0;
    DMA2->LIFCR = DMA_RX_FLAGS | DMA_TX_FLAGS;
    DMA2_Stream0->PAR = DMA2_Stream3->PAR = (uint32_t)&SPI1->DR;
    DMA2_Stream0->M0AR = (uint32_t)rx;
    DMA2_Stream3->M0AR = (uint32_t)tx;
    DMA2_Stream0->NDTR = DMA2_Stream3->NDTR = sizeof(rx);
    DMA2_Stream0->FCR = DMA2_Stream3->FCR = 0;
    const uint32_t common = 3u << DMA_SxCR_CHSEL_Pos | DMA_SxCR_MINC |
                            DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    DMA2_Stream0->CR = common | DMA_SxCR_TCIE | DMA_SxCR_PL_1;
    DMA2_Stream3->CR = common | DMA_SxCR_DIR_0;
    chip_select(true);
    __DMB();
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;
    bool ok = ulTaskNotifyTakeIndexed(DMA_EVENT, pdTRUE, pdMS_TO_TICKS(3)) != 0;
    SPI1->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    unsigned spins = 32000;
    while ((DMA2_Stream0->CR | DMA2_Stream3->CR) & DMA_SxCR_EN) {
        if (!--spins) {
            chip_select(false);
            NVIC_SystemReset();
        }
    }
    ok = ok && dma_ok && wait_spi(SPI_SR_BSY, false) && !(SPI1->SR & SPI_SR_OVR);
    chip_select(false);
    /* ADXL345 needs at least 5 us between FIFO sample reads. */
    uint32_t start = board_cycles();
    for (unsigned i = 0; i < 96 && board_cycles() - start < 80; ++i) pause_poll();
    if (!ok) {
        spi_recover();
        return false;
    }
    __DMB();
    for (unsigned i = 0; i < 3; ++i) {
        sample->axis[i] = (int16_t)(rx[1 + 2 * i] | (uint16_t)rx[2 + 2 * i] << 8);
    }
    return true;
}

void board_led(Condition condition)
{
    uint32_t set = condition == NORMAL ? (1u << 7) :
                   condition == LEARNING ? 0 : (1u << 14);
    GPIOB->BSRR = set | (((1u << 7 | 1u << 14) & ~set) << 16);
}

void uart_send(const char *text)
{
    while (*text) {
        while (!(USART3->ISR & USART_ISR_TXE)) {}
        USART3->TDR = (uint8_t)*text++;
    }
}

int uart_receive(void)
{
    if (USART3->ISR & USART_ISR_ORE) USART3->ICR = USART_ICR_ORECF;
    return USART3->ISR & USART_ISR_RXNE ? (int)(uint8_t)USART3->RDR : -1;
}
