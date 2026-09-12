# Mbed TLS 3.6.2, Apache-2.0. A Bedrock login is a key exchange and an encrypted stream, so a bot
# that speaks it needs P-384, AES and SHA-256, and none of that is worth writing by hand.
#
# Vendored here rather than borrowed from Wire on purpose: Swarm depends on no other Basalt
# repository, and this is a C library from outside the project, not the code under test.

file(GLOB SWARM_MBEDTLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/library/*.c)

add_library(swarm_mbedtls STATIC ${SWARM_MBEDTLS_SOURCES})
add_library(basalt::mbedtls ALIAS swarm_mbedtls)

target_include_directories(swarm_mbedtls SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(swarm_mbedtls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/library)
set_target_properties(swarm_mbedtls PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(swarm_mbedtls PRIVATE -w)
