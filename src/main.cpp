#include "daemon/daemon.hpp"
#include "ipc/unix_socket.hpp"
#include "logging/logger.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void usage() {
    std::cout
        << "Usage: processpilot [options]\n\n"
        << "Options:\n"
        << "  -d, --daemon       Run in the background (detach from terminal)\n"
        << "  -l, --log FILE     Append logs to FILE instead of stdout\n"
        << "  -s, --socket PATH  Control socket path (default: "
        << defaultSocketPath() << ")\n"
        << "  -h, --help         Show this help\n";
}

std::string absolutePath(const std::string& path) {
    if (path.empty() || path[0] == '/') {
        return path;
    }
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        return path;
    }
    return std::string(cwd) + "/" + path;
}

// Classic single-fork daemonization. The parent exits immediately without
// running destructors, so it doesn't remove the already-bound socket.
bool daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        std::cout << "ProcessPilot daemon started (pid " << pid << ")\n";
        std::cout.flush();
        _exit(0);
    }

    if (setsid() < 0) {
        return false;
    }
    if (chdir("/") != 0) {
        return false;
    }

    int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) {
            close(devNull);
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool background = false;
    std::string logFile;
    std::string socketPath = defaultSocketPath();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;

        if (arg == "-d" || arg == "--daemon") {
            background = true;
        } else if ((arg == "-l" || arg == "--log") && hasValue) {
            logFile = absolutePath(argv[++i]);
        } else if ((arg == "-s" || arg == "--socket") && hasValue) {
            socketPath = absolutePath(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n\n";
            usage();
            return 2;
        }
    }

    // --daemon changes directory to /, so a relative path from
    // $PROCESSPILOT_SOCKET would later point somewhere else.
    socketPath = absolutePath(socketPath);

    if (!logFile.empty() && !Logger::setLogFile(logFile)) {
        std::cerr << "Could not open log file " << logFile << ": "
                  << std::strerror(errno) << "\n";
        return 1;
    }

    if (background && logFile.empty()) {
        std::cerr << "Note: --daemon without --log discards all log output\n";
    }

    installSignalHandlers();

    ProcessPilotDaemon daemon(socketPath);
    if (!daemon.listen()) {
        return 1;
    }

    if (background) {
        if (!daemonize()) {
            Logger::error("Could not daemonize");
            return 1;
        }
    } else {
        std::cout << "ProcessPilot daemon is running. Press Ctrl+C to stop.\n";
    }

    return daemon.run() ? 0 : 1;
}
