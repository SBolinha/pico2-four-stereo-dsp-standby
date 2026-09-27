#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool w5500_onkyo_init(void);
void w5500_onkyo_task(void);
bool w5500_onkyo_connected(void);

bool w5500_onkyo_standby(void);
bool w5500_onkyo_query_power(void);
uint32_t w5500_onkyo_power_generation(void);
bool w5500_onkyo_power_is_on(void);

#ifdef __cplusplus
}
#endif
