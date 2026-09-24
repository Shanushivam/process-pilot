#ifndef UNIX_SOCKET_HPP
#define UNIX_SOCKET_HPP

#include <string>

// $PROCESSPILOT_SOCKET, else $XDG_RUNTIME_DIR/processpilot.sock,
// else /tmp/processpilot-<uid>.sock.
std::string defaultSocketPath();

class UnixSocketServer {
public:
    UnixSocketServer();
    ~UnixSocketServer();

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    // Refuses to start if another daemon is already listening on `path`.
    // The socket is created with mode 0600.
    bool start(const std::string& path);

    // Waits up to timeoutMs for a client. Returns true and fills `request`
    // if a request from a client running as the same user was received.
    bool receive(int timeoutMs, std::string& request);

    bool send(const std::string& message);
    void stop();

    const std::string& lastError() const;

private:
    int serverFd;
    int clientFd;
    std::string socketPath;
    std::string error;
};

class UnixSocketClient {
public:
    ~UnixSocketClient();

    bool connectTo(const std::string& path);
    std::string request(const std::string& message);
    void closeConnection();

private:
    int fd = -1;
};

#endif
