# The same warning set the rest of Basalt uses. Copied rather than shared: Wire owns that file, and
# Swarm deliberately does not depend on Wire.

add_library(swarm_warnings INTERFACE)
add_library(swarm::warnings ALIAS swarm_warnings)

target_compile_options(swarm_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
)

if(SWARM_WERROR)
    target_compile_options(swarm_warnings INTERFACE -Werror)
endif()
