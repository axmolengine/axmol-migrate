
cmake_minimum_required(VERSION 3.13.0)

set(target_name axmol-migrate)
project(axmol-migrate)

set(CMAKE_CXX_STANDARD 20)
add_executable(${target_name} main.cpp base/posix_io.cpp)
target_include_directories(${target_name} 
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}
)
set_target_properties(${target_name}
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
