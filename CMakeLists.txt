cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)

# Disable RISC-V small data section (.sdata) to avoid internal SRAM overflow.
# Only apply to C/C++ compilers, not assembler which doesn't support this flag.
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-msmall-data-limit=0>)

project(12_ESP32P4_TEST)
