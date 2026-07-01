#ifndef MAIN_H
#define MAIN_H

#include "driver/gpio.h"
#include "soc/gpio_reg.h"

/* ---- Pin configuration ---- */
#define GPIO_XTAL_DRIVE        GPIO_NUM_9   /* RMT burst output: 10 pulses @ ~1.818 MHz */
#define GPIO_TRISTATE_DRIVE    GPIO_NUM_10   /* RMT channel for state: low during burst, high once burst completes */
#define GPIO_STATE        GPIO_NUM_11   /* Gate pin: low during burst, high once burst completes */
#define GPIO_TEST         GPIO_NUM_12   /* Test pin: Pulse after vTaskDelay returns */

/* GPIO Tri-State STATES
 */
#define STATE_HI_Z  1
#define STATE_LO_Z  0

#define GPIO_OUTPUT_PINS_MASK  (1ULL << GPIO_STATE | 1ULL<<GPIO_TEST)
#define GPIO_STATE_BITMASK         (1UL << GPIO_STATE)

/* ---- Burst / RMT configuration ---- */
#define PULSE_COUNT_PER_BURST   13     /* number of full square-wave cycles per burst */

/* RMT tick resolution. 80 MHz APB / 22 = 3,636,364 Hz.
 * With 1 tick high + 1 tick low per cycle, this yields ~1.818182 MHz
 * (+1.01% vs. the 1.8 MHz target -- closest achievable with an integer
 * APB clock divider; confirmed acceptable for this application). */
#define RMT_RESOLUTION_HZ       3636364U
#define RMT_TICKS_PER_CYCLE     2U

/* ---- GPTimer configuration ----
 * 10 MHz resolution (100 ns/tick) chosen so the burst duration
 * (10 cycles * 2 ticks / 3,636,364 Hz = 5.5 us exactly) lands on an
 * EXACT integer tick count (55) with zero additional rounding error
 * on top of the RMT frequency's own +1.01% offset. */
#define GPTIMER_RESOLUTION_HZ   10000000U
// timer pulse length (measured):   0.0994*ticks + 5.5 (us)
//  tick count = (del - 5.5)/ 0.0994
#define GPTIMER_ALARM_TICKS     211U   /* = 25.5us, latencies + RMT burst*/

/* ---- Function prototypes ---- */
void gpio_pin2_init(void);
void rmt_burst_init(void);
void gate_timer_init(void);
void rmt_burst_task(void *arg);

#endif /* MAIN_H */
