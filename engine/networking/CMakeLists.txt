# engine/networking/CMakeLists.txt
#
# Networking subsystem — ENet-based client-server transport.
# No GPU involvement; pure CPU + sockets.
#
# Tiers: LEGACY (primary), STANDARD, MODERN. RETRO not supported.

find_package(unofficial-enet CONFIG REQUIRED)

add_library(ffe_networking STATIC
    client.cpp
    lag_compensation.cpp
    lobby.cpp
    network_system.cpp
    packet.cpp
    prediction.cpp
    replication.cpp
    server.cpp
    transport.cpp
)

target_include_directories(ffe_networking
    PUBLIC ${CMAKE_SOURCE_DIR}/engine  # #include "networking/packet.h"
)

target_link_libraries(ffe_networking
    PUBLIC
        ffe_core
        ffe_renderer
    PRIVATE
        unofficial::enet::enet
)
