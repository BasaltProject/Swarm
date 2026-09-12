#pragma once

#include <basalt/swarm/UdpSocket.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt::swarm::bedrock {

/// The sixteen bytes that tell a RakNet datagram from anything else arriving at
/// a UDP port.
inline constexpr std::array<std::uint8_t, 16> kMagic{0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                                     0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

inline constexpr std::uint8_t kRakNetProtocol = 11;

/// What a server answers an unconnected ping with, taken apart.
struct Advertisement {
    std::string raw;
    std::string edition;
    std::string motd;
    int protocol = 0;
    std::string version;
    int online = 0;
    int max = 0;
    std::uint16_t port = 0;

    /// False when the line did not have the twelve fields a client reads, which
    /// is a server a real client would not list.
    [[nodiscard]] static std::optional<Advertisement> parse(std::string_view line);
};

/// The client half of RakNet: the handshake that opens a session and the reliability that keeps it.
/// Written against the protocol rather than shared with the server it tests: a bot that
/// acknowledges the way the server does cannot notice that either of them is wrong.
class RakClient {
public:
    RakClient(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

    /// Pings without connecting, which is what a server list does.
    [[nodiscard]] std::optional<Advertisement> ping(std::int32_t millis = 2000);

    /// Opens a session: the two rounds of the offline handshake, then the
    /// connection request.
    [[nodiscard]] bool connect(std::string& problem);

    /// Queues a game payload, splitting it when it does not fit one datagram.
    void sendGame(std::span<const std::uint8_t> payload);

    /// Everything that arrived, in order, with the acknowledgements owed sent
    /// back. A payload here is what came after the 0xfe.
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> poll(std::int32_t millis);

    void disconnect();

    [[nodiscard]] bool connected() const noexcept { return connected_; }

    [[nodiscard]] std::uint16_t mtu() const noexcept { return mtu_; }

private:
    void sendMessage(std::span<const std::uint8_t> body, std::uint8_t reliability = 3);
    void sendDatagram(std::span<const std::uint8_t> frame);
    void acknowledge(std::uint32_t sequence);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> receiveRaw(std::int32_t millis);
    void handleFrames(std::span<const std::uint8_t> datagram, std::vector<std::vector<std::uint8_t>>& out);

    std::string host_;
    std::uint16_t port_ = 19132;
    UdpClient socket_;
    bool connected_ = false;
    std::uint16_t mtu_ = 1400;
    std::uint64_t guid_ = 0;

    std::uint32_t nextSequence_ = 0;
    std::uint32_t nextReliable_ = 0;
    std::uint32_t nextOrder_ = 0;
    std::uint16_t nextSplitId_ = 0;

    /// Parts of a message still arriving, by the id the sender gave the split.
    std::unordered_map<std::uint16_t, std::map<std::uint32_t, std::vector<std::uint8_t>>> splits_;
    std::unordered_map<std::uint16_t, std::uint32_t> splitCounts_;
};

} // namespace basalt::swarm::bedrock
