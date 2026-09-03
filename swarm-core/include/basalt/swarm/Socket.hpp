#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace basalt::swarm {

struct SocketError {
    std::string message;
};

/// A blocking TCP client, and nothing more than one.
///
/// Swarm speaks to one server over one socket per bot and never listens, so this is the whole of
/// what it needs from the network. It is its own rather than Wire's on purpose: a bot that shares
/// its protocol code with the server it tests agrees with that server's bugs.
class TcpClient {
public:
    TcpClient() = default;
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;
    ~TcpClient();

    [[nodiscard]] static std::expected<TcpClient, SocketError> connect(std::string_view host, std::uint16_t port);

    /// Whether anything arrived within `millis`. False on a timeout, which is not a failure: it is
    /// the bot's cue to run a tick of its own.
    [[nodiscard]] bool waitReadable(std::int32_t millis) const;

    /// Reads whatever is there, up to the size of `into`. Zero means the server hung up.
    [[nodiscard]] std::expected<std::size_t, SocketError> read(std::span<std::uint8_t> into) const;

    /// Writes all of it, or answers why it could not.
    [[nodiscard]] std::expected<void, SocketError> writeAll(std::span<const std::uint8_t> bytes) const;

    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return handle_ >= 0; }

private:
    explicit TcpClient(std::int64_t handle) : handle_(handle) {}

    std::int64_t handle_ = -1;
};

} // namespace basalt::swarm
