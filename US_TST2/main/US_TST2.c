#include "US_TST2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/gptimer.h"
#include "esp_log.h"

#include "soc/system_reg.h"      // SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_TIMERGROUP_CLK_EN
#include "soc/timer_group_reg.h" // TIMG_T0CONFIG_REG, TIMG_T0_DIVIDER, etc.


static const char *TAG = "rmt_burst test";

// defines for direct register version
#define APB_CLK_HZ          80000000UL
#define GPTIMER_RESOLUTION_HZ 10000000U   // 1 MHz — adjust to match your define
#define PRESCALE_VALUE          (APB_CLK_HZ / GPTIMER_RESOLUTION_HZ)  // = 80

static rmt_channel_handle_t s_rmt_chan   = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static rmt_symbol_word_t    s_burst_symbols[PULSE_COUNT_PER_BURST];

static gptimer_handle_t s_gate_timer = NULL;

/* -------------------------------------------------------------------- */
/* GPIO_STATE setup (plain GPIO, not RMT)                                */
/* -------------------------------------------------------------------- */
void gpio_pin2_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = GPIO_OUTPUT_PIN_SEL_PIN2,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Idle state: high (Hi-Z side of T/R switch, "not bursting") */
    gpio_set_level(GPIO_STATE, STATE_HI_Z);
}

/* -------------------------------------------------------------------- */
/* RMT burst generator on GPIO_XTAL_DRIVE                                     */
/* -------------------------------------------------------------------- */
void rmt_burst_init(void)
{
    /* Pre-build the burst waveform once. Each rmt_symbol_word_t = one
     * full cycle: 1 tick high, 1 tick low. */
    for (int i = 0; i < PULSE_COUNT_PER_BURST; i++) {
        s_burst_symbols[i].level0    = 1;
        s_burst_symbols[i].duration0 = 1;
        s_burst_symbols[i].level1    = 0;
        s_burst_symbols[i].duration1 = 1;
    }

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num           = GPIO_XTAL_DRIVE,
        .mem_block_symbols  = 64,  // standard block size!
        .resolution_hz      = RMT_RESOLUTION_HZ,
        .trans_queue_depth  = 1,
        .flags.invert_out   = false,
        .flags.with_dma     = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_rmt_chan));

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &s_copy_encoder));

    /* NOTE: no on_trans_done callback registered here -- GPIO_STATE's
     * Hi-Z edge is now driven by the independent GPTimer alarm below,
     * not by RMT's transaction-done event, to avoid RMT ISR/ping-pong
     * latency on the gating edge. */

    ESP_ERROR_CHECK(rmt_enable(s_rmt_chan));
}

/* -------------------------------------------------------------------- */
/* GPTimer one-shot alarm: fires GPIO_STATE high exactly GPTIMER_ALARM_TICKS
 * after being armed, independent of RMT's own done-callback latency.    */
/* -------------------------------------------------------------------- */
static bool IRAM_ATTR gate_timer_alarm_cb(gptimer_handle_t timer,
                                           const gptimer_alarm_event_data_t *edata,
                                           void *user_ctx)
{
    /* Direct register write -- same low-latency W1TS technique used
     * earlier for the manual GPIO toggle tests. */
    REG_WRITE(GPIO_OUT_W1TS_REG, GPIO_STATE_BITMASK);  // set STATE output to HiZ
    return false; /* no FreeRTOS task woken from this callback */
}

void gate_timer_init(void)
{

    gptimer_config_t timer_config = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = GPTIMER_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &s_gate_timer));



    // 1. Enable the timer group peripheral clock (required on ESP32-S2/S3/C3; no-op on classic ESP32)
    //    On classic ESP32 the group clocks are always on, but this is harmless.
    // DPORT_SET_PERI_CLK_EN(DPORT_TIMERGROUP_CLK_EN);   // classic ESP32
    // For ESP32-S3/C3/H2 use SYSTEM_PERIP_CLK_EN0_REG instead:
    REG_SET_BIT(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_TIMERGROUP_CLK_EN);

    // 2. Reset the timer (hold in reset, then release)
    // Reset Timer Group 0 via peripheral reset register
    REG_SET_BIT(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_TIMERGROUP_RST);
    REG_CLR_BIT(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_TIMERGROUP_RST);

    // 3. Set clock divider (prescaler) — bits [15:13] in T0CONFIG
    //    Field: TIMG_T0_DIVIDER, 16-bit value written to bits 28:13

    REG_SET_FIELD(TIMG_T0CONFIG_REG(0), TIMG_T0_DIVIDER, PRESCALE_VALUE);

    // 4. Set count direction: up-counting (TIMG_T0_INCREASE = 1)
    REG_SET_BIT(TIMG_T0CONFIG_REG(0),   TIMG_T0_INCREASE);

    // 5. Select clock source: APB (default); clear TIMG_T0_USE_XTAL
    REG_CLR_BIT(TIMG_T0CONFIG_REG(0),   TIMG_T0_USE_XTAL);

    // 6. Enable the timer
    REG_SET_BIT(TIMG_T0CONFIG_REG(0),   TIMG_T0_EN);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = gate_timer_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_gate_timer, &cbs, NULL));

    ESP_ERROR_CHECK(gptimer_enable(s_gate_timer));
    /* Timer is enabled but not started -- gptimer_start() is called
     * fresh each burst cycle in rmt_burst_task(), after resetting the
     * raw count to 0, so the alarm always fires exactly
     * GPTIMER_ALARM_TICKS after the burst is armed. */
}

/* -------------------------------------------------------------------- */
/* Main burst loop                                                       */
/* -------------------------------------------------------------------- */
void rmt_burst_task(void *arg)
{
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,   /* single-shot burst */
    };

    gptimer_alarm_config_t alarm_config = {
        .alarm_count          = GPTIMER_ALARM_TICKS,
        .reload_count         = 0,
        .flags.auto_reload_on_alarm = false,  /* one-shot per burst */
    };

    while (1) {
        /* Arm the gate: pin low, reset+arm the timer, then fire the burst.
         * Order matters here -- we want the timer armed and running
         * *before* (or as close as possible to) the RMT burst starting,
         * so the 5.5us window is measured from a consistent reference. */

        gptimer_stop(s_gate_timer);                         /* ensure clean state */
        gptimer_set_raw_count(s_gate_timer, 0);              /* reset count to 0 */
        gptimer_set_alarm_action(s_gate_timer, &alarm_config); /* re-arm one-shot alarm */

        // Set driver to Lo-Z mode
        REG_WRITE(GPIO_OUT_W1TC_REG, GPIO_STATE_BITMASK);   /* pin low: enter Low-Z window */

        /* GPIO_STATE will be driven high by gate_timer_alarm_cb() exactly
         * GPTIMER_ALARM_TICKS (5.5us) after gptimer_start() above --
         * independent of RMT's internal completion-callback latency. */
        gptimer_start(s_gate_timer);                         /* start counting now */

        // Start the drive pulses from RMT peripheral
        ESP_ERROR_CHECK(rmt_transmit(s_rmt_chan, s_copy_encoder,
                                      s_burst_symbols, sizeof(s_burst_symbols),
                                      &transmit_config));

       // ESP_LOGI(TAG, "Task Loop...");
        vTaskDelay(1);   /* block for exactly 1 OS tick before next burst */
    }
}

void app_main(void)
{
    gpio_pin2_init();
    ESP_LOGI(TAG, " GPIO init completed");

    rmt_burst_init();
    ESP_LOGI(TAG, " rmt burst init completed");

    gate_timer_init();
    ESP_LOGI(TAG, " timer init completed");

    xTaskCreate(rmt_burst_task, "rmt_burst_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, " task started");
}
