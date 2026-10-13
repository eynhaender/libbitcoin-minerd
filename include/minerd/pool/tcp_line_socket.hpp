#pragma once
// Blocking, newline-delimited TCP client used by Stratum V1.

#include "minerd/util/status.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace minerd::pool {

class TcpLineSocket {
public:
    TcpLineSocket() = default;
    ~TcpLineSocket();
    TcpLineSocket(const TcpLineSocket&) = delete;
    TcpLineSocket& operator=(const TcpLineSocket&) = delete;

    Status connect(const std::string& host, unsigned port, std::chrono::milliseconds timeout);
    void close();
    bool is_open() const;

    /// Thread-safe; appends '\n'.
    Status send_line(const std::string& line);
    /// Returns a line without the trailing newline, nullopt on timeout.
    /// Sets `closed` when the peer closed the connection or an error occurred.
    std::optional<std::string> read_line(std::chrono::milliseconds timeout, bool& closed);

private:
    int fd_ = -1;
    std::string rx_;
    mutable std::mutex tx_mx_;
};

} // namespace minerd::pool
