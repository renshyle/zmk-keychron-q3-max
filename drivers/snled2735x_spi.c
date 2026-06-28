#define DT_DRV_COMPAT sonix_snled2735x_spi

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

#define WAKEUP_TIME K_USEC(128)

#define PAGE_SHIFT 8
#define PAGE0 (0 << PAGE_SHIFT)
#define PAGE1 (1 << PAGE_SHIFT)
#define PAGE3 (3 << PAGE_SHIFT)
#define PAGE4 (4 << PAGE_SHIFT)

// Bytes per one CB in the register
#define REG_LED_CONTROL_CB_WIDTH 2
#define REG_LED_OPEN_CB_WIDTH 2
#define REG_LED_SHORT_CB_WIDTH 2
#define REG_PWM_CB_WIDTH 16
#define REG_LED_TUNE_CB_WIDTH 1

#define REG_LED_CONTROL_ADDR 0
#define REG_LED_OPEN_ADDR 0x18
#define REG_LED_SHORT_ADDR 0x30
#define REG_PWM_ADDR 0
#define REG_FUNCTION_ADDR 0
#define REG_LED_TUNE_ADDR 0

#define REG_FUNCTION_SW_SHUTDOWN 0

#define HEADER_REG_WRITE (0 << 7)
#define HEADER_REG_READ (0 << 7)
#define HEADER_CHECKING_PATTERN 0x20
#define HEADER_PAGE(page) page

#define REG_PAGE(reg) (reg >> PAGE_SHIFT)
#define REG_ADDR(reg) (reg & 0xff)

enum snled2735x_register {
    REG_LED_CONTROL = PAGE0 | REG_LED_CONTROL_ADDR,
    REG_LED_OPEN = PAGE0 | REG_LED_OPEN_ADDR,
    REG_LED_SHORT = PAGE0 | REG_LED_SHORT_ADDR,
    REG_PWM = PAGE1 | REG_PWM_ADDR,
    REG_FUNCTION = PAGE3 | REG_FUNCTION_ADDR,
    REG_LED_TUNE = PAGE4 | REG_LED_TUNE_ADDR,
};

struct snled2735x_config {
    struct spi_dt_spec bus;
    struct gpio_dt_spec sdb;
    int cas;
    int cbs;
};

static int snled2735x_write_register(const struct device *dev,
                                     enum snled2735x_register reg, uint8_t idx,
                                     uint8_t value)
{
    const struct snled2735x_config *config = dev->config;
    uint8_t data[3];
    int err;

    data[0] =
        HEADER_REG_WRITE | HEADER_CHECKING_PATTERN | HEADER_PAGE(REG_PAGE(reg));
    data[1] = REG_ADDR(reg) + idx;
    data[2] = value;

    struct spi_buf buf = (struct spi_buf){.buf = data, .len = 3};
    struct spi_buf_set buf_set;
    buf_set.count = 1;
    buf_set.buffers = &buf;
    err = spi_write_dt(&config->bus, &buf_set);

    if (err) {
        printk("failed to write to snled2735x register\n");
        return err;
    }

    return 0;
}

static int snled2735x_set_led_pwm(const struct device *dev, uint8_t b,
                                  uint8_t a, uint8_t duty)
{
    return snled2735x_write_register(dev, REG_PWM, b * REG_PWM_CB_WIDTH + a,
                                     duty);
}

static int snled2735x_hw_sleep(const struct device *dev, bool sleep)
{
    const struct snled2735x_config *config = dev->config;
    int err = gpio_pin_configure_dt(&config->sdb, sleep ? GPIO_OUTPUT_INACTIVE
                                                        : GPIO_OUTPUT_ACTIVE);

    if (err) {
        return err;
    }

    if (!sleep) {
        k_sleep(WAKEUP_TIME);
    }

    return 0;
}

static int snled2735x_spi_init(const struct device *dev)
{
    const struct snled2735x_config *config = dev->config;
    int err;

    if (!spi_is_ready_dt(&config->bus) || !gpio_is_ready_dt(&config->sdb)) {
        return -ENODEV;
    }

    err = snled2735x_hw_sleep(dev, false);

    if (err) {
        return err;
    }

    err = snled2735x_write_register(dev, REG_FUNCTION, REG_FUNCTION_SW_SHUTDOWN,
                                    1);

    if (err) {
        return err;
    }

    for (int b = 0; b < config->cbs; b++) {
        for (int i = 0; i < REG_LED_CONTROL_CB_WIDTH; i++) {
            err = snled2735x_write_register(
                dev, REG_LED_CONTROL, b * REG_LED_CONTROL_CB_WIDTH + i, 0xff);

            if (err) {
                return err;
            }
        }
    }

    uint32_t x = (uint32_t) dev;
    for (int b = 0; b < config->cbs; b++) {
        for (int a = 0; a < config->cas; a++) {
            // xorshift for quick n easy randomness
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            err = snled2735x_set_led_pwm(dev, b, a, x & 0xff);
        }
    }

    return 0;
}

#define SNLED2735X_DEVICE(idx)                                                 \
    static const struct snled2735x_config snled2735x_##idx##_config = {        \
        .bus = SPI_DT_SPEC_INST_GET(                                           \
            idx, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8), 0),  \
        .sdb = GPIO_DT_SPEC_INST_GET_BY_IDX(idx, sdb_gpios, 0),                \
        .cbs = DT_INST_PROP(idx, cbs),                                         \
        .cas = DT_INST_PROP(idx, cas),                                         \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(idx, snled2735x_spi_init, NULL, NULL,                \
                          &snled2735x_##idx##_config, POST_KERNEL,             \
                          90 /* CONFIG_LED_STRIP_INIT_PRIORITY */, NULL);

DT_INST_FOREACH_STATUS_OKAY(SNLED2735X_DEVICE)
