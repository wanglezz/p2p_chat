# ---------------------------------------------------------------
# src/CMakeLists.txt
# ---------------------------------------------------------------

add_library(peer_lib 
    peer_message.cc
    tcp_peer.cc
)

target_include_directories(peer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(peer_lib PUBLIC
    cereal
)