#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "interconnect/tcp_transport.hpp"
#include "interconnect/transport.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

bool InitSockets() {
    WSADATA wsa_data{};
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
}

void CloseSocket(SocketHandle fd) {
    if (fd != kInvalidSocket) {
        closesocket(fd);
    }
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

bool InitSockets() {
    return true;
}

void CloseSocket(SocketHandle fd) {
    if (fd >= 0) {
        ::close(fd);
    }
}
#endif

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool ReadAll(SocketHandle fd, void* buffer, std::size_t size) {
    std::uint8_t* ptr = static_cast<std::uint8_t*>(buffer);
    std::size_t received = 0U;
    while (received < size) {
#ifdef _WIN32
        const int rc = ::recv(fd, reinterpret_cast<char*>(ptr + received),
                              static_cast<int>(size - received), 0);
#else
        const ssize_t rc = ::recv(fd, ptr + received, size - received, 0);
#endif
        if (rc <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(rc);
    }
    return true;
}

bool WriteAll(SocketHandle fd, const void* buffer, std::size_t size) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(buffer);
    std::size_t sent = 0U;
    while (sent < size) {
#ifdef _WIN32
        const int rc = ::send(fd, reinterpret_cast<const char*>(ptr + sent),
                              static_cast<int>(size - sent), 0);
#else
        const ssize_t rc = ::send(fd, ptr + sent, size - sent, 0);
#endif
        if (rc <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(rc);
    }
    return true;
}

bool RunLoopbackServer(std::uint16_t* out_port) {
    if (out_port == nullptr) {
        return false;
    }

    SocketHandle server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == kInvalidSocket) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket(server_fd);
        return false;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(server_fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        CloseSocket(server_fd);
        return false;
    }

    if (::listen(server_fd, 1) != 0) {
        CloseSocket(server_fd);
        return false;
    }

    *out_port = ntohs(addr.sin_port);

    SocketHandle client_fd = ::accept(server_fd, nullptr, nullptr);
    CloseSocket(server_fd);
    if (client_fd == kInvalidSocket) {
        return false;
    }

    std::uint32_t net_len = 0;
    if (!ReadAll(client_fd, &net_len, sizeof(net_len))) {
        CloseSocket(client_fd);
        return false;
    }

    const std::uint32_t len_payload = ntohl(net_len);
    std::string payload(len_payload, '\0');
    if (len_payload > 0 && !ReadAll(client_fd, payload.data(), len_payload)) {
        CloseSocket(client_fd);
        return false;
    }

    if (!WriteAll(client_fd, &net_len, sizeof(net_len))) {
        CloseSocket(client_fd);
        return false;
    }

    if (len_payload > 0 && !WriteAll(client_fd, payload.data(), len_payload)) {
        CloseSocket(client_fd);
        return false;
    }

    CloseSocket(client_fd);
    return true;
}

bool TestTcpLoopback() {
    if (!InitSockets()) {
        return false;
    }

    std::uint16_t port = 0U;
    std::thread server([&]() {
        (void)RunLoopbackServer(&port);
    });

    while (port == 0U) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    vr::interconnect::TcpTransport transport;
    vr::interconnect::TransportEndpointConfig config;
    config.name = "tcp_loopback";
    config.params["host"] = "127.0.0.1";
    config.params["port"] = std::to_string(port);
    config.params["tcp_nodelay"] = "on";
    config.params["tcp_keepalive"] = "on";

    const auto create_ec = transport.Create(config);
    if (!ExpectTrue(create_ec == vr::core::ErrorCode::kOk, "tcp create")) {
        server.join();
        return false;
    }

    const std::string payload = "loopback_ping";
    const auto send_ec = transport.SendWithTimeout(payload, 0U, 2000);
    if (!ExpectTrue(send_ec == vr::core::ErrorCode::kOk, "tcp send")) {
        transport.Close();
        server.join();
        return false;
    }

    std::string reply;
    std::uint32_t priority = 0U;
    const auto recv_ec = transport.ReceiveWithTimeout(&reply, &priority, 2000);
    transport.Close();
    server.join();

    return ExpectTrue(recv_ec == vr::core::ErrorCode::kOk, "tcp recv") &&
           ExpectTrue(reply == payload, "echo payload match");
}

}  // namespace

int main() {
    const bool ok = TestTcpLoopback();
    if (!ok) {
        std::cerr << "interconnect tcp loopback test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect tcp loopback test passed." << std::endl;
    return 0;
}
