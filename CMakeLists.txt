
cmake_minimum_required(VERSION 3.13.0)

set(target_name axmol-migrate)
project(axmol-migrate)

set(CMAKE_CXX_STANDARD 20)
add_executable(${target_name} main.cpp base/posix_io.cpp)

# download libclang prebuilt for clang.index
set(clang_dir ${CMAKE_CURRENT_LIST_DIR}/clang-c)
if(NOT IS_DIRECTORY ${clang_dir}/prebuilt)
    set(pkg_ver "15.0.7")
    set(pkg_name "llvmorg-${pkg_ver}")
    set(pkg_url "https://github.com/axmolengine/build1k/releases/download/llvmorg-${pkg_ver}/${pkg_name}.zip")
    if (NOT EXISTS ${clang_dir}/${pkg_name}.zip)
        message(AUTHOR_WARNING "Downloading ${pkg_url}")
        file(DOWNLOAD ${pkg_url} ${clang_dir}/${pkg_name}.zip)
    endif()
    if (NOT IS_DIRECTORY ${clang_dir}/${pkg_name})
        file(ARCHIVE_EXTRACT INPUT ${clang_dir}/${pkg_name}.zip DESTINATION ${clang_dir}/)
    endif()
    file(RENAME ${clang_dir}/${pkg_name}/llvm/prebuilt ${clang_dir}/prebuilt)
    file(REMOVE_RECURSE ${clang_dir}/${package_name})
endif()

# add custom command copy libclang lib to exe dir
if(WIN32)
    set(platform_name "windows/x64")
    set(SO_SUFFIX ".dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platform_name "linux")
    set(SO_SUFFIX ".so")
elseif(APPLE)
    set(platform_name "mac")
    set(SO_SUFFIX ".dylib")
endif()
message(STATUS "platform_name=${platform_name}, SO_SUFFIX=${SO_SUFFIX}")
add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_LIST_DIR}/clang-c/prebuilt/${platform_name}/libclang${SO_SUFFIX}
            $<TARGET_FILE_DIR:${target_name}>
        )

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
