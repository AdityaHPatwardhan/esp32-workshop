/*
 * Serial console commands (type them in ESP Launchpad's console or any serial monitor):
 *   team <name>          set your team name (saved in flash, shows on the projector)
 *   wifi <ssid> [pass]   change Wi-Fi network (saved in flash, board restarts)
 */
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "hello_net.h"

static int cmd_team(int argc, char **argv)
{
    if (argc < 2) {
        printf("Team is \"%s\". Usage: team <name>\n", team_get());
        return 0;
    }
    // allow multi-word names: team Code Ninjas
    char name[64] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            strlcat(name, " ", sizeof(name));
        }
        strlcat(name, argv[i], sizeof(name));
    }
    if (team_set(name) != ESP_OK) {
        printf("Invalid name. Use 1-%d letters, digits, space, - _ .\n", TEAM_MAX_LEN);
        return 1;
    }
    printf("Team set to \"%s\" - look for it on the big screen!\n", team_get());
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi <ssid> [password]\n");
        return 0;
    }
    if (wifi_settings_set(argv[1], argc > 2 ? argv[2] : "") != ESP_OK) {
        printf("Invalid SSID/password\n");
        return 1;
    }
    printf("Saved. Restarting to join \"%s\"...\n", argv[1]);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "hello>";

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t dev = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&dev, &repl_cfg, &repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev, &repl_cfg, &repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t dev = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_cdc(&dev, &repl_cfg, &repl);
#else
    return;
#endif
    if (err != ESP_OK) {
        ESP_LOGE("console", "Console unavailable (%s)", esp_err_to_name(err));
        return;
    }

    const esp_console_cmd_t cmds[] = {
        { .command = "team", .help = "Set team name: team <name>", .func = cmd_team },
        { .command = "wifi", .help = "Set Wi-Fi: wifi <ssid> [password]", .func = cmd_wifi },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    esp_console_register_help_command();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
