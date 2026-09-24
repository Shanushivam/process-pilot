#ifndef PROCESSPILOT_DAEMON_HPP
#define PROCESSPILOT_DAEMON_HPP

#include "../dependency/dependency_graph.hpp"
#include "../ipc/unix_socket.hpp"
#include "../process/process_manager.hpp"
#include "../service/service_config.hpp"

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <string>

enum class ServiceState {
    Stopped,     // never started, or stopped by the user
    Running,
    Restarting,  // exited; waiting RestartSec before starting again
    Exited,      // exited cleanly and policy says don't restart
    Failed       // exited with an error and won't be restarted
};

struct Service {
    using Clock = std::chrono::steady_clock;

    ServiceConfig config;
    ProcessManager process;
    ServiceState state = ServiceState::Stopped;
    int restarts = 0;
    std::deque<Clock::time_point> recentStarts;
    Clock::time_point restartAt;
};

class ProcessPilotDaemon {
public:
    explicit ProcessPilotDaemon(std::string socketPath);

    // Binds the control socket. Call before run() (and before daemonizing,
    // so startup errors are still visible on the terminal).
    bool listen();

    // Serves requests and supervises services until `shutdown` or
    // SIGINT/SIGTERM, then stops every service.
    bool run();

    // Handles one request line and returns the response. Public for testing.
    std::string handle(const std::string& request);

    // One supervision pass: reaps exited services and applies restart
    // policies. run() calls this every loop iteration; public for testing.
    void supervise();

private:
    std::string commandStart(const std::string& target);
    std::string commandStop(const std::string& name);
    std::string commandRestart(const std::string& name);
    std::string commandStatus(const std::string& name);

    bool loadService(const std::string& path, std::string& name,
                     std::string& error);
    bool startWithDependencies(const std::string& name, std::string& error);
    bool startService(Service& service, std::string& error);
    void stopService(Service& service);
    void stopAll();

    Service* find(const std::string& name);
    std::string describe(Service& service, size_t nameWidth);

    std::map<std::string, std::unique_ptr<Service>> services;
    DependencyGraph dependencies;
    UnixSocketServer server;
    std::string socketPath;
    bool shutdownRequested = false;
};

// Installs SIGINT/SIGTERM handlers that make run() shut down cleanly,
// and ignores SIGPIPE.
void installSignalHandlers();

const char* serviceStateName(ServiceState state);

#endif
