
cmake_minimum_required(VERSION 3.13.0)

set(target_name axmol-migrate)
project(axmol-migrate)

set(CMAKE_CXX_STANDARD 20)
add_executable(${target_name} main.cpp base/posix_io.cpp)
if (WIN32)
    file(GLOB_RECURSE LIBCLANG_DLL_PATH "${CMAKE_CURRENT_LIST_DIR}/plugins/*.dll")
elseif(${CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBCLANG_DLL_PATH "${CMAKE_CURRENT_LIST_DIR}/plugins/libclang/linux/libclang.so")
elseif(APPLE)
    set(LIBCLANG_DLL_PATH "${CMAKE_CURRENT_LIST_DIR}/plugins/libclang/mac/libclang.dylib")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/config.h.in" "${CMAKE_BINARY_DIR}/config.h" @ONLY)
target_include_directories(${target_name} 
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}
)
set_target_properties(${target_name}
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
