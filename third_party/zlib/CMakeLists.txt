# zlib 1.3.1, zlib licence. Compiled from source rather than found on the host: the
# cross-compilation sysroots for Linux and Windows carry nothing.

add_library(basalt_zlib STATIC
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    infback.c
    inffast.c
    inflate.c
    inftrees.c
    trees.c
    uncompr.c
    zutil.c
)
add_library(basalt::zlib ALIAS basalt_zlib)

target_include_directories(basalt_zlib SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# NO_GZIP must stay undefined: zlib turns gzip support off on the mere presence of the name, and
# gzip is what saved structure files and rotated logs use.
target_compile_definitions(basalt_zlib PRIVATE HAVE_UNISTD_H)
set_target_properties(basalt_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(basalt_zlib PRIVATE -w)
