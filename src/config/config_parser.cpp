#include "config_parser.hpp"

#include <fstream>
#include <algorithm>
#include <cctype>

bool ConfigParser::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    values.clear();
    std::string line;

    while (std::getline(file, line)) {
        line.erase(line.begin(),
                   std::find_if(line.begin(), line.end(),
                                [](unsigned char c) { return !std::isspace(c); }));

        if (line.empty() || line[0] == '#' || line[0] == '[') {
            continue;
        }

        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        key.erase(std::remove_if(key.begin(), key.end(),
                                 [](unsigned char c) { return std::isspace(c); }),
                  key.end());

        value.erase(value.begin(),
                    std::find_if(value.begin(), value.end(),
                                 [](unsigned char c) { return !std::isspace(c); }));

        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }

        values[key] = value;
    }

    return true;
}

std::string ConfigParser::get(const std::string& key) const {
    auto it = values.find(key);
    return it == values.end() ? "" : it->second;
}
