cmake_minimum_required(VERSION 3.31.0)
project(jocasta-emu C CXX)

option(JOCASTA_DREAMCAST "Build Dreamcast targets" OFF)
option(JOCASTA_ASAN "Enable AddressSanitizer" OFF)
if(APPLE OR WIN32)
    set(JOCASTA_BUILD_GUI_DEFAULT ON)
else()
    set(JOCASTA_BUILD_GUI_DEFAULT OFF)
endif()
option(JOCASTA_BUILD_GUI "Build Jocasta GUI" ${JOCASTA_BUILD_GUI_DEFAULT})

if(JOCASTA_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -O1")
endif()

# Define DEBUG in Debug build
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

if(JOCASTA_DREAMCAST OR CMAKE_SYSTEM_NAME STREQUAL "Dreamcast")
    add_compile_definitions(FOR_DREAMCAST)

    add_compile_options(
            -Os
            -ffunction-sections
            -fdata-sections
    )

    add_link_options(
            -Wl,--gc-sections
    )
endif()

add_subdirectory(jocasta-lib)

if(JOCASTA_DREAMCAST OR CMAKE_SYSTEM_NAME STREQUAL "Dreamcast")
    add_subdirectory(jocasta-cast)
else()
    if(JOCASTA_BUILD_GUI)
        add_subdirectory(jocasta-gui)
    endif()

    if(APPLE)
        add_subdirectory(jocasta-tests)
    endif()
endif()
