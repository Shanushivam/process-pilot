#ifndef SERVICE_CONFIG_HPP
#define SERVICE_CONFIG_HPP

#include <string>
#include <vector>

enum class RestartPolicy {
    No,
    OnFailure,
    Always
};

struct ServiceConfig {
    std::string name;
    std::string command;
    std::string workingDirectory;
    std::string sourceFile;
    RestartPolicy restart = RestartPolicy::No;
    int restartSec = 1;
    int stopTimeoutSec = 5;
    std::vector<std::string> dependencies;  // from Requires=
};

// Loads and validates a .service file. On failure returns false and fills
// `error` with a human-readable reason.
bool loadServiceConfig(const std::string& path,
                       ServiceConfig& out,
                       std::string& error);

bool isValidServiceName(const std::string& name);

const char* restartPolicyName(RestartPolicy policy);

#endif
