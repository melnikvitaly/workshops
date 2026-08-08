# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# "../../protocol" is the shared wire format (telemetry_packet.h), the one file
# this firmware and the STM32 master both compile. Keeping it outside either
# project is what makes a layout change impossible to apply to only one end.
idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "../../protocol")
