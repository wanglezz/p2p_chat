# ---------------------------------------------------------------
# (Root) /CMakeLists.txt
# ---------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(LAB2_CHATROOM VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 寻找依赖 (SDL2, Threads, Cereal)
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

add_library(cereal INTERFACE)
target_include_directories(cereal INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/cereal/include"
)

# 2. 添加 ImGui 库
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_sdl2.cpp
    external/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# 3. 编译 'src' 子目录
#    (这将创建 "message_lib" 和 "tcppeer_lib" 两个目标)
add_subdirectory(src)

# 4. 定义 "ChatClient" (lab1_app)
add_executable(lab1_app
    main.cpp
)
# 链接 客户端网络库, ImGui, 和 SDL2
target_link_libraries(lab1_app PRIVATE
    tcppeer_lib
    imgui
    SDL2::SDL2
)

# 5. 定义 "ChatServer" (新)
add_executable(chat_server
    server_main.cpp
)
# 链接 消息库 和 线程库
target_link_libraries(chat_server PRIVATE
    message_lib
    Threads::Threads
)