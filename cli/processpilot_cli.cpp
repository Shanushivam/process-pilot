#include "../src/ipc/unix_socket.hpp"

#include <climits>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace {

void usage() {
    std::cout
        << "ProcessPilot CLI\n\n"
        << "Usage:\n"
        << "  processpilot-cli [--socket PATH] <command>\n\n"
        << "Commands:\n"
        << "  start <service-file|name>  Load (if a file) and start a service\n"
        << "                             and anything it Requires=\n"
        << "  stop <name>                Stop a service\n"
        << "  restart <name>             Stop and start a service\n"
        << "  status [name]              Show all services, or one\n"
        << "  shutdown                   Stop all services and the daemon\n";
}

// The daemon may run in a different working directory, so send it an
// absolute path whenever the argument names an existing file.
std::string resolveTarget(const std::string& target) {
    struct stat info {};
    if (stat(target.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        return target;
    }
    char resolved[PATH_MAX];
    if (realpath(target.c_str(), resolved)) {
        return resolved;
    }
    return target;
}

}  // namespace

int main(int argc, char* argv[]) {
    // If the daemon closes the connection early, report it instead of being
    // killed by SIGPIPE on write.
    std::signal(SIGPIPE, SIG_IGN);

    std::string socketPath = defaultSocketPath();
    int index = 1;

    if (index < argc && std::string(argv[index]) == "--socket") {
        if (index + 1 >= argc) {
            usage();
            return 2;
        }
        socketPath = argv[index + 1];
        index += 2;
    }

    if (index >= argc) {
        usage();
        return 2;
    }

    std::string command = argv[index];
    std::string argument = index + 1 < argc ? argv[index + 1] : "";

    if (command == "-h" || command == "--help" || command == "help") {
        usage();
        return 0;
    }

    std::string request;
    if (command == "start") {
        if (argument.empty()) {
            std::cerr << "Missing service file or name.\n";
            return 2;
        }
        request = "start " + resolveTarget(argument);
    } else if (command == "stop" || command == "restart") {
        if (argument.empty()) {
            std::cerr << "Missing service name.\n";
            return 2;
        }
        request = command + " " + argument;
    } else if (command == "status" || command == "list") {
        request = argument.empty() ? "status" : "status " + argument;
    } else if (command == "shutdown") {
        request = "shutdown";
    } else {
        std::cerr << "Unknown command: " << command << "\n\n";
        usage();
        return 2;
    }

    UnixSocketClient client;
    if (!client.connectTo(socketPath)) {
        std::cerr << "Could not connect to ProcessPilot daemon at "
                  << socketPath << ".\n"
                  << "Start the daemon first.\n";
        return 1;
    }

    std::string response = client.request(request);
    if (response.empty()) {
        std::cerr << "No response from daemon.\n";
        return 1;
    }

    bool failed = response.rfind("ERROR", 0) == 0;
    (failed ? std::cerr : std::cout) << response << "\n";
    return failed ? 1 : 0;
}
