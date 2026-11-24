cmake_minimum_required(VERSION 3.16)
project(siren_synth_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(siren_synth_host_tests
    test_siren_synth.c
    ../../src/siren_synth.c
)
target_include_directories(siren_synth_host_tests PRIVATE
    include
    ../../include
)

if(MSVC)
    target_compile_options(siren_synth_host_tests PRIVATE /W4)
else()
    target_compile_options(siren_synth_host_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(siren_synth_host_tests PRIVATE m)
endif()

enable_testing()
add_test(NAME siren_synth_host_tests COMMAND siren_synth_host_tests)
