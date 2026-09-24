#include "service_config.hpp"
#include "../config/config_parser.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sstream>

namespace {

bool parseSeconds(const std::string& text, int& out, long max) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || value < 0 || value > max) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

std::vector<std::string> splitList(const std::string& text) {
    std::string normalized = text;
    for (char& c : normalized) {
        if (c == ',') {
            c = ' ';
        }
    }

    std::vector<std::string> items;
    std::istringstream input(normalized);
    std::string item;
    while (input >> item) {
        items.push_back(item);
    }
    return items;
}

}  // namespace

bool isValidServiceName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (unsigned char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

const char* restartPolicyName(RestartPolicy policy) {
    switch (policy) {
        case RestartPolicy::No: return "no";
        case RestartPolicy::OnFailure: return "on-failure";
        case RestartPolicy::Always: return "always";
    }
    return "no";
}

bool loadServiceConfig(const std::string& path,
                       ServiceConfig& out,
                       std::string& error) {
    ConfigParser parser;
    if (!parser.load(path)) {
        error = "could not read " + path;
        return false;
    }

    ServiceConfig config;
    config.sourceFile = path;
    config.name = parser.get("Name");
    config.command = parser.get("Command");
    config.workingDirectory = parser.get("WorkingDirectory");

    if (!isValidServiceName(config.name)) {
        error = "Name must be 1-64 characters of [A-Za-z0-9._-]";
        return false;
    }

    if (config.command.empty()) {
        error = "Command is missing";
        return false;
    }

    std::string restart = parser.get("Restart");
    if (restart.empty() || restart == "no") {
        config.restart = RestartPolicy::No;
    } else if (restart == "on-failure") {
        config.restart = RestartPolicy::OnFailure;
    } else if (restart == "always") {
        config.restart = RestartPolicy::Always;
    } else {
        error = "Restart must be one of: no, on-failure, always";
        return false;
    }

    std::string restartSec = parser.get("RestartSec");
    if (!restartSec.empty() && !parseSeconds(restartSec, config.restartSec, 3600)) {
        error = "RestartSec must be a whole number of seconds (0-3600)";
        return false;
    }

    std::string stopTimeout = parser.get("StopTimeoutSec");
    if (!stopTimeout.empty() && !parseSeconds(stopTimeout, config.stopTimeoutSec, 60)) {
        error = "StopTimeoutSec must be a whole number of seconds (0-60)";
        return false;
    }

    config.dependencies = splitList(parser.get("Requires"));
    for (const auto& dependency : config.dependencies) {
        if (!isValidServiceName(dependency)) {
            error = "invalid service name in Requires: " + dependency;
            return false;
        }
        if (dependency == config.name) {
            error = "a service cannot require itself";
            return false;
        }
    }

    out = config;
    return true;
}
