cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "src/siren_synth.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_common
)
