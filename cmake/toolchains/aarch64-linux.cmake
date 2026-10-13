# Cross toolchain for 64-bit ARM control boards (e.g. Amlogic A113D with arm64
# userland, newer CVITEK/other boards, Raspberry Pi based replacement boards).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CROSS_PREFIX aarch64-linux-gnu- CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

if(DEFINED ENV{MINERD_SYSROOT})
    set(CMAKE_SYSROOT $ENV{MINERD_SYSROOT})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
