#include <basalt/swarm/UdpSocket.hpp>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace basalt::swarm {
namespace {

#if defined(_WIN32)
void ensureStarted() {
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    static_cast<void>(started);
}

void closeHandle(const std::int64_t handle) { closesocket(static_cast<SOCKET>(handle)); }
#else
void ensureStarted() {}

void closeHandle(const std::int64_t handle) { ::close(static_cast<int>(handle)); }
#endif

[[nodiscard]] std::string lastError() {
#if defined(_WIN32)
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

[[nodiscard]] bool waitReadable(const std::int64_t handle, const std::int32_t millis) {
#if defined(_WIN32)
    WSAPOLLFD waiting{};
    waiting.fd = static_cast<SOCKET>(handle);
    waiting.events = POLLRDNORM;
    return WSAPoll(&waiting, 1, millis) > 0;
#else
    pollfd waiting{};
    waiting.fd = static_cast<int>(handle);
    waiting.events = POLLIN;
    return ::poll(&waiting, 1, millis) > 0 && (waiting.revents & POLLIN) != 0;
#endif
}

} // namespace

UdpClient::UdpClient(UdpClient&& other) noexcept : handle_(std::exchange(other.handle_, -1)) {}

UdpClient& UdpClient::operator=(UdpClient&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

UdpClient::~UdpClient() { close(); }

std::expected<UdpClient, SocketError> UdpClient::to(const std::string_view host, const std::uint16_t port) {
    ensureStarted();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    const std::string name{host};
    const std::string service = std::to_string(port);
    if (::getaddrinfo(name.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
        return std::unexpected(SocketError{.message = "cannot resolve " + name});
    }

    std::int64_t handle = -1;
    for (const addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next) {
        const auto opened =
            static_cast<std::int64_t>(::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol));
        if (opened < 0) {
            continue;
        }
        // Connected, though there is no connection: it fixes who this socket talks
        // to, so a send needs no address and a datagram from anybody else never
        // arrives.
        if (::connect(static_cast<int>(opened), candidate->ai_addr, static_cast<socklen_t>(candidate->ai_addrlen)) ==
            0) {
            handle = opened;
            break;
        }
        closeHandle(opened);
    }
    const std::string why = lastError();
    ::freeaddrinfo(found);
    if (handle < 0) {
        return std::unexpected(SocketError{.message = why});
    }
    return UdpClient{handle};
}

std::expected<void, SocketError> UdpClient::send(const std::span<const std::uint8_t> bytes) const {
    if (handle_ < 0) {
        return std::unexpected(SocketError{.message = "socket closed"});
    }
    const auto sent = ::send(static_cast<int>(handle_), reinterpret_cast<const char*>(bytes.data()),
                             static_cast<int>(bytes.size()), 0);
    if (sent < 0) {
        return std::unexpected(SocketError{.message = lastError()});
    }
    return {};
}

std::expected<std::optional<std::size_t>, SocketError> UdpClient::receive(const std::span<std::uint8_t> into,
                                                                          const std::int32_t millis) const {
    if (handle_ < 0) {
        return std::unexpected(SocketError{.message = "socket closed"});
    }
    if (!waitReadable(handle_, millis)) {
        return std::optional<std::size_t>{};
    }
    const auto received =
        ::recv(static_cast<int>(handle_), reinterpret_cast<char*>(into.data()), static_cast<int>(into.size()), 0);
    if (received < 0) {
        return std::unexpected(SocketError{.message = lastError()});
    }
    return std::optional<std::size_t>{static_cast<std::size_t>(received)};
}

void UdpClient::close() noexcept {
    if (handle_ < 0) {
        return;
    }
    closeHandle(handle_);
    handle_ = -1;
}

} // namespace basalt::swarm
