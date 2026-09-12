#include <basalt/swarm/Zlib.hpp>

#include <zlib.h>

#include <algorithm>

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

std::expected<std::vector<std::uint8_t>, ZlibError> compressRaw(const std::span<const std::uint8_t> data,
                                                                const int level) {
    if (data.empty()) {
        return std::vector<std::uint8_t>{};
    }
    z_stream stream{};
    const int started = deflateInit2(&stream, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (started != Z_OK) {
        return std::unexpected(ZlibError{.message = "raw deflate would not start: " + std::to_string(started)});
    }

    std::vector<std::uint8_t> out(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());

    const int result = deflate(&stream, Z_FINISH);
    const std::size_t written = out.size() - stream.avail_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        return std::unexpected(ZlibError{.message = "raw deflate failed with code " + std::to_string(result)});
    }
    out.resize(written);
    return out;
}

std::expected<std::vector<std::uint8_t>, ZlibError> decompressRaw(const std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return std::vector<std::uint8_t>{};
    }
    z_stream stream{};
    const int started = inflateInit2(&stream, -15);
    if (started != Z_OK) {
        return std::unexpected(ZlibError{.message = "raw inflate would not start: " + std::to_string(started)});
    }

    std::vector<std::uint8_t> out(std::max<std::size_t>(data.size() * 4, 1024));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    std::size_t written = 0;

    while (true) {
        stream.next_out = out.data() + written;
        stream.avail_out = static_cast<uInt>(out.size() - written);
        const int result = inflate(&stream, Z_NO_FLUSH);
        written = out.size() - stream.avail_out;

        if (result == Z_STREAM_END) {
            inflateEnd(&stream);
            out.resize(written);
            return out;
        }
        if (result != Z_OK && result != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return std::unexpected(ZlibError{.message = "raw inflate failed with code " + std::to_string(result)});
        }
        if (written < out.size()) {
            inflateEnd(&stream);
            return std::unexpected(ZlibError{.message = "a batch ended in the middle of its own stream"});
        }
        if (out.size() >= kMaxUncompressed) {
            inflateEnd(&stream);
            return std::unexpected(ZlibError{.message = "a batch inflates past what one is allowed to be"});
        }
        out.resize(std::min(out.size() * 2, kMaxUncompressed));
    }
}

} // namespace basalt::swarm::zlib
