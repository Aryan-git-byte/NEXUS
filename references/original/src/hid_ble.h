#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void hid_ble_init(const char *name);
bool hid_ble_connected(void);
void hid_ble_key(uint8_t mod, uint8_t key);   // (0,0) = release
void hid_ble_consumer(uint16_t usage);        // 0 = release
#ifdef __cplusplus
}
#endif