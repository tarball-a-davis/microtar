set(CMAKE_INCLUDE_CURRENT_DIR ON)
file(GLOB mtar_src *.c)

add_library(
    microtar
    ${mtar_src}
)
target_include_directories(
    microtar
    PUBLIC .
)
