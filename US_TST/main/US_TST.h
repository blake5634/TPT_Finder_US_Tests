#ifndef US_TST_H
#define US_TST_H

#include "driver/gpio.h"

/* ---- Pin configuration ---- */
/* Change these to whichever two GPIOs you want to toggle. */
#define GPIO_PIN_1   GPIO_NUM_9
#define GPIO_PIN_2   GPIO_NUM_10

#define GPIO_OUTPUT_PIN_SEL  ((1ULL << GPIO_PIN_1) | (1ULL << GPIO_PIN_2))

/* ---- Function prototypes ---- */
void gpio_toggle_init(void);
void gpio_toggle_task(void *arg);

#endif /* US_TST_H */
