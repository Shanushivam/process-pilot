#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {

std::ofstream logFile;

void logMessage(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);

    std::ostream& out = logFile.is_open() ? static_cast<std::ostream&>(logFile)
                                          : std::cout;

    out << "[" << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
        << "] [" << level << "] " << message << std::endl;
}

}  // namespace

bool Logger::setLogFile(const std::string& path) {
    logFile.open(path, std::ios::app);
    return logFile.is_open();
}

void Logger::info(const std::string& message) {
    logMessage("INFO", message);
}

void Logger::warn(const std::string& message) {
    logMessage("WARN", message);
}

void Logger::error(const std::string& message) {
    logMessage("ERROR", message);
}
