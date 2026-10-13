#include "minerd/pool/tcp_line_socket.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace minerd::pool {

TcpLineSocket::~TcpLineSocket() { close(); }

Status TcpLineSocket::connect(const std::string& host, unsigned port, std::chrono::milliseconds timeout) {
    close();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    if (int rc = ::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res); rc != 0)
        return Status::error("resolve " + host + ": " + ::gai_strerror(rc));

    Status last = Status::error("no addresses for " + host);
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) continue;
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            rc = ::poll(&p, 1, int(timeout.count()));
            int err = 0;
            socklen_t len = sizeof err;
            if (rc == 1 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) rc = 0;
            else { rc = -1; errno = err ? err : ETIMEDOUT; }
        }
        if (rc == 0) {
            ::fcntl(fd, F_SETFL, flags);  // back to blocking; reads use poll()
            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
            fd_ = fd;
            ::freeaddrinfo(res);
            return Status::ok();
        }
        last = Status::error("connect " + host + ":" + port_s + ": " + std::strerror(errno));
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return last;
}

void TcpLineSocket::close() {
    std::lock_guard lk(tx_mx_);
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
    }
    fd_ = -1;
    rx_.clear();
}

bool TcpLineSocket::is_open() const {
    std::lock_guard lk(tx_mx_);
    return fd_ >= 0;
}

Status TcpLineSocket::send_line(const std::string& line) {
    std::lock_guard lk(tx_mx_);
    if (fd_ < 0) return Status::error("not connected");
    std::string data = line + "\n";
    std::size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(fd_, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return Status::error(std::string("send: ") + std::strerror(errno));
        }
        off += std::size_t(n);
    }
    return Status::ok();
}

std::optional<std::string> TcpLineSocket::read_line(std::chrono::milliseconds timeout, bool& closed) {
    closed = false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (auto nl = rx_.find('\n'); nl != std::string::npos) {
            std::string line = rx_.substr(0, nl);
            rx_.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (rx_.size() > (1u << 20)) { closed = true; return std::nullopt; }  // runaway line
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return std::nullopt;
        int fd;
        {
            std::lock_guard lk(tx_mx_);
            fd = fd_;
        }
        if (fd < 0) { closed = true; return std::nullopt; }
        pollfd p{fd, POLLIN, 0};
        const int rc = ::poll(&p, 1, int(left.count()));
        if (rc == 0) return std::nullopt;
        if (rc < 0) { if (errno == EINTR) continue; closed = true; return std::nullopt; }
        char buf[4096];
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) { closed = true; return std::nullopt; }
        rx_.append(buf, std::size_t(n));
    }
}

} // namespace minerd::pool
