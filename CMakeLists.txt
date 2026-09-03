cmake_minimum_required(VERSION 3.28)

# The version the binary is named after. The build number is the CI's run number; a build made
# anywhere else has none and says so.
set(SWARM_VERSION_BASE "26.2")
if(DEFINED ENV{GITHUB_RUN_NUMBER})
    set(SWARM_VERSION "${SWARM_VERSION_BASE}.$ENV{GITHUB_RUN_NUMBER}")
else()
    set(SWARM_VERSION "${SWARM_VERSION_BASE}.0-dev")
endif()

project(basalt-swarm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# No RTTI: nothing here asks an object what it is, and it is dead weight in the binary.
add_compile_options(-fno-rtti)

# Swarm does not consume Wire, and that is deliberate: a bot that speaks the protocol through the
# same code as the server it is testing cannot catch a bug in that code, because both sides make
# the same mistake and agree.
include(cmake/SwarmWarnings.cmake)
include(cmake/SwarmSanitizers.cmake)
enable_testing()

add_subdirectory(third_party)
add_subdirectory(swarm-core)
