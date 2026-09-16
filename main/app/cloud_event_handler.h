#ifndef _APP_DEVICE_H__
#define _APP_DEVICE_H__

#include "IOT_ErrorManager.h"
#include "stdint.h"

#define ATTR_ONOFF 1
#define ATTR_COLOR_HSV 31
#define SETTING_BUTTON_LED_COLOR_ON 61936
#define SETTING_BUTTON_LED_COLOR_OFF 61937
#define SETTING_POWER_ON_BEHAVIOR 61985

// ============================================================================
// Sync-on-off (subSmartType 71) binding table — in-memory demo store
// ----------------------------------------------------------------------------
// The cloud binds a sync-on-off group by sending a trigger with subSmartType
// 71 (delivered as SMART_EVENT_BIND_TRIGGER). Unlike an ordinary trigger, the
// bind's PRIMARY attribute value is not a match value — it is attribute
// APP_ATTR_SYNC_ONOFF_ELEMENTS, whose format is a list of element IDs (each a
// 2-byte big-endian value). See the attribute documentation for the format.
//
// We remember "this smid drives these local elements" so we can (a) apply an
// incoming peer broadcast to every one of them and (b) broadcast our own local
// changes. A real SDK persists this in its DAO; here a small fixed array is
// enough to show the mechanics.
//
// The synced attribute is always ON/OFF — the group mirrors on/off state across
// devices, so there is no per-bind attribute to remember.
// ============================================================================
// Sub-type and attribute IDs are defined by YOUR SDK, from the attribute
// documentation — core headers do not declare them.
#define APP_SYNC_ON_OFF_SUBTYPE      71
#define APP_ATTR_SYNC_ONOFF_ELEMENTS 315    // value format: list of 2-byte element IDs
#define APP_COND_GROUP_SUBTYPE 2048 // your SDK defines this, not core

typedef enum{
    TYPE_RGB = 0,
    TYPE_HSV = 1,
    TYPE_HSL = 2
}ColorType;

extern uint8_t g_cloud_is_connected;

iot_err_t devRegisterEvent(void);
void root_device_factory_reset(void);

#endif /*  */
