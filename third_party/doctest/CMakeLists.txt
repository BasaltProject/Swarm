add_library(basalt_doctest INTERFACE)
add_library(basalt::doctest ALIAS basalt_doctest)
target_include_directories(basalt_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
