#include "resource_monitor.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

ProcessStats ResourceMonitor::readProcess(pid_t pid) {
    ProcessStats stats;
    if (pid <= 0) {
        return stats;
    }

    std::ifstream file("/proc/" + std::to_string(pid) + "/status");
    std::string line;

    while (std::getline(file, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            // strtol never throws; malformed input just yields 0.
            auto pos = line.find_first_of("0123456789");
            if (pos != std::string::npos) {
                stats.memoryKb = std::strtol(line.c_str() + pos, nullptr, 10);
                stats.available = true;
            }
            break;
        }
    }

    return stats;
}
