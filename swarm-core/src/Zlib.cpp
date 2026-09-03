#include <basalt/swarm/Zlib.hpp>

#include <zlib.h>

namespace basalt::swarm::zlib {

std::expected<std::vector<std::uint8_t>, ZlibError> compress(const std::span<const std::uint8_t> data,
                                                             const int level) {
    if (data.empty()) {
        return std::vector<std::uint8_t>{};
    }
    uLongf bound = compressBound(static_cast<uLong>(data.size()));
    std::vector<std::uint8_t> out(bound);
    const int result = compress2(out.data(), &bound, data.data(), static_cast<uLong>(data.size()), level);
    if (result != Z_OK) {
        return std::unexpected(ZlibError{.message = "deflate failed with code " + std::to_string(result)});
    }
    out.resize(bound);
    return out;
}

std::expected<std::vector<std::uint8_t>, ZlibError> decompress(const std::span<const std::uint8_t> data,
                                                               const std::size_t uncompressedSize) {
    if (uncompressedSize == 0) {
        return std::vector<std::uint8_t>{};
    }
    if (uncompressedSize > kMaxUncompressed) {
        return std::unexpected(
            ZlibError{.message = "a packet claims to inflate to " + std::to_string(uncompressedSize) + " bytes"});
    }
    if (data.empty()) {
        return std::unexpected(ZlibError{.message = "nothing to inflate"});
    }
    std::vector<std::uint8_t> out(uncompressedSize);
    auto written = static_cast<uLongf>(uncompressedSize);
    const int result = uncompress(out.data(), &written, data.data(), static_cast<uLong>(data.size()));
    if (result != Z_OK) {
        return std::unexpected(ZlibError{.message = "inflate failed with code " + std::to_string(result)});
    }
    out.resize(written);
    return out;
}

} // namespace basalt::swarm::zlib
