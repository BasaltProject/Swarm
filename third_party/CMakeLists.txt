# Vendored dependencies, compiled from source. Nothing here is looked up on the host system: the
# cross-compilation sysroots carry none of it, and a stress tool that cannot be built for the
# platform it is pointed at is not much of one.

add_subdirectory(doctest)
add_subdirectory(zlib)
