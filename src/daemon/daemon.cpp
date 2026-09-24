#include "daemon.hpp"
#include "../logging/logger.hpp"
#include "../monitor/resource_monitor.hpp"

#include <algorithm>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

volatile std::sig_atomic_t signalStop = 0;

void onStopSignal(int) {
    signalStop = 1;
}

constexpr int kPollIntervalMs = 200;
constexpr int kStartLimitBurst = 5;
constexpr auto kStartLimitInterval = std::chrono::seconds(60);

std::string trim(const std::string& text) {
    auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string directoryOf(const std::string& path) {
    auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const auto& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += name;
    }
    return joined;
}

}  // namespace

void installSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = onStopSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;  // no SA_RESTART: let poll() wake up
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    std::signal(SIGPIPE, SIG_IGN);
}

const char* serviceStateName(ServiceState state) {
    switch (state) {
        case ServiceState::Stopped: return "stopped";
        case ServiceState::Running: return "running";
        case ServiceState::Restarting: return "restarting";
        case ServiceState::Exited: return "exited";
        case ServiceState::Failed: return "failed";
    }
    return "unknown";
}

ProcessPilotDaemon::ProcessPilotDaemon(std::string path)
    : socketPath(std::move(path)) {}

bool ProcessPilotDaemon::listen() {
    if (!server.start(socketPath)) {
        Logger::error("Could not start control socket: " + server.lastError());
        return false;
    }
    Logger::info("Listening on " + socketPath);
    return true;
}

bool ProcessPilotDaemon::run() {
    Logger::info("ProcessPilot daemon running");

    while (!shutdownRequested && !signalStop) {
        std::string request;
        if (server.receive(kPollIntervalMs, request)) {
            server.send(handle(request));
        }
        supervise();
    }

    if (signalStop) {
        Logger::info("Received stop signal");
    }

    stopAll();
    server.stop();
    Logger::info("ProcessPilot daemon stopped");
    return true;
}

std::string ProcessPilotDaemon::handle(const std::string& request) {
    std::istringstream input(request);
    std::string command;
    input >> command;

    std::string argument;
    std::getline(input, argument);
    argument = trim(argument);

    if (command == "start") {
        return commandStart(argument);
    }
    if (command == "stop") {
        return commandStop(argument);
    }
    if (command == "restart") {
        return commandRestart(argument);
    }
    if (command == "status" || command == "list") {
        return commandStatus(argument);
    }
    if (command == "shutdown") {
        shutdownRequested = true;
        return "OK shutting down";
    }
    return "ERROR unknown command: " + command;
}

std::string ProcessPilotDaemon::commandStart(const std::string& target) {
    if (target.empty()) {
        return "ERROR usage: start <service-file|name>";
    }

    std::string name = target;
    std::string error;

    // A bare name refers to an already-loaded service; anything else is a file.
    if (!find(target) || target.find('/') != std::string::npos) {
        if (!loadService(target, name, error)) {
            return "ERROR " + error;
        }
    }

    Service* service = find(name);
    if (service->state == ServiceState::Running) {
        return "ERROR " + name + " is already running (pid " +
               std::to_string(service->process.getPid()) + ")";
    }

    if (!startWithDependencies(name, error)) {
        return "ERROR " + error;
    }

    return "OK started " + name + " (pid " +
           std::to_string(service->process.getPid()) + ")";
}

std::string ProcessPilotDaemon::commandStop(const std::string& name) {
    if (name.empty()) {
        return "ERROR usage: stop <name>";
    }

    Service* service = find(name);
    if (!service) {
        return "ERROR unknown service: " + name;
    }

    std::vector<std::string> activeDependents;
    for (const auto& dependent : dependencies.getDependents(name)) {
        Service* other = find(dependent);
        if (other && (other->state == ServiceState::Running ||
                      other->state == ServiceState::Restarting)) {
            activeDependents.push_back(dependent);
        }
    }
    if (!activeDependents.empty()) {
        return "ERROR cannot stop " + name + ": required by " +
               joinNames(activeDependents) + " (stop those first)";
    }

    if (service->state == ServiceState::Stopped) {
        return "ERROR " + name + " is not running";
    }

    stopService(*service);
    return "OK stopped " + name;
}

std::string ProcessPilotDaemon::commandRestart(const std::string& name) {
    if (name.empty()) {
        return "ERROR usage: restart <name>";
    }

    Service* service = find(name);
    if (!service) {
        return "ERROR unknown service: " + name;
    }

    stopService(*service);

    std::string error;
    if (!startWithDependencies(name, error)) {
        return "ERROR " + error;
    }
    return "OK restarted " + name + " (pid " +
           std::to_string(service->process.getPid()) + ")";
}

std::string ProcessPilotDaemon::commandStatus(const std::string& name) {
    if (!name.empty()) {
        Service* service = find(name);
        if (!service) {
            return "ERROR unknown service: " + name;
        }
        return describe(*service, name.size() + 2);
    }

    if (services.empty()) {
        return "No services loaded";
    }

    size_t nameWidth = 16;
    for (const auto& [serviceName, service] : services) {
        nameWidth = std::max(nameWidth, serviceName.size() + 2);
    }

    std::ostringstream out;
    out << std::left << std::setw(static_cast<int>(nameWidth)) << "NAME"
        << std::setw(12) << "STATE"
        << std::setw(9) << "PID" << std::setw(12) << "MEMORY"
        << std::setw(10) << "RESTARTS" << "LAST EXIT";
    for (auto& [serviceName, service] : services) {
        out << "\n" << describe(*service, nameWidth);
    }
    return out.str();
}

bool ProcessPilotDaemon::loadService(const std::string& path,
                                     std::string& name,
                                     std::string& error) {
    ServiceConfig config;
    if (!loadServiceConfig(path, config, error)) {
        return false;
    }

    if (!config.workingDirectory.empty() && config.workingDirectory[0] != '/') {
        config.workingDirectory =
            directoryOf(config.sourceFile) + "/" + config.workingDirectory;
    }

    Service* existing = find(config.name);
    if (existing && existing->state == ServiceState::Running) {
        error = config.name + " is already running (pid " +
                std::to_string(existing->process.getPid()) + ")";
        return false;
    }

    if (dependencies.wouldCreateCycle(config.name, config.dependencies)) {
        error = "dependency cycle involving " + config.name;
        return false;
    }

    dependencies.setDependencies(config.name, config.dependencies);
    name = config.name;

    if (existing) {
        // Stopped/exited/failed: reload with the new definition.
        stopService(*existing);
        existing->config = config;
        Logger::info("Reloaded service " + name + " from " + path);
    } else {
        auto service = std::make_unique<Service>();
        service->config = config;
        services.emplace(name, std::move(service));
        Logger::info("Loaded service " + name + " from " + path);
    }
    return true;
}

bool ProcessPilotDaemon::startWithDependencies(const std::string& name,
                                               std::string& error) {
    std::vector<std::string> order;
    if (!dependencies.startOrder(name, order)) {
        error = "dependency cycle involving " + name;
        return false;
    }

    // Check everything first so we never start half a dependency chain.
    for (const auto& item : order) {
        if (!find(item)) {
            error = name + " requires " + item +
                    ", which is not loaded (start its .service file first)";
            return false;
        }
    }

    for (const auto& item : order) {
        Service* service = find(item);
        if (service->state == ServiceState::Running) {
            continue;
        }
        service->restarts = 0;
        service->recentStarts.clear();
        if (!startService(*service, error)) {
            return false;
        }
    }
    return true;
}

bool ProcessPilotDaemon::startService(Service& service, std::string& error) {
    const auto& config = service.config;

    if (!service.process.start(config.command, config.workingDirectory)) {
        service.state = ServiceState::Failed;
        error = "could not start " + config.name;
        Logger::error("Could not start " + config.name);
        return false;
    }

    service.state = ServiceState::Running;
    service.recentStarts.push_back(Service::Clock::now());
    Logger::info("Started " + config.name + " (pid " +
                 std::to_string(service.process.getPid()) + ")");
    return true;
}

void ProcessPilotDaemon::stopService(Service& service) {
    if (service.process.isRunning()) {
        Logger::info("Stopping " + service.config.name);
        service.process.stop(service.config.stopTimeoutSec * 1000);
        Logger::info("Stopped " + service.config.name + " (" +
                     service.process.lastExitDescription() + ")");
    }
    service.state = ServiceState::Stopped;
}

void ProcessPilotDaemon::supervise() {
    auto now = Service::Clock::now();

    for (auto& [name, service] : services) {
        if (service->state == ServiceState::Running &&
            service->process.checkExited()) {
            bool clean = service->process.lastExitWasClean();
            RestartPolicy policy = service->config.restart;
            bool restart = policy == RestartPolicy::Always ||
                           (policy == RestartPolicy::OnFailure && !clean);

            std::string how = service->process.lastExitDescription();
            if (!restart) {
                service->state = clean ? ServiceState::Exited
                                       : ServiceState::Failed;
                Logger::warn(name + " exited (" + how + ")");
                continue;
            }

            while (!service->recentStarts.empty() &&
                   now - service->recentStarts.front() > kStartLimitInterval) {
                service->recentStarts.pop_front();
            }
            if (static_cast<int>(service->recentStarts.size()) >=
                kStartLimitBurst) {
                service->state = ServiceState::Failed;
                Logger::error(name + " exited (" + how + ") after " +
                              std::to_string(kStartLimitBurst) +
                              " starts within 60s; giving up");
                continue;
            }

            service->state = ServiceState::Restarting;
            service->restartAt =
                now + std::chrono::seconds(service->config.restartSec);
            Logger::warn(name + " exited (" + how + "); restarting in " +
                         std::to_string(service->config.restartSec) + "s");
        }

        if (service->state == ServiceState::Restarting &&
            now >= service->restartAt) {
            std::string error;
            if (startService(*service, error)) {
                service->restarts++;
            }
        }
    }
}

void ProcessPilotDaemon::stopAll() {
    // Stop dependents before the services they depend on.
    std::vector<std::string> order;
    for (const auto& [name, service] : services) {
        std::vector<std::string> chain;
        if (!dependencies.startOrder(name, chain)) {
            chain = {name};
        }
        for (const auto& item : chain) {
            if (std::find(order.begin(), order.end(), item) == order.end()) {
                order.push_back(item);
            }
        }
    }

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (Service* service = find(*it)) {
            stopService(*service);
        }
    }
}

Service* ProcessPilotDaemon::find(const std::string& name) {
    auto it = services.find(name);
    return it == services.end() ? nullptr : it->second.get();
}

std::string ProcessPilotDaemon::describe(Service& service, size_t nameWidth) {
    std::string pid = "-";
    std::string memory = "-";

    if (service.state == ServiceState::Running) {
        pid_t processId = service.process.getPid();
        pid = std::to_string(processId);
        ProcessStats stats = ResourceMonitor::readProcess(processId);
        memory = stats.available ? std::to_string(stats.memoryKb) + " kB" : "n/a";
    }

    std::ostringstream out;
    out << std::left << std::setw(static_cast<int>(nameWidth))
        << service.config.name
        << std::setw(12) << serviceStateName(service.state)
        << std::setw(9) << pid << std::setw(12) << memory
        << std::setw(10) << service.restarts
        << service.process.lastExitDescription();
    return out.str();
}
