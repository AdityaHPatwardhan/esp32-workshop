/*
 * Workshop app 01: Hello Board + Blink
 *
 * Prints a report about the board (chip, memory, flash partitions, camera,
 * microphone, Wi-Fi networks nearby, running tasks, restart counters), then
 * blinks the LED. Press BOOT to restart and watch the counters.
 *
 * By the time app_main() runs, a lot has already happened:
 *   1. ROM bootloader (inside the chip, unchangeable) read the strapping
 *      pins, decided to boot from SPI flash, and loaded...
 *   2. ...the 2nd-stage bootloader from flash, which read the partition
 *      table, picked this app, mapped it so code can run straight from
 *      flash, and jumped to...
 *   3. ...the app startup code: started both CPU cores, set up the heap,
 *      started FreeRTOS, and finally called app_main() from the "main" task.
 * Press RST with the serial console open and read the log - every step is there.
 */
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "sdkconfig.h"
#include "board_report.h"

static const char *TAG = "blink";

#define BOOT_BUTTON_GPIO 0

#define LED_GPIO CONFIG_BLINK_GPIO

#if CONFIG_BLINK_ACTIVE_LOW
#define LED_ON_LEVEL 0
#else
#define LED_ON_LEVEL 1
#endif

/*
 * Turning the LED on or off is just writing a bit into a hardware register.
 * Registers are special memory addresses: writing to them changes real pins.
 *   W1TS = "write 1 to set"   -> pin goes HIGH
 *   W1TC = "write 1 to clear" -> pin goes LOW
 * GPIOs 0-31 live in one register, 32-48 in a second one.
 */
static void led_write(int on)
{
    int level = on ? LED_ON_LEVEL : !LED_ON_LEVEL;
#if CONFIG_BLINK_USE_REGISTERS
    if (LED_GPIO < 32) {
        REG_WRITE(level ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, 1UL << LED_GPIO);
    } else {
        REG_WRITE(level ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, 1UL << (LED_GPIO - 32));
    }
#else
    gpio_set_level(LED_GPIO, level);
#endif
}

void app_main(void)
{
    board_report();
    printf("Blinking the LED on GPIO%d. Press BOOT to restart.\n\n", LED_GPIO);

    // BOOT button: input with pull-up, reads 0 while pressed
    gpio_reset_pin(BOOT_BUTTON_GPIO);
    gpio_set_direction(BOOT_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    // Route the pin to the GPIO peripheral and make it an output.
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    int on = 0;
    uint32_t count = 0;
    while (1) {
        on = !on;
        led_write(on);
        if (on) {
            ESP_LOGI(TAG, "blink #%" PRIu32 " (LED on)", ++count);
        }
        // Not a busy-wait: this task sleeps and FreeRTOS lets the CPU do
        // other work (or idle) until the delay expires. We wake every 20 ms
        // to check the BOOT button.
        for (int t = 0; t < CONFIG_BLINK_PERIOD_MS / 2; t += 20) {
            if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                printf("\n>> BOOT pressed: restarting...\n");
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
