idf_component_register(
    INCLUDE_DIRS "${SDK_PATH}/iotcore/include"
    REQUIRES iotcommon
)

add_prebuilt_library(iotcore "${SDK_PATH}/iotcore/lib/${IDF_TARGET}/libiotcore.a"
    REQUIRES iotplatform)
target_link_libraries(${COMPONENT_LIB} INTERFACE iotcore)
