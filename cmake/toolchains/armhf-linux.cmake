# Cross toolchain for 32-bit ARM control boards:
#   - BeagleBone (TI AM335x, Cortex-A8)
#   - Xilinx Zynq-7000 (Cortex-A9)
#   - Amlogic A113D running a 32-bit userland
# Usage: cmake -B build-armhf -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armhf-linux.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_PREFIX arm-linux-gnueabihf- CACHE STRING "Cross compiler prefix")
set(CMAKE_C_COMPILER   ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

# Optional sysroot extracted from the target image.
if(DEFINED ENV{MINERD_SYSROOT})
    set(CMAKE_SYSROOT $ENV{MINERD_SYSROOT})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static libstdc++ avoids depending on the (old) C++ runtime of stock images.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
