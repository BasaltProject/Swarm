add_library(swarm-core STATIC
    src/Bot.cpp
    src/BotReconfiguration.cpp
    src/BotResourcePacks.cpp
    src/Bytes.cpp
    src/Checks.cpp
    src/Dialogs.cpp
    src/KnownVersions.cpp
    src/KnownVersionsData.cpp
    src/Nbt.cpp
    src/Protocols.cpp
    src/Socket.cpp
    src/UdpSocket.cpp
    src/bedrock/BedrockBot.cpp
    src/bedrock/BedrockCrypto.cpp
    src/bedrock/RakClient.cpp
    src/Spread.cpp
    src/Zlib.cpp
)
add_library(swarm::core ALIAS swarm-core)

target_include_directories(swarm-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(swarm-core PUBLIC swarm::warnings swarm::sanitizers PRIVATE basalt::zlib basalt::mbedtls)

# The binary is named the way it is downloaded, the same as the server's.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_swarm_platform "macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_swarm_platform "Windows")
else()
    set(_swarm_platform "${CMAKE_SYSTEM_NAME}")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(_swarm_arch "arm64")
else()
    set(_swarm_arch "x64")
endif()

add_executable(basalt-swarm src/Main.cpp)
target_link_libraries(basalt-swarm PRIVATE swarm::core)
set_target_properties(basalt-swarm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "BasaltSwarm-${SWARM_VERSION}-${_swarm_platform}-${_swarm_arch}"
)

add_executable(swarm-checks-test test/ChecksTest.cpp)
target_link_libraries(swarm-checks-test PRIVATE swarm::core basalt::doctest)
add_test(NAME swarm-checks-test COMMAND swarm-checks-test)

add_executable(swarm-spread-test test/SpreadTest.cpp)
target_link_libraries(swarm-spread-test PRIVATE swarm::core basalt::doctest)
add_test(NAME swarm-spread-test COMMAND swarm-spread-test)

add_executable(swarm-bedrock-test test/BedrockTest.cpp)
target_link_libraries(swarm-bedrock-test PRIVATE swarm::core basalt::doctest)
add_test(NAME swarm-bedrock-test COMMAND swarm-bedrock-test)

add_executable(swarm-protocols-test test/ProtocolsTest.cpp)
target_link_libraries(swarm-protocols-test PRIVATE swarm::core basalt::doctest)
add_test(NAME swarm-protocols-test COMMAND swarm-protocols-test)
