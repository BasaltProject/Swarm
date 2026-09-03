#include <basalt/swarm/Socket.hpp>

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
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace basalt::swarm {
namespace {

#if defined(_WIN32)
/// Winsock has to be started, once, before anything opens a socket.
struct WinsockStartup {
    WinsockStartup() {
        WSADATA data{};
        static_cast<void>(WSAStartup(MAKEWORD(2, 2), &data));
    }
    ~WinsockStartup() { WSACleanup(); }
};

void ensureStarted() { static const WinsockStartup kStarted; }

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

} // namespace

TcpClient::TcpClient(TcpClient&& other) noexcept : handle_(std::exchange(other.handle_, -1)) {}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

TcpClient::~TcpClient() { close(); }

std::expected<TcpClient, SocketError> TcpClient::connect(const std::string_view host, const std::uint16_t port) {
    ensureStarted();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string name{host};
    const std::string service = std::to_string(port);
    if (::getaddrinfo(name.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
        return std::unexpected(SocketError{.message = "cannot resolve " + name});
    }
    std::int64_t handle = -1;
    for (const addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next) {
        const auto opened = static_cast<std::int64_t>(
            ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol));
        if (opened < 0) {
            continue;
        }
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
    // A bot's packets are small and frequent, and waiting to fill a segment turns every keep-alive
    // into a fortieth of a second of nothing.
    const int one = 1;
    static_cast<void>(::setsockopt(static_cast<int>(handle), IPPROTO_TCP, TCP_NODELAY,
                                   reinterpret_cast<const char*>(&one), sizeof(one)));
    return TcpClient{handle};
}

bool TcpClient::waitReadable(const std::int32_t millis) const {
    if (handle_ < 0) {
        return false;
    }
#if defined(_WIN32)
    WSAPOLLFD waiting{};
    waiting.fd = static_cast<SOCKET>(handle_);
    waiting.events = POLLRDNORM;
    return WSAPoll(&waiting, 1, millis) > 0;
#else
    pollfd waiting{};
    waiting.fd = static_cast<int>(handle_);
    waiting.events = POLLIN;
    return ::poll(&waiting, 1, millis) > 0 && (waiting.revents & POLLIN) != 0;
#endif
}

std::expected<std::size_t, SocketError> TcpClient::read(const std::span<std::uint8_t> into) const {
    if (handle_ < 0) {
        return std::unexpected(SocketError{.message = "the socket is closed"});
    }
    const std::int64_t got = ::recv(static_cast<int>(handle_), reinterpret_cast<char*>(into.data()),
                                    static_cast<std::size_t>(into.size()), 0);
    if (got < 0) {
        return std::unexpected(SocketError{.message = lastError()});
    }
    return static_cast<std::size_t>(got);
}

std::expected<void, SocketError> TcpClient::writeAll(const std::span<const std::uint8_t> bytes) const {
    if (handle_ < 0) {
        return std::unexpected(SocketError{.message = "the socket is closed"});
    }
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const std::int64_t wrote = ::send(static_cast<int>(handle_),
                                          reinterpret_cast<const char*>(bytes.data() + sent),
                                          static_cast<std::size_t>(bytes.size() - sent), 0);
        if (wrote <= 0) {
            return std::unexpected(SocketError{.message = lastError()});
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return {};
}

void TcpClient::close() noexcept {
    if (handle_ >= 0) {
        closeHandle(handle_);
        handle_ = -1;
    }
}

} // namespace basalt::swarm
