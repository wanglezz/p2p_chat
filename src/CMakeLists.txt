# ---------------------------------------------------------------
# src/CMakeLists.txt (Lab 2 版本)
# ---------------------------------------------------------------

# 1. 创建 "message_lib" (通用消息库)
#    客户端和服务端都需要这个库
add_library(message_lib
    peer_message.cc
)
target_include_directories(message_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# 消息库依赖 Cereal
target_link_libraries(message_lib PUBLIC
    cereal
)


# 2. 创建 "tcppeer_lib" (Lab 1 客户端网络库)
#    只有客户端 (lab1_app) 需要这个库
add_library(tcppeer_lib
    tcp_peer.cc
)
target_include_directories(tcppeer_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# 客户端库依赖 消息库 和 线程库
target_link_libraries(tcppeer_lib PUBLIC
    message_lib
    Threads::Threads
)