# ---------------------------------------------------------------
# (Root) /CMakeLists.txt
# ---------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(LAB1 VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cereal INTERFACE)

target_include_directories(cereal INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/cereal/include"
)

add_subdirectory(src)

add_executable(lab1_app
    main.cpp
)


target_link_libraries(lab1_app PRIVATE
    peer_lib
)
