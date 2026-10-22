/*
 * Serial console commands (type them in ESP Launchpad's console or any serial monitor):
 *   team <name>          set your team name (saved to NVS, takes effect immediately)
 *   wifi <ssid> [pass]   change Wi-Fi network (saved to NVS, board restarts)
 *   status               show current settings and detector state
 *   snap                 print the 96x96 image the model last saw
 */
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static int cmd_team(int argc, char **argv)
{
    if (argc < 2) {
        printf("Team is \"%s\". Usage: team <name>\n", g_config.team);
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
    if (config_set_team(name) != ESP_OK) {
        printf("Invalid name. Use 1-%d letters, digits, space, - _ .\n", TEAM_MAX_LEN);
        return 1;
    }
    printf("Team set to \"%s\" - look for it on the big screen!\n", g_config.team);
    net_notify();
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("Wi-Fi is \"%s\". Usage: wifi <ssid> [password]\n", g_config.ssid);
        return 0;
    }
    if (config_set_wifi(argv[1], argc > 2 ? argv[2] : "") != ESP_OK) {
        printf("Invalid SSID/password\n");
        return 1;
    }
    printf("Saved. Restarting to join \"%s\"...\n", argv[1]);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    detect_state_t st;
    state_get(&st);
    printf("team=\"%s\" wifi=\"%s\"\n", g_config.team, g_config.ssid);
    printf("person=%s score=%d%% seen=%lu inference=%lu ms\n", st.person ? "YES" : "no", st.score,
           (unsigned long)st.seen, (unsigned long)st.infer_ms);
    return 0;
}

static int cmd_snap(int argc, char **argv)
{
    frame_print();
    return 0;
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "workshop>";

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
        // never reboot-loop over the console: detection + Wi-Fi keep working without it
        ESP_LOGE("console", "Console unavailable (%s)", esp_err_to_name(err));
        return;
    }

    const esp_console_cmd_t cmds[] = {
        { .command = "team", .help = "Set team name: team <name>", .func = cmd_team },
        { .command = "wifi", .help = "Set Wi-Fi: wifi <ssid> [password]", .func = cmd_wifi },
        { .command = "status", .help = "Show settings and detector state", .func = cmd_status },
        { .command = "snap", .help = "Print the 96x96 image the model just saw (hex)", .func = cmd_snap },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    esp_console_register_help_command();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
