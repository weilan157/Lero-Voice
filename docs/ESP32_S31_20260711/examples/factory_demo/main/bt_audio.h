#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bt_audio_init(void);
bool bt_audio_is_connected(void);
const char *bt_audio_device_name(void);
void bt_audio_notify_connected(bool connected);

#ifdef __cplusplus
}
#endif
