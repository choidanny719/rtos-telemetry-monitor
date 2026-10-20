cmake_minimum_required(VERSION 3.24)
project(rtos_telemetry_monitor VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(ENABLE_SANITIZERS "Instrument unit tests with address and undefined sanitizers" OFF)

include(CTest)
include(FetchContent)
add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
if(ENABLE_SANITIZERS)
    target_compile_options(project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie)
    target_link_options(project_options INTERFACE -fsanitize=address,undefined -no-pie)
endif()

add_library(telemetry_core src/tmp102.c)
target_include_directories(telemetry_core PUBLIC include)
target_link_libraries(telemetry_core PUBLIC project_options)
add_library(sensor_sim sim/sensor.c)
target_include_directories(sensor_sim PUBLIC sim)
target_link_libraries(sensor_sim PUBLIC telemetry_core)

if(BUILD_TESTING)
    FetchContent_Declare(unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG cbcd08fa7de711053a3deec6339ee89cad5d2697)
    FetchContent_MakeAvailable(unity)
    add_executable(test_sensor tests/test_sensor.c)
    target_link_libraries(test_sensor PRIVATE sensor_sim unity)
    add_test(NAME sensor COMMAND test_sensor)
endif()
