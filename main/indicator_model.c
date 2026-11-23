#include "wifi.h"
#include "indicator_enabler.h"

int indicator_model_init(void) {
#ifdef STORAGE_NVS_H
	indicator_nvs_init();
#else
	#error "Please inplement storage model"
#endif

#ifdef BTN_H
	indicator_btn_init();
#endif

#ifdef INDICATOR_DISPLAY_H
	indicator_display_init(); // lcd bl on
#endif

#ifdef WIFI_H
	indicator_wifi_model_init();
	indicator_tec_init();

#endif

}
