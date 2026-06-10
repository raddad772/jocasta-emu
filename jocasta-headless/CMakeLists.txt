set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(jocasta-headless src/main.cpp)

target_include_directories(jocasta-headless PRIVATE
        ${CMAKE_SOURCE_DIR}/jocasta-lib/src
        ${CMAKE_SOURCE_DIR}/jocasta-gui/vendor   # stb_image_write.h
)

target_link_libraries(jocasta-headless PRIVATE jocasta-lib)

if(NOT MSVC)
    target_link_libraries(jocasta-headless PRIVATE m)
endif()
