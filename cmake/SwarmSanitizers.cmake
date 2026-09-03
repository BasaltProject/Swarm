# Sanitizer selection. SWARM_SANITIZE is a comma-separated list: address, undefined, thread.

add_library(swarm_sanitizers INTERFACE)
add_library(swarm::sanitizers ALIAS swarm_sanitizers)

if(SWARM_SANITIZE)
    string(REPLACE "," ";" _swarm_sanitizers "${SWARM_SANITIZE}")
    if("thread" IN_LIST _swarm_sanitizers AND "address" IN_LIST _swarm_sanitizers)
        message(FATAL_ERROR "SWARM_SANITIZE: thread and address are mutually exclusive")
    endif()
    string(REPLACE ";" "," _swarm_sanitize_flag "${_swarm_sanitizers}")
    target_compile_options(swarm_sanitizers INTERFACE
        -fsanitize=${_swarm_sanitize_flag}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    target_link_options(swarm_sanitizers INTERFACE -fsanitize=${_swarm_sanitize_flag})
    message(STATUS "Swarm: sanitizers enabled (${_swarm_sanitize_flag})")
endif()
