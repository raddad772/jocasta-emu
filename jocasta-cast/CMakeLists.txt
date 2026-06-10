cmake_minimum_required(VERSION 3.21)

project(jocasta-cast C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT JOCASTA_DREAMCAST AND NOT CMAKE_SYSTEM_NAME STREQUAL "Dreamcast")
    message(FATAL_ERROR "jocasta-cast must be built with the Dreamcast toolchain/profile")
endif()

add_executable(jocasta-cast
        src/dc_app.cpp
        src/dc_app.h
)

target_include_directories(jocasta-cast
        PRIVATE
        ${CMAKE_SOURCE_DIR}/jocasta-lib/src
        ${KOS_BASE}/include
        ${KOS_BASE}/kernel/arch/dreamcast/include
        ${KOS_PORTS}/include
        ~/dev/libs/include
)

target_link_directories(jocasta-cast
        PRIVATE
        ${KOS_BASE}/lib/dreamcast
        ${KOS_PORTS}/lib
        ~/dev/libs/bin
)

target_link_libraries(jocasta-cast
        PRIVATE
        jocasta-lib
        m
        kallisti
        c
        gcc)

install(TARGETS jocasta-cast RUNTIME DESTINATION bin)
target_compile_definitions(jocasta-cast PRIVATE FOR_DREAMCAST)

add_custom_target(build_cdi
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/build-dreamcast-cdi.sh
        DEPENDS jocasta-cast
)

add_custom_target(run_flycast
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/run-flycast.sh
        DEPENDS jocasta-cast
        DEPENDS build_cdi
)
