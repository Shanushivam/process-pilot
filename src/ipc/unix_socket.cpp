#include "unix_socket.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr size_t kMaxMessage = 64 * 1024;

bool makeAddress(const std::string& path, sockaddr_un& address) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size());
    return true;
}

bool writeAll(int fd, const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

// Reads until EOF, a read error/timeout, or kMaxMessage bytes.
std::string readAll(int fd) {
    std::string data;
    char buffer[4096];
    while (data.size() < kMaxMessage) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(n));
    }
    return data;
}

// Server side: like readAll, but the whole request must arrive within
// timeoutMs, so a client trickling bytes can't hold the daemon up.
std::string readRequest(int fd, int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);

    std::string data;
    char buffer[4096];
    while (data.size() < kMaxMessage) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now()).count();
        if (remaining <= 0) {
            return "";
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, static_cast<int>(remaining));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            return "";  // timed out: drop the incomplete request
        }

        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;  // EOF: request complete
        }
        data.append(buffer, static_cast<size_t>(n));

        // A newline also ends a request (handy for tools like `nc`).
        auto newline = data.find('\n');
        if (newline != std::string::npos) {
            data.resize(newline);
            break;
        }
    }
    return data;
}

bool peerIsSameUser(int fd) {
#if defined(__linux__)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        return false;
    }
    return credentials.uid == geteuid();
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (getpeereid(fd, &uid, &gid) != 0) {
        return false;
    }
    return uid == geteuid();
#endif
}

void setReceiveTimeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

}  // namespace

std::string defaultSocketPath() {
    if (const char* explicitPath = std::getenv("PROCESSPILOT_SOCKET")) {
        if (*explicitPath != '\0') {
            return explicitPath;
        }
    }
    if (const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR")) {
        if (*runtimeDir != '\0') {
            return std::string(runtimeDir) + "/processpilot.sock";
        }
    }
    return "/tmp/processpilot-" + std::to_string(geteuid()) + ".sock";
}

UnixSocketServer::UnixSocketServer()
    : serverFd(-1), clientFd(-1) {}

UnixSocketServer::~UnixSocketServer() {
    stop();
}

bool UnixSocketServer::start(const std::string& path) {
    sockaddr_un address{};
    if (!makeAddress(path, address)) {
        error = "socket path is empty or too long: " + path;
        return false;
    }

    // Don't steal the socket from a daemon that is still running.
    UnixSocketClient probe;
    if (probe.connectTo(path)) {
        error = "another ProcessPilot daemon is already listening on " + path;
        return false;
    }

    serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    fcntl(serverFd, F_SETFD, FD_CLOEXEC);
    fcntl(serverFd, F_SETFL, fcntl(serverFd, F_GETFL) | O_NONBLOCK);

    unlink(path.c_str());

    // Create the socket file with owner-only permissions from the start.
    mode_t oldMask = umask(077);
    int bound = bind(serverFd, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address));
    umask(oldMask);

    if (bound < 0) {
        error = std::string("bind: ") + std::strerror(errno);
        ::close(serverFd);
        serverFd = -1;
        return false;
    }
    socketPath = path;
    chmod(socketPath.c_str(), 0600);

    if (listen(serverFd, 16) < 0) {
        error = std::string("listen: ") + std::strerror(errno);
        stop();
        return false;
    }

    return true;
}

bool UnixSocketServer::receive(int timeoutMs, std::string& request) {
    if (serverFd < 0) {
        return false;
    }

    pollfd pfd{};
    pfd.fd = serverFd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeoutMs) <= 0) {
        return false;
    }

    clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0) {
        return false;
    }

    // Accepted sockets inherit O_NONBLOCK on some platforms; reads are
    // bounded by poll() in readRequest instead.
    fcntl(clientFd, F_SETFL, fcntl(clientFd, F_GETFL) & ~O_NONBLOCK);
    fcntl(clientFd, F_SETFD, FD_CLOEXEC);

    if (!peerIsSameUser(clientFd)) {
        send("ERROR permission denied");
        return false;
    }

    request = readRequest(clientFd, 2000);
    while (!request.empty() &&
           (request.back() == '\n' || request.back() == '\r')) {
        request.pop_back();
    }

    if (request.empty()) {
        ::close(clientFd);
        clientFd = -1;
        return false;
    }

    return true;
}

bool UnixSocketServer::send(const std::string& message) {
    if (clientFd < 0) {
        return false;
    }

    bool ok = writeAll(clientFd, message + "\n");
    ::close(clientFd);
    clientFd = -1;

    return ok;
}

void UnixSocketServer::stop() {
    if (clientFd >= 0) {
        ::close(clientFd);
        clientFd = -1;
    }

    // Unlink while still bound: once closed, a new daemon may bind the same
    // path, and unlinking after that would delete its socket.
    if (!socketPath.empty()) {
        unlink(socketPath.c_str());
        socketPath.clear();
    }

    if (serverFd >= 0) {
        ::close(serverFd);
        serverFd = -1;
    }
}

const std::string& UnixSocketServer::lastError() const {
    return error;
}

UnixSocketClient::~UnixSocketClient() {
    closeConnection();
}

bool UnixSocketClient::connectTo(const std::string& path) {
    sockaddr_un address{};
    if (!makeAddress(path, address)) {
        return false;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) < 0) {
        closeConnection();
        return false;
    }

    return true;
}

std::string UnixSocketClient::request(const std::string& message) {
    if (fd < 0) {
        return "";
    }

    if (!writeAll(fd, message)) {
        closeConnection();
        return "";
    }

    // Signal end-of-request so the server's read loop finishes.
    shutdown(fd, SHUT_WR);

    // Stopping a service can take up to its StopTimeoutSec.
    setReceiveTimeout(fd, 120);
    std::string response = readAll(fd);
    closeConnection();

    while (!response.empty() && response.back() == '\n') {
        response.pop_back();
    }
    return response;
}

void UnixSocketClient::closeConnection() {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}
