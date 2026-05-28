set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

add_definitions(-DJSM_SDLGPU)

set(JOCASTA_LOCAL_LIBS_DIR "$ENV{HOME}/dev/libs" CACHE PATH "Local dependency directory")
set(JOCASTA_SDL3_LIBRARY "SDL3" CACHE STRING "SDL3 library name or path")

include_directories("${JOCASTA_LOCAL_LIBS_DIR}/include")
link_directories("${JOCASTA_LOCAL_LIBS_DIR}/bin")
link_directories("${JOCASTA_LOCAL_LIBS_DIR}/lib")

#option(DAWN_BUILD_SAMPLES "Enables building Dawn's samples" OFF)
#option(TINT_BUILD_CMD_TOOLS "Build the Tint command line tools" OFF)
#option(TINT_BUILD_DOCS "Build documentation" OFF)
#option(TINT_BUILD_TESTS "Build tests" OFF)

add_executable(jocasta-gui
        src/my_texture_sdlgpu.cpp
        src/my_texture_sdlgpu.h)

# Add application src
target_sources(
        jocasta-gui 
        PRIVATE 
        src/full_sys.cpp
        src/full_sys.h
        src/application.h
        src/application.cpp
        src/app_settings.cpp
        src/app_settings.h
        src/keybinding.cpp
        src/keybinding.h
        src/keymap_translate.cpp
        src/keymap_translate.h
        src/audiowrap.cpp
        src/audiowrap.h
        src/virtual_input.cpp
        src/virtual_input.h
        src/build.h
        src/my_texture.h
        src/my_texture_sdlr3.cpp
        src/my_texture_sdlr3.h
        src/my_texture_wgpu.h
        src/my_texture_ogl3.h
        src/my_texture_ogl3.cpp
        src/my_texture_wgpu.cpp
        vendor/miniz/miniz.c
        vendor/miniz/miniz_tdef.c
        vendor/miniz/miniz_tinfl.c
        vendor/miniz/miniz_zip.c
        vendor/tinyfiledialogs/tinyfiledialogs.c
)

# Add a subset of ImGui source files directly to the executable to fit our use case
target_sources(
        jocasta-gui
        PRIVATE

        # Among the different backends available, we are interested in connecting
        # the GUI to GLFW andWebGPU:
        #vendor/myimgui/backends/imgui_impl_wgpu.h
        #vendor/myimgui/backends/imgui_impl_wgpu.cpp
        #vendor/myimgui/backends/imgui_impl_glfw.h
        #vendor/myimgui/backends/imgui_impl_glfw.cpp
        vendor/myimgui/backends/imgui_impl_sdl3.h
        vendor/myimgui/backends/imgui_impl_sdl3.cpp
        vendor/myimgui/backends/imgui_impl_sdlgpu3.h
        vendor/myimgui/backends/imgui_impl_sdlgpu3.cpp


        # Bonus to add some C++ specific features (the core ImGUi is a C library)
        vendor/myimgui/misc/cpp/imgui_stdlib.h
        vendor/myimgui/misc/cpp/imgui_stdlib.cpp

        # The core ImGui files
        vendor/myimgui/imconfig.h
        vendor/myimgui/imgui.h
        vendor/myimgui/imgui.cpp
        vendor/myimgui/imgui_draw.cpp
        vendor/myimgui/imgui_internal.h
        vendor/myimgui/imgui_tables.cpp
        vendor/myimgui/imgui_widgets.cpp
        vendor/myimgui/imstb_rectpack.h
        vendor/myimgui/imstb_textedit.h
        vendor/myimgui/imstb_truetype.h
)

#FIND_PACKAGE(SDL2 REQUIRED)
#include_directories( ${SDL2_INCLUDE_DIR} )

target_include_directories(
    jocasta-gui
    PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/jocasta-lib/src
    vendor/myimgui
    vendor/miniz
    vendor/tinyfiledialogs
    vendor/mINI/src
    vendor/nanosha256
)

#add_subdirectory(vendor/WebGPU-distribution)
#add_subdirectory(vendor/glfw3webgpu)
#find_package(Dawn)

# [...]

target_link_libraries(jocasta-gui jocasta-lib ${JOCASTA_SDL3_LIBRARY})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(jocasta-gui PROPERTIES
            BUILD_RPATH "\$ORIGIN"
            INSTALL_RPATH "\$ORIGIN")
endif()
#target_link_libraries(jocasta-gui jocasta-lib glfw3 webgpu_dawn dawn_glfw "-framework CoreFoundation" "-framework AppKit" "-framework Cocoa" "-framework IOKit" "-framework OpenGL" "-framework Quartz")
#webgpu glfw3webgpu webgpu_cpp  webgpu_glfw
#target_link_libraries(jocasta-gui PRIVATE )

install(TARGETS jocasta-gui RUNTIME DESTINATION bin)
