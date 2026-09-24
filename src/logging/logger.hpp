#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

class Logger {
public:
    // Appends to `path` instead of stdout. Returns false if it can't be opened.
    static bool setLogFile(const std::string& path);

    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
};

#endif
