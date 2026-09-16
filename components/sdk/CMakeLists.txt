# This component is "your SDK" in the example's structure. It is seeded with the
# reference DAOs and attribute defines from reference/ so the example builds and
# runs out of the box — they are starter code to copy and adapt, not a library to
# depend on. Replace these sources with your own as you write them; the component
# name stays, only what fills it changes.
#
# INCLUDE_DIRS points at reference/ itself, so headers resolve as
# "dao/IOT_DaoDeviceMgmt.h" — the same include paths as before this repo renamed
# the directory from sdk/ to reference/.
file(GLOB_RECURSE SRCS "${SDK_PATH}/reference/dao/*.c"
                       "${SDK_PATH}/reference/defines/*.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "${SDK_PATH}/reference"
                       REQUIRES iotplatform iotcommon iotcore)
