#include "interconnect/tcp_transport.hpp"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

namespace vr {
namespace interconnect {

namespace {

using SocketHandle = SOCKET;

vr::core::ErrorCode MapSocketError() {
    return vr::core::ErrorCode::kQueueSendFailed;
}

bool ParsePort(const std::string& value, std::uint16_t* port) {
    if (port == nullptr) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }

    *port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseHostPort(const TransportEndpointConfig& config, std::string* host,
                   std::uint16_t* port) {
    if (host == nullptr || port == nullptr) {
        return false;
    }

    const auto host_it = config.params.find("host");
    const auto port_it = config.params.find("port");
    if (host_it == config.params.end() || port_it == config.params.end()) {
        return false;
    }

    *host = host_it->second;
    return ParsePort(port_it->second, port);
}

void EnsureWsaInit() {
    static std::once_flag wsa_once;
    std::call_once(wsa_once, []() {
        WSADATA wsa_data{};
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
    });
}

int SetNonBlocking(const SocketHandle fd, const bool enable) {
    u_long mode = enable ? 1UL : 0UL;
    return ioctlsocket(fd, FIONBIO, &mode);
}

vr::core::ErrorCode WaitForSocket(const SocketHandle fd, const bool write_ready,
                                  const std::int64_t timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval tv;
    timeval* tv_ptr = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
        tv_ptr = &tv;
    }

    const int res = select(0, write_ready ? nullptr : &fds,
                           write_ready ? &fds : nullptr, nullptr, tv_ptr);
    if (res == 0) {
        return vr::core::ErrorCode::kTimeout;
    }
    if (res < 0) {
        return MapSocketError();
    }
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode SendAll(const SocketHandle fd, const void* data, const std::size_t len,
                            const std::int64_t timeout_ms) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(data);
    std::size_t total = 0;
    while (total < len) {
        const vr::core::ErrorCode wait_ec = WaitForSocket(fd, true, timeout_ms);
        if (wait_ec != vr::core::ErrorCode::kOk) {
            return wait_ec;
        }

        const int sent = ::send(fd, reinterpret_cast<const char*>(ptr + total),
                                static_cast<int>(len - total), 0);
        if (sent == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            return MapSocketError();
        }
        total += static_cast<std::size_t>(sent);
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode RecvAll(const SocketHandle fd, void* data, const std::size_t len,
                            const std::int64_t timeout_ms) {
    std::uint8_t* ptr = static_cast<std::uint8_t*>(data);
    std::size_t total = 0;
    while (total < len) {
        const vr::core::ErrorCode wait_ec = WaitForSocket(fd, false, timeout_ms);
        if (wait_ec != vr::core::ErrorCode::kOk) {
            return wait_ec;
        }

        const int received = ::recv(fd, reinterpret_cast<char*>(ptr + total),
                                    static_cast<int>(len - total), 0);
        if (received == 0) {
            return vr::core::ErrorCode::kQueueReceiveFailed;
        }
        if (received == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            return MapSocketError();
        }
        total += static_cast<std::size_t>(received);
    }

    return vr::core::ErrorCode::kOk;
}

}  // namespace

const char* TcpTransport::Name() const noexcept {
    return "tcp";
}

TransportCapabilities TcpTransport::Caps() const noexcept {
    TransportCapabilities caps;
    caps.supports_priority = false;
    caps.supports_discard_oldest = false;
    caps.supports_unlink = false;
    return caps;
}

vr::core::ErrorCode TcpTransport::Create(const TransportEndpointConfig& config) noexcept {
    EnsureWsaInit();

    std::string host;
    std::uint16_t port = 0;
    if (!ParseHostPort(config, &host, &port)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    bool enable_nodelay = true;
    bool enable_keepalive = true;
    auto nodelay_it = config.params.find("tcp_nodelay");
    if (nodelay_it != config.params.end()) {
        enable_nodelay = (nodelay_it->second == "1" || nodelay_it->second == "true" ||
                          nodelay_it->second == "on");
    }
    auto keepalive_it = config.params.find("tcp_keepalive");
    if (keepalive_it != config.params.end()) {
        enable_keepalive = (keepalive_it->second == "1" || keepalive_it->second == "true" ||
                            keepalive_it->second == "on");
    }

    return Connect(host, port, 2000, enable_nodelay, enable_keepalive);
}

vr::core::ErrorCode TcpTransport::SendWithTimeout(const std::string& message,
                                                  std::uint32_t,
                                                  std::int64_t timeout_ms) noexcept {
    if (socket_fd_ == INVALID_SOCKET) {
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    const std::uint32_t len = static_cast<std::uint32_t>(message.size());
    const std::uint32_t net_len = htonl(len);
    const vr::core::ErrorCode len_ec = SendAll(socket_fd_, &net_len, sizeof(net_len), timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }

    if (len == 0) {
        return vr::core::ErrorCode::kOk;
    }

    return SendAll(socket_fd_, message.data(), message.size(), timeout_ms);
}

vr::core::ErrorCode TcpTransport::ReceiveWithTimeout(std::string* const message,
                                                     std::uint32_t* const priority,
                                                     const std::int64_t timeout_ms) noexcept {
    if (message == nullptr || priority == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }
    if (socket_fd_ == INVALID_SOCKET) {
        return vr::core::ErrorCode::kQueueReceiveFailed;
    }

    std::uint32_t net_len = 0;
    vr::core::ErrorCode len_ec = RecvAll(socket_fd_, &net_len, sizeof(net_len), timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }
    const std::uint32_t len = ntohl(net_len);

    message->assign(len, '\0');
    if (len == 0) {
        *priority = 0U;
        return vr::core::ErrorCode::kOk;
    }

    len_ec = RecvAll(socket_fd_, message->data(), len, timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }

    *priority = 0U;
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode TcpTransport::DiscardOldest() noexcept {
    return vr::core::ErrorCode::kOk;
}

void TcpTransport::Close() noexcept {
    CloseSocket();
}

void TcpTransport::Unlink() noexcept {
}

vr::core::ErrorCode TcpTransport::Connect(const std::string& host, const std::uint16_t port,
                                          const std::int64_t timeout_ms,
                                          const bool enable_nodelay,
                                          const bool enable_keepalive) noexcept {
    CloseSocket();

    const SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) {
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    if (SetNonBlocking(fd, true) != 0) {
        closesocket(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        closesocket(fd);
        return vr::core::ErrorCode::kInvalidParam;
    }

    const int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            closesocket(fd);
            return vr::core::ErrorCode::kQueueCreateFailed;
        }
    }

    const vr::core::ErrorCode wait_ec = WaitForSocket(fd, true, timeout_ms);
    if (wait_ec != vr::core::ErrorCode::kOk) {
        closesocket(fd);
        return wait_ec;
    }

    int socket_error = 0;
    int len = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &len) != 0 ||
        socket_error != 0) {
        closesocket(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    const char nodelay_flag = enable_nodelay ? 1 : 0;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay_flag, sizeof(nodelay_flag));

    const char keepalive_flag = enable_keepalive ? 1 : 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive_flag, sizeof(keepalive_flag));

    if (SetNonBlocking(fd, false) != 0) {
        closesocket(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    socket_fd_ = fd;
    return vr::core::ErrorCode::kOk;
}

void TcpTransport::CloseSocket() noexcept {
    if (socket_fd_ != INVALID_SOCKET) {
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
    }
}

}  // namespace interconnect
}  // namespace vr

#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace vr {
namespace interconnect {

namespace {

vr::core::ErrorCode MapSocketError() {
    return vr::core::ErrorCode::kQueueSendFailed;
}

bool ParsePort(const std::string& value, std::uint16_t* port) {
    if (port == nullptr) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }

    *port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool ParseHostPort(const TransportEndpointConfig& config, std::string* host,
                   std::uint16_t* port) {
    if (host == nullptr || port == nullptr) {
        return false;
    }

    const auto host_it = config.params.find("host");
    const auto port_it = config.params.find("port");
    if (host_it == config.params.end() || port_it == config.params.end()) {
        return false;
    }

    *host = host_it->second;
    return ParsePort(port_it->second, port);
}

int SetNonBlocking(const int fd, const bool enable) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    if (enable) {
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

vr::core::ErrorCode WaitForSocket(const int fd, const bool write_ready,
                                  const std::int64_t timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval tv;
    timeval* tv_ptr = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        tv_ptr = &tv;
    }

    const int res = select(fd + 1, write_ready ? nullptr : &fds,
                           write_ready ? &fds : nullptr, nullptr, tv_ptr);
    if (res == 0) {
        return vr::core::ErrorCode::kTimeout;
    }
    if (res < 0) {
        return MapSocketError();
    }
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode SendAll(const int fd, const void* data, const std::size_t len,
                            const std::int64_t timeout_ms) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(data);
    std::size_t total = 0;
    while (total < len) {
        const vr::core::ErrorCode wait_ec = WaitForSocket(fd, true, timeout_ms);
        if (wait_ec != vr::core::ErrorCode::kOk) {
            return wait_ec;
        }

        const ssize_t sent = ::send(fd, ptr + total, len - total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return MapSocketError();
        }
        total += static_cast<std::size_t>(sent);
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode RecvAll(const int fd, void* data, const std::size_t len,
                            const std::int64_t timeout_ms) {
    std::uint8_t* ptr = static_cast<std::uint8_t*>(data);
    std::size_t total = 0;
    while (total < len) {
        const vr::core::ErrorCode wait_ec = WaitForSocket(fd, false, timeout_ms);
        if (wait_ec != vr::core::ErrorCode::kOk) {
            return wait_ec;
        }

        const ssize_t received = ::recv(fd, ptr + total, len - total, 0);
        if (received == 0) {
            return vr::core::ErrorCode::kQueueReceiveFailed;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return MapSocketError();
        }
        total += static_cast<std::size_t>(received);
    }

    return vr::core::ErrorCode::kOk;
}

}  // namespace

const char* TcpTransport::Name() const noexcept {
    return "tcp";
}

TransportCapabilities TcpTransport::Caps() const noexcept {
    TransportCapabilities caps;
    caps.supports_priority = false;
    caps.supports_discard_oldest = false;
    caps.supports_unlink = false;
    return caps;
}

vr::core::ErrorCode TcpTransport::Create(const TransportEndpointConfig& config) noexcept {
    std::string host;
    std::uint16_t port = 0;
    if (!ParseHostPort(config, &host, &port)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    bool enable_nodelay = true;
    bool enable_keepalive = true;
    auto nodelay_it = config.params.find("tcp_nodelay");
    if (nodelay_it != config.params.end()) {
        enable_nodelay = (nodelay_it->second == "1" || nodelay_it->second == "true" ||
                          nodelay_it->second == "on");
    }
    auto keepalive_it = config.params.find("tcp_keepalive");
    if (keepalive_it != config.params.end()) {
        enable_keepalive = (keepalive_it->second == "1" || keepalive_it->second == "true" ||
                            keepalive_it->second == "on");
    }

    return Connect(host, port, 2000, enable_nodelay, enable_keepalive);
}

vr::core::ErrorCode TcpTransport::SendWithTimeout(const std::string& message,
                                                  std::uint32_t,
                                                  std::int64_t timeout_ms) noexcept {
    if (socket_fd_ < 0) {
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    const std::uint32_t len = static_cast<std::uint32_t>(message.size());
    const std::uint32_t net_len = htonl(len);
    const vr::core::ErrorCode len_ec = SendAll(socket_fd_, &net_len, sizeof(net_len), timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }

    if (len == 0) {
        return vr::core::ErrorCode::kOk;
    }

    return SendAll(socket_fd_, message.data(), message.size(), timeout_ms);
}

vr::core::ErrorCode TcpTransport::ReceiveWithTimeout(std::string* const message,
                                                     std::uint32_t* const priority,
                                                     const std::int64_t timeout_ms) noexcept {
    if (message == nullptr || priority == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }
    if (socket_fd_ < 0) {
        return vr::core::ErrorCode::kQueueReceiveFailed;
    }

    std::uint32_t net_len = 0;
    vr::core::ErrorCode len_ec = RecvAll(socket_fd_, &net_len, sizeof(net_len), timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }
    const std::uint32_t len = ntohl(net_len);

    message->assign(len, '\0');
    if (len == 0) {
        *priority = 0U;
        return vr::core::ErrorCode::kOk;
    }

    len_ec = RecvAll(socket_fd_, message->data(), len, timeout_ms);
    if (len_ec != vr::core::ErrorCode::kOk) {
        return len_ec;
    }

    *priority = 0U;
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode TcpTransport::DiscardOldest() noexcept {
    return vr::core::ErrorCode::kOk;
}

void TcpTransport::Close() noexcept {
    CloseSocket();
}

void TcpTransport::Unlink() noexcept {
}

vr::core::ErrorCode TcpTransport::Connect(const std::string& host, const std::uint16_t port,
                                          const std::int64_t timeout_ms,
                                          const bool enable_nodelay,
                                          const bool enable_keepalive) noexcept {
    CloseSocket();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    if (SetNonBlocking(fd, true) != 0) {
        ::close(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(fd);
        return vr::core::ErrorCode::kInvalidParam;
    }

    const int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    const vr::core::ErrorCode wait_ec = WaitForSocket(fd, true, timeout_ms);
    if (wait_ec != vr::core::ErrorCode::kOk) {
        ::close(fd);
        return wait_ec;
    }

    int socket_error = 0;
    socklen_t len = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &len) != 0 || socket_error != 0) {
        ::close(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    const int nodelay_flag = enable_nodelay ? 1 : 0;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay_flag, sizeof(nodelay_flag));

    const int keepalive_flag = enable_keepalive ? 1 : 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive_flag, sizeof(keepalive_flag));

    if (SetNonBlocking(fd, false) != 0) {
        ::close(fd);
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    socket_fd_ = fd;
    return vr::core::ErrorCode::kOk;
}

void TcpTransport::CloseSocket() noexcept {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

}  // namespace interconnect
}  // namespace vr

#endif  // _WIN32
