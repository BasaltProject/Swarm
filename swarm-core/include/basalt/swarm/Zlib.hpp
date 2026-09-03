#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace basalt::swarm::zlib {

struct ZlibError {
    std::string message;
};

/// Deflates, the way a compressed packet carries its payload.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, ZlibError> compress(std::span<const std::uint8_t> data,
                                                                          int level = 6);

/// Inflates into exactly `uncompressedSize` bytes, which the packet said up front.
///
/// The size is checked rather than trusted: it arrives from the network, and a wrong one is how a
/// bot ends up asking for a gigabyte because a server had a bad day.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, ZlibError> decompress(std::span<const std::uint8_t> data,
                                                                            std::size_t uncompressedSize);

/// The most a single packet may inflate to. Vanilla's own limit is 8 MiB.
inline constexpr std::size_t kMaxUncompressed = 8UL * 1024UL * 1024UL;

} // namespace basalt::swarm::zlib
