file(GLOB_RECURSE SRCS "${SDK_PATH}/iotcommon/src/*.c")

set(INCLUDE_DIRS "${SDK_PATH}/iotcommon/include")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS ${INCLUDE_DIRS})
