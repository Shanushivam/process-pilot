#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <string>
#include <map>

class ConfigParser {
public:
    bool load(const std::string& filename);
    std::string get(const std::string& key) const;

private:
    std::map<std::string, std::string> values;
};

#endif
