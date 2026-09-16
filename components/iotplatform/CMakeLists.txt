file(GLOB_RECURSE SRCS "${SDK_PATH}/implplatform/esp32/*.c"
                "${SDK_PATH}/iotPlatform/src/*.c")

# Remove ethernet sources when Ethernet feature is disabled
if(NOT CONFIG_IOT_ETHERNET_ENABLED)
    list(FILTER SRCS EXCLUDE REGEX ".*network/ethernet.*\\.c$")
endif()

set(REQUIRES esp_wifi esp_event nvs_flash bt mqtt esp_netif app_update esp_https_ota esp_http_client driver mdns iotcommon)
if(CONFIG_IOT_ETHERNET_ENABLED)
    list(APPEND REQUIRES esp_eth)
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "${SDK_PATH}/iotPlatform/include" "${SDK_PATH}/implplatform/esp32/dao"
                       REQUIRES ${REQUIRES})

# ==========================================================================
# REQUIRED — DO NOT REMOVE. BLE provisioning depends on these link flags.
# --------------------------------------------------------------------------
# The reference ESP32 BLE driver registers itself with iotcore via a
# __attribute__((constructor)) (IOT_BleMgmt{Nimble,Bluedroid}Register). In a
# static library a constructor only runs if its object file is pulled into the
# link, so these -u flags create synthetic undefined references that force the
# linker to extract that object and put the constructor in .init_array.
#
# If you remove these flags the build STILL SUCCEEDS, but the constructor never
# runs, no BLE driver is registered, and every IOT_Ble*() call returns
# IOT_ERR_NOT_SUPPORTED — i.e. BLE provisioning silently does nothing on device.
#
# Only the symbol for the selected BT stack exists; the other -u is harmless
# (an unresolvable -u wish is ignored by the linker). If you supply your OWN BLE
# driver, add a matching "-u <YourRegisterFn>" here.
# ==========================================================================
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-u IOT_BleMgmtNimbleRegister"
    "-u IOT_BleMgmtBluedroidRegister")