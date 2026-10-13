cmake_minimum_required(VERSION 3.21)
project(minerd VERSION 0.1.0 DESCRIPTION "Open ASIC miner control daemon" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MINERD_BUILD_TESTS     "Build unit tests"                               ON)
option(MINERD_WITH_LIBBITCOIN "Use libbitcoin-system for sha256d (cross-check)" OFF)
option(MINERD_WERROR          "Treat warnings as errors"                        OFF)

find_package(Threads REQUIRED)

add_library(minerd_core STATIC
    # util
    src/util/log.cpp
    src/util/bytes.cpp
    src/util/config.cpp
    src/util/json.cpp
    # crypto
    src/crypto/sha256.cpp
    # work
    src/work/work_generator.cpp
    src/work/share.cpp
    # asic (chip abstraction)
    src/asic/chip_registry.cpp
    src/asic/bm13xx/protocol.cpp
    src/asic/bm13xx/bm13xx_chain.cpp
    src/asic/bm13xx/bm1362.cpp
    src/asic/bm13xx/bm1366.cpp
    src/asic/sim/sim_chain.cpp
    # hal
    src/hal/sim/sim_hal.cpp
    src/hal/linux/serial_linux.cpp
    src/hal/linux/i2c_linux.cpp
    src/hal/linux/gpio_sysfs.cpp
    src/hal/linux/hwmon.cpp
    # power
    src/power/power.cpp
    # board
    src/board/profile.cpp
    src/board/hash_board.cpp
    src/board/platform.cpp
    # pool
    src/pool/stratum_v1.cpp
    src/pool/local_test_pool.cpp
    src/pool/tcp_line_socket.cpp
    # control
    src/control/thermal.cpp
    src/control/safety.cpp
    src/control/autotuner.cpp
    src/control/miner.cpp
    # api
    src/api/cgminer_api.cpp
)

target_include_directories(minerd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(minerd_core PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minerd_core PUBLIC -Wall -Wextra -Wpedantic -Wshadow)
    if(MINERD_WERROR)
        target_compile_options(minerd_core PUBLIC -Werror)
    endif()
endif()

if(MINERD_WITH_LIBBITCOIN)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBBITCOIN_SYSTEM REQUIRED IMPORTED_TARGET libbitcoin-system)
    target_sources(minerd_core PRIVATE src/crypto/libbitcoin_backend.cpp)
    target_link_libraries(minerd_core PUBLIC PkgConfig::LIBBITCOIN_SYSTEM)
    target_compile_definitions(minerd_core PUBLIC MINERD_HAVE_LIBBITCOIN=1)
endif()

add_executable(minerd app/main.cpp)
target_link_libraries(minerd PRIVATE minerd_core)
install(TARGETS minerd RUNTIME DESTINATION bin)
install(DIRECTORY profiles/ DESTINATION share/minerd/profiles)

if(MINERD_BUILD_TESTS)
    enable_testing()
    foreach(t test_sha256 test_work test_bm13xx_protocol test_json_config test_sim_mining test_safety)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE minerd_core)
        target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()
endif()
