#include "../src/ipc/unix_socket.hpp"
#include "test_util.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

namespace {

std::string dir;

void testRoundTrip() {
    std::string path = dir + "/rt.sock";
    UnixSocketServer server;
    CHECK(server.start(path));

    struct stat info {};
    CHECK(stat(path.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);

    std::string reply;
    std::thread client([&] {
        UnixSocketClient c;
        if (c.connectTo(path)) {
            reply = c.request("status web");
        }
    });

    std::string request;
    bool received = false;
    for (int i = 0; i < 50 && !received; ++i) {
        received = server.receive(100, request);
    }
    CHECK(received);
    CHECK(request == "status web");
    CHECK(server.send("line one\nline two"));
    client.join();
    CHECK(reply == "line one\nline two");

    // Nothing pending: receive times out instead of blocking.
    CHECK(!server.receive(50, request));
}

void testLargeMessage() {
    std::string path = dir + "/large.sock";
    UnixSocketServer server;
    CHECK(server.start(path));

    std::string big(20000, 'x');
    std::string reply;
    std::thread client([&] {
        UnixSocketClient c;
        if (c.connectTo(path)) {
            reply = c.request("start " + big);
        }
    });

    std::string request;
    bool received = false;
    for (int i = 0; i < 50 && !received; ++i) {
        received = server.receive(100, request);
    }
    CHECK(request.size() == 6 + big.size());
    server.send(big + big);
    client.join();
    CHECK(reply.size() == 2 * big.size());
}

// Regression: a second daemon used to silently unlink and take over the
// socket of a running one.
void testRefusesToStealSocket() {
    std::string path = dir + "/busy.sock";
    UnixSocketServer first;
    CHECK(first.start(path));

    UnixSocketServer second;
    CHECK(!second.start(path));
    CHECK(second.lastError().find("already listening") != std::string::npos);
}

void testReplacesStaleSocket() {
    std::string path = dir + "/stale.sock";

    // Leave a socket file behind with nobody listening on it.
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    CHECK(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    close(fd);

    UnixSocketServer server;
    CHECK(server.start(path));
}

void testStopRemovesSocket() {
    std::string path = dir + "/gone.sock";
    {
        UnixSocketServer server;
        CHECK(server.start(path));
    }
    CHECK(access(path.c_str(), F_OK) != 0);
}

// Regression: a client sending one byte at a time could stall the daemon
// indefinitely (the read timeout restarted on every byte).
void testSlowClientIsCutOff() {
    std::string path = dir + "/slow.sock";
    UnixSocketServer server;
    CHECK(server.start(path));

    std::thread client([&] {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            for (int i = 0; i < 6; ++i) {
                if (write(fd, "x", 1) != 1) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
            }
        }
        close(fd);
    });

    auto begin = std::chrono::steady_clock::now();
    std::string request;
    CHECK(!server.receive(1000, request));
    auto elapsed = std::chrono::steady_clock::now() - begin;
    CHECK(elapsed < std::chrono::milliseconds(3000));
    client.join();
}

// A newline ends a request even if the client doesn't close its side.
void testNewlineTerminatesRequest() {
    std::string path = dir + "/nl.sock";
    UnixSocketServer server;
    CHECK(server.start(path));

    std::string reply;
    std::thread client([&] {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            write(fd, "status\n", 7) == 7) {
            char buffer[64]{};
            ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                reply.assign(buffer, static_cast<size_t>(n));
            }
        }
        close(fd);
    });

    auto begin = std::chrono::steady_clock::now();
    std::string request;
    bool received = false;
    for (int i = 0; i < 20 && !received; ++i) {
        received = server.receive(100, request);
    }
    CHECK(received);
    CHECK(request == "status");
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(1000));
    server.send("ok");
    client.join();
    CHECK(reply == "ok\n");
}

void testPathTooLong() {
    UnixSocketServer server;
    CHECK(!server.start(dir + "/" + std::string(200, 'a') + ".sock"));
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);  // as the daemon and CLI do
    dir = makeTempDir();
    testRoundTrip();
    testLargeMessage();
    testRefusesToStealSocket();
    testReplacesStaleSocket();
    testStopRemovesSocket();
    testPathTooLong();
    testSlowClientIsCutOff();
    testNewlineTerminatesRequest();
    return finishTests("ipc_tests");
}
