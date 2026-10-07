/*
 * ESP32-C3 slot store. Talks via host tool (scripts/tool.py).
 *
 * Request  : SYNC  OPCODE  PIN_LEN  PIN[PIN_LEN]  <payload>
 *   LIST   : (no payload)
 *   READ   : SLOT
 *   WRITE  : SLOT  LENGTH  DATA[LENGTH]          (LENGTH 0 clears the slot)
 *
 * Response : SYNC  STATUS  LENGTH  DATA[LENGTH]
 *   LIST   : DATA = SLOT_COUNT bytes, the stored length of each slot (0 = empty)
 *   READ   : DATA = slot contents
 *   WRITE  : no data
 *
 * SYNC lets both sides skip stray bytes (boot logs, etc).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"

#include "secrets.h"
#include "crypto.h"

// Message Protocol
#define SYNC            0xA5
 
#define OP_WRITE        1
#define OP_READ         2
#define OP_LIST         3
 
#define ST_OK           0
#define ST_BAD_PIN      1
#define ST_LOCKED       2
#define ST_BAD_SLOT     3
#define ST_EMPTY        4
#define ST_BAD_REQUEST  5
 
// Limits
#define SLOT_COUNT      5
#define MAX_DATA        255
#define MAX_NAME        15
#define PIN_MAX         15
#define MAX_ATTEMPTS    3
#define IO_TIMEOUT_MS   1000
#define FAIL_DELAY_MS   500
 
typedef struct {
    uint8_t op;
    char    pin[PIN_MAX + 1];
    uint8_t slot;
    uint8_t len;
    uint8_t data[MAX_DATA];
} request_t;
 
typedef struct {
    uint8_t len;
    uint8_t data[MAX_DATA];
} slot_t;
 
static slot_t    slots[SLOT_COUNT];
static request_t req;
static int       failed_attempts = 0;

static nvs_handle_t store;

static void storage_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_open("files", NVS_READWRITE, &store));

    for (int i = 0; i < SLOT_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "slot%d", i);

        size_t size = sizeof(slots[i]);
        esp_err_t err = nvs_get_blob(store, key, &slots[i], &size);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;  // New slot: static RAM is already zeroed.
        }

        ESP_ERROR_CHECK(err);

        if (size != sizeof(slots[i])) {
            memset(&slots[i], 0, sizeof(slots[i]));
        }
    }
}

static bool check_pin(const char *pin) {
    uint8_t hash[32];
 
    sha256(pin, hash);
    return memcmp(hash, PIN_HASH, 32) == 0;
}
 
static bool read_exact(uint8_t *buf, size_t n) {
    size_t got = 0;
    int idle_ms = 0;
 
    while (got < n) {
        int r = usb_serial_jtag_read_bytes(buf + got, n - got, pdMS_TO_TICKS(50));
        if (r > 0) {
            got += r;
            idle_ms = 0;
        } else {
            idle_ms += 50;
            if (idle_ms >= IO_TIMEOUT_MS) {
                return false;
            }
        }
    }
    return true;
}
 
static void drain_rx(void) {
    uint8_t tmp[64];
 
    while (usb_serial_jtag_read_bytes(tmp, sizeof(tmp), pdMS_TO_TICKS(20)) > 0) {
    }
}
 
static void wait_for_sync(void) {
    uint8_t b;
 
    do {
        if (usb_serial_jtag_read_bytes(&b, 1, portMAX_DELAY) != 1) {
            b = 0;
        }
    } while (b != SYNC);
}
 
static void send_response(uint8_t status, const uint8_t *data, uint8_t len) {
    uint8_t frame[3 + MAX_DATA];
 
    frame[0] = SYNC;
    frame[1] = status;
    frame[2] = len;
    if (len > 0) {
        memcpy(&frame[3], data, len);
    }
    usb_serial_jtag_write_bytes(frame, 3 + len, pdMS_TO_TICKS(1000));
}

static bool recv_request(request_t *r) {
    uint8_t hdr[2];
 
    memset(r, 0, sizeof(*r));
 
    if (!read_exact(hdr, sizeof(hdr))) {
        return false;
    }
    r->op = hdr[0];
 
    uint8_t pin_len = hdr[1];
    if (pin_len == 0 || pin_len > PIN_MAX) {
        return false;
    }
    if (!read_exact((uint8_t *)r->pin, pin_len)) {
        return false;
    }
 
    switch (r->op) {
    case OP_LIST:
        return true;
    case OP_READ:
        return read_exact(&r->slot, 1);
    case OP_WRITE:
        if (!read_exact(&r->slot, 1) || !read_exact(&r->len, 1)) {
            return false;
        }
        return r->len == 0 || read_exact(r->data, r->len);
    default:
        return false;
    }
}

static void handle_list(void) {
    uint8_t lens[SLOT_COUNT];
 
    for (int i = 0; i < SLOT_COUNT; i++) {
        lens[i] = slots[i].len;
    }
    send_response(ST_OK, lens, SLOT_COUNT);
}
 
static void handle_read(const request_t *r) {
    if (r->slot >= SLOT_COUNT) {
        send_response(ST_BAD_SLOT, NULL, 0);
    } else if (slots[r->slot].len == 0) {
        send_response(ST_EMPTY, NULL, 0);
    } else {
        send_response(ST_OK, slots[r->slot].data, slots[r->slot].len);
    }
}
 
static void handle_write(const request_t *r) {
    if (r->slot >= SLOT_COUNT) {
        send_response(ST_BAD_SLOT, NULL, 0);
        return;
    }

    memset(&slots[r->slot], 0, sizeof(slot_t));
    memcpy(slots[r->slot].data, r->data, r->len);
    slots[r->slot].len = r->len;

    char key[16];
    snprintf(key, sizeof(key), "slot%d", r->slot);

    ESP_ERROR_CHECK(nvs_set_blob(
        store, key, &slots[r->slot], sizeof(slot_t)));
    ESP_ERROR_CHECK(nvs_commit(store));

    send_response(ST_OK, NULL, 0);
}

static void dispatch(const request_t *r) {
    if (failed_attempts >= MAX_ATTEMPTS) {
        send_response(ST_LOCKED, NULL, 0);
        return;
    }
    if (!check_pin(r->pin)) {
        failed_attempts++;
        vTaskDelay(pdMS_TO_TICKS(FAIL_DELAY_MS));
        send_response(ST_BAD_PIN, NULL, 0);
        return;
    }
    failed_attempts = 0;
 
    switch (r->op) {
    case OP_LIST:  handle_list();     break;
    case OP_READ:  handle_read(r);    break;
    case OP_WRITE: handle_write(r);   break;
    default:       send_response(ST_BAD_REQUEST, NULL, 0); break;
    }
}

static void usb_init(void) {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
 
    cfg.rx_buffer_size = 512;
    usb_serial_jtag_driver_install(&cfg);
}
 
void app_main(void) {
    usb_init();
    storage_init();
 
    while (1) {
        wait_for_sync();
 
        if (recv_request(&req)) {
            dispatch(&req);
        } else {
            drain_rx();
            send_response(ST_BAD_REQUEST, NULL, 0);
        }
    }
}
