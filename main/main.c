#include <stdbool.h>
#include "IOT_ErrorManager.h"
#include "IOT_Log.h"
#include "IOT_Platform.h"
#include "IOT_Core.h"
#include "IOT_CoreEventHandler.h"
#include "IOT_CoreReporter.h"
#include "IOT_AppManager.h"
#include "dao/IOT_DaoDeviceMgmt.h"
#include "ble/IOT_BleMgmt.h"
#include "network/IOT_NetworkMgmt.h"

#include "cloud_event_handler.h"

#define STAGING_ENVIRONMENT 1
#define PRODUCT_ENVIRONMENT !STAGING_ENVIRONMENT

static const char *TAG = "MAIN";

void app_main(void)
{
#if 1
    iot_err_t err = IOT_OK;

    // Step 1: Initialize platform (NVS, WiFi, BLE, MQTT, etc.)
    err = IOT_InitPlatform();
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_InitPlatform failed: %s", iot_err_to_name(err));
        return;
    }

    // Step 2: Initialize core with your model ID
#if STAGING_ENVIRONMENT
    static const char *MODEL_ID = "000001000C040019";
#endif

#if PRODUCT_ENVIRONMENT
    static const char *MODEL_ID = "000001000C040001";
#endif
    err = IOT_CoreInit(MODEL_ID);
    if (err != IOT_OK)
    {
        IOT_LOGE(TAG, "IOT_CoreInit failed: %s", iot_err_to_name(err));
        return;
    }
    // Step 3: Register event callbacks
    err = devRegisterEvent();
    if (err != IOT_OK)
    {
        IOT_LOGE("MAIN", "Failed to register event callback");
    }

    IOT_LOGI(TAG, "============= Version %s =============\n", CONFIG_APP_PROJECT_VER);

#endif
    if(IOT_CoreIsProvisioned())
    {
        root_device_factory_reset();
    }

    return;
}
