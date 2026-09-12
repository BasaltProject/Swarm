#pragma once

#include <basalt/swarm/Socket.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace basalt::swarm {

/// A blocking UDP client, which is what RakNet needs: Bedrock has no stream to
/// read, only datagrams that arrive whole or not at all.
class UdpClient {
public:
    UdpClient() = default;
    UdpClient(const UdpClient&) = delete;
    UdpClient& operator=(const UdpClient&) = delete;
    UdpClient(UdpClient&& other) noexcept;
    UdpClient& operator=(UdpClient&& other) noexcept;
    ~UdpClient();

    /// Opens a socket already pointed at one server, so every send and receive is
    /// with it.
    [[nodiscard]] static std::expected<UdpClient, SocketError> to(std::string_view host, std::uint16_t port);

    [[nodiscard]] std::expected<void, SocketError> send(std::span<const std::uint8_t> bytes) const;

    /// One datagram, or nothing when the wait ran out. A timeout is not a
    /// failure: a bot that hears nothing for a moment is a bot with nothing to
    /// do.
    [[nodiscard]] std::expected<std::optional<std::size_t>, SocketError> receive(std::span<std::uint8_t> into,
                                                                                 std::int32_t millis) const;

    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return handle_ >= 0; }

private:
    explicit UdpClient(std::int64_t handle) : handle_(handle) {}

    std::int64_t handle_ = -1;
};

} // namespace basalt::swarm
