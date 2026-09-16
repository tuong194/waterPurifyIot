set(ROOT_DIR ${CMAKE_SOURCE_DIR}) # chỉ gán biến, không hiểu j về path, không xử lý được các lệnh: ./, ../
get_filename_component(ROOT_DIR ${ROOT_DIR} REALPATH) # xử lý đường dẫn, ROOT_DIR = CMAKE_SOURCE_DIR = .../FW_SDK

# GLOB: ko quét thư mục con GLOB_RECURSE: quét cả thư mục con
file(GLOB_RECURSE SRCS "IOT_DaoSmartMgmt.c" "${ROOT_DIR}/main/*.c")

# Collect all include directories
file(GLOB_RECURSE ALL_HEADERS "${ROOT_DIR}/main/*.h") 
# Extract directory paths from header list
set(INCLUDE_DIRS "")
foreach(_header ${ALL_HEADERS})
    get_filename_component(_dir ${_header} DIRECTORY)    
    list(APPEND INCLUDE_DIRS ${_dir}) 
endforeach()
message(STATUS "Root directory: ${ROOT_DIR}")
 
# Remove duplicates
list(REMOVE_DUPLICATES INCLUDE_DIRS)
   
idf_component_register(SRCS "${SRCS}" 
                       INCLUDE_DIRS "${INCLUDE_DIRS}"  
                       REQUIRES iotcore iotplatform sdk esp_wifi esp_event nvs_flash esp_eth bt mqtt esp_netif json)