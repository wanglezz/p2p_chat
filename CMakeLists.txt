# ---------------------------------------------------------------
# (Root) /CMakeLists.txt
# ---------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(LAB1 VERSION 1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

add_library(cereal INTERFACE)
target_include_directories(cereal INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/cereal/include"
)

# 添加 ImGui 库
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_sdl2.cpp
    external/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
#    告诉 ImGui 库去哪里找头文件
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)
#    ImGui 库需要 SDL2
target_link_libraries(imgui PUBLIC SDL2::SDL2)


# 4. 编译 'src' 子目录 (不变)
add_subdirectory(src)

# 5. 定义你的可执行程序 (不变)
add_executable(lab1_app
    main.cpp
)

# 6. 链接！(关键)
#    lab1_app 现在需要链接 peer_lib, imgui, 和 SDL2
target_link_libraries(lab1_app PRIVATE
    peer_lib
    imgui       # <-- 新增
    SDL2::SDL2  # <-- 新增
)