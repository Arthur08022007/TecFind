#ifndef TEC_H
#define TEC_H

#include <stddef.h>
#include <stdint.h>

int indicator_tec_init(void);
int indicator_tec_view_init(void);
size_t indicator_tec_stop_count(void);
const char *indicator_tec_stop_name(uint16_t index);
unsigned int indicator_tec_stop_distance_m(uint16_t index);
uint16_t indicator_tec_selected_stop(void);
void indicator_tec_select_stop(uint16_t index);

#endif
