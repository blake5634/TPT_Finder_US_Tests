#include "US_TST.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"   // uSec delay
#include "soc/gpio_reg.h"

#define BIT_RISING_MASK   (1UL << GPIO_PIN_1)   // this pin: 0 -> 1
#define BIT_FALLING_MASK  (1UL << GPIO_PIN_2)   // this pin: 1 -> 0

/**
 * Configure GPIO_PIN_1 and GPIO_PIN_2 as outputs.
 */
void gpio_toggle_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Start low */
    gpio_set_level(GPIO_PIN_1, 0);
    gpio_set_level(GPIO_PIN_2, 0);
}

/**
 * Set both pins high, delay 1 ms, clear both pins, delay 1 ms. Repeat forever.
 *
 * NOTE: vTaskDelay(1) blocks for one RTOS tick. With the default
 * configTICK_RATE_HZ = 100, one tick is 10 ms, not 1 ms. To actually get
 * ~1 ms delays you must raise CONFIG_FREERTOS_HZ to 1000 in `idf.py menuconfig`
 * (Component config -> FreeRTOS -> Tick rate (Hz)). Even at 1000 Hz, this is
 * tick-resolution timing, not a hard real-time guarantee -- expect some
 * jitter since the task can be preempted between the two delay calls.
 */
void gpio_toggle_task(void *arg)
{
    int cycle=0;
    while (1) {
        // gpio_set_level(GPIO_PIN_1, 1);  // t=0
        // gpio_set_level(GPIO_PIN_2, 1);  // t+250ns
        REG_WRITE(GPIO_OUT_W1TS_REG, BIT_RISING_MASK);   // pin1: 0->1   t=0
        REG_WRITE(GPIO_OUT_W1TC_REG, BIT_FALLING_MASK);  // pin2: 1->0   t+50ns

        // vTaskDelay(pdMS_TO_TICKS(100));
        esp_rom_delay_us(10);

        // gpio_set_level(GPIO_PIN_1, 0);
        // gpio_set_level(GPIO_PIN_2, 0);
        REG_WRITE(GPIO_OUT_W1TS_REG, BIT_FALLING_MASK);  // pin2: 0->1
        REG_WRITE(GPIO_OUT_W1TC_REG, BIT_RISING_MASK);   // pin1: 1->0
        // vTaskDelay(pdMS_TO_TICKS(100));
        esp_rom_delay_us(10);
        cycle++;
        /*if (! cycle%500){
            ESP_LOGI("test", "I'm running...");
        } */
    }
}

void app_main(void)
{
    gpio_toggle_init();
    vTaskDelay(2);
    xTaskCreate(gpio_toggle_task, "gpio_toggle_task", 2048, NULL, 5, NULL);
}
