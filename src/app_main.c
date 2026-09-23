#include <stdio.h>
#include <string.h>
#include "secrets.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp32c3/rom/sha.h"

#define CMD_BUF_SIZE 128

static const char *files[] = {
    "secrets.txt",
    "user_data.csv",
    "calibration.dat",
    "log_0001.txt",
    "firmware.bin",
};

#define FILE_COUNT (sizeof(files) / sizeof(files[0]))

static void print_help(void) {
    printf("Commands:\n");
    printf("  help             - show this message\n");
    printf("  list <PIN>       - print an array of files\n");
}

void sha256(const char *input, uint8_t output[32]) {
    SHA_CTX ctx;

    ets_sha_enable();

    ets_sha_init(&ctx, SHA2_256);
    ets_sha_starts(&ctx, 0);

    ets_sha_update(
        &ctx,
        (const unsigned char *)input,
        strlen(input),
        true
    );

    ets_sha_finish(&ctx, output);

    ets_sha_disable();
}

bool check_pin(const char *pin) {
    uint8_t pin_hash[32];

    sha256(pin, pin_hash);

    if (memcmp(pin_hash, PIN_HASH, 32) != 0) {
        return false;
    }

    return true;
}

static void handle_list() {
    printf("Files:\n");

    for (size_t i = 0; i < FILE_COUNT; i++) {
        printf("  [%zu] %s\n", i, files[i]);
    }
}

static void run_command(char *line) {
    line[strcspn(line, "\r\n")] = '\0';

    if (strlen(line) == 0) {
        return;
    }

    char cmd[16] = {0};
    char arg[16] = {0};

    int argc = sscanf(line, "%15s %15s", cmd, arg);

    if (strcmp(cmd, "help") == 0) {

        print_help();

    } else if (strcmp(cmd, "list") == 0) {

        if (argc < 2) {
            printf("Usage: list <PIN>\n");
            return;
        }

        if (check_pin(arg)) {
            handle_list();
        } else {
            printf("Incorrect PIN\n");
        }
    }
}

static void usb_console_init(void) {
    usb_serial_jtag_driver_config_t usb_cfg =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();

    usb_serial_jtag_driver_install(&usb_cfg);
    usb_serial_jtag_vfs_use_driver();
}



void app_main(void) {
    char line[CMD_BUF_SIZE];
    size_t idx = 0;

    usb_console_init();

    printf("\nESP32-C3 command console ready. Type 'help' for commands.\n");
    printf("> ");
    fflush(stdout);

    while (1) {

        int c = fgetc(stdin);

        if (c == EOF) {
            continue;
        }

        if (c == '\n') {

            line[idx] = '\0';

            putchar('\n');

            run_command(line);

            idx = 0;

            printf("> ");
            fflush(stdout);

            continue;
        }

        if (c == '\r') {
            continue;
        }

        if (idx < CMD_BUF_SIZE - 1) {
            line[idx++] = (char)c;
        }

        
    }
}