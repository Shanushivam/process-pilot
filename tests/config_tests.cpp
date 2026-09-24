#include "../src/config/config_parser.hpp"
#include "../src/service/service_config.hpp"
#include "test_util.hpp"

#ifndef PROCESSPILOT_SOURCE_DIR
#define PROCESSPILOT_SOURCE_DIR ".."
#endif

namespace {

std::string dir;

bool loads(const std::string& contents, ServiceConfig& config,
           std::string& error) {
    std::string path = writeFile(dir + "/test.service", contents);
    return loadServiceConfig(path, config, error);
}

bool rejects(const std::string& contents) {
    ServiceConfig config;
    std::string error;
    bool ok = loads(contents, config, error);
    return !ok && !error.empty();
}

void testParserBasics() {
    ConfigParser parser;
    std::string path = std::string(PROCESSPILOT_SOURCE_DIR) +
                       "/configs/demo.service";
    CHECK(parser.load(path));
    CHECK(parser.get("Name") == "demo");
    CHECK(parser.get("Command") == "/bin/sleep 60");
    CHECK(parser.get("Missing").empty());
    CHECK(!parser.load(dir + "/nope.service"));
}

void testWhitespaceAndComments() {
    ServiceConfig config;
    std::string error;
    CHECK(loads("# comment\n"
                "[Service]\n"
                "  Name = web  \n"
                "Command = echo a=b  \n"
                "Restart=on-failure\n"
                "RestartSec=3\n"
                "StopTimeoutSec=10\n"
                "Requires=db, cache  queue\n",
                config, error));
    CHECK(config.name == "web");
    CHECK(config.command == "echo a=b");
    CHECK(config.restart == RestartPolicy::OnFailure);
    CHECK(config.restartSec == 3);
    CHECK(config.stopTimeoutSec == 10);
    CHECK(config.dependencies.size() == 3);
    CHECK(config.dependencies[0] == "db");
    CHECK(config.dependencies[2] == "queue");
}

void testDefaults() {
    ServiceConfig config;
    std::string error;
    CHECK(loads("Name=a\nCommand=true\n", config, error));
    CHECK(config.restart == RestartPolicy::No);
    CHECK(config.restartSec == 1);
    CHECK(config.stopTimeoutSec == 5);
    CHECK(config.dependencies.empty());
}

void testValidation() {
    CHECK(rejects("Command=true\n"));                           // no name
    CHECK(rejects("Name=a b\nCommand=true\n"));                 // bad name
    CHECK(rejects("Name=../x\nCommand=true\n"));                // bad name
    CHECK(rejects("Name=a\n"));                                 // no command
    CHECK(rejects("Name=a\nCommand=true\nRestart=sometimes\n"));
    CHECK(rejects("Name=a\nCommand=true\nRestartSec=abc\n"));
    CHECK(rejects("Name=a\nCommand=true\nRestartSec=-1\n"));
    CHECK(rejects("Name=a\nCommand=true\nStopTimeoutSec=999\n"));
    CHECK(rejects("Name=a\nCommand=true\nRequires=a\n"));      // self
    CHECK(rejects("Name=a\nCommand=true\nRequires=b/c\n"));

    ServiceConfig config;
    std::string error;
    CHECK(!loadServiceConfig(dir + "/missing.service", config, error));
}

}  // namespace

int main() {
    dir = makeTempDir();
    testParserBasics();
    testWhitespaceAndComments();
    testDefaults();
    testValidation();
    return finishTests("config_tests");
}
