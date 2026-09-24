#include "../src/daemon/daemon.hpp"
#include "../src/logging/logger.hpp"
#include "test_util.hpp"

#include <chrono>
#include <thread>

using namespace std::chrono;

namespace {

std::string dir;

bool starts(const std::string& response, const std::string& prefix) {
    return response.rfind(prefix, 0) == 0;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string service(const std::string& name, const std::string& body) {
    return writeFile(dir + "/" + name + ".service",
                     "[Service]\nName=" + name + "\n" + body);
}

// Runs supervision passes until `status <name>` contains `expected`.
bool superviseUntil(ProcessPilotDaemon& daemon, const std::string& name,
                    const std::string& expected, milliseconds timeout) {
    auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        daemon.supervise();
        if (contains(daemon.handle("status " + name), expected)) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(20));
    }
    return false;
}

void testStartStopStatus() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    std::string file = service("sleeper", "Command=sleep 30\n");

    CHECK(daemon.handle("status") == "No services loaded");
    CHECK(starts(daemon.handle("start " + file), "OK started sleeper"));
    CHECK(contains(daemon.handle("status sleeper"), "running"));
    CHECK(starts(daemon.handle("start " + file), "ERROR sleeper is already running"));
    CHECK(starts(daemon.handle("start sleeper"), "ERROR sleeper is already running"));

    CHECK(daemon.handle("stop sleeper") == "OK stopped sleeper");
    CHECK(contains(daemon.handle("status sleeper"), "stopped"));
    CHECK(starts(daemon.handle("stop sleeper"), "ERROR sleeper is not running"));

    // Once loaded, a service can be started by name.
    CHECK(starts(daemon.handle("start sleeper"), "OK started sleeper"));
    CHECK(starts(daemon.handle("restart sleeper"), "OK restarted sleeper"));
    CHECK(daemon.handle("stop sleeper") == "OK stopped sleeper");
}

void testErrors() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    CHECK(starts(daemon.handle("bogus"), "ERROR unknown command"));
    CHECK(starts(daemon.handle("start"), "ERROR usage"));
    CHECK(starts(daemon.handle("stop"), "ERROR usage"));
    CHECK(starts(daemon.handle("stop nobody"), "ERROR unknown service"));
    CHECK(starts(daemon.handle("status nobody"), "ERROR unknown service"));
    CHECK(starts(daemon.handle("start " + dir + "/missing.service"), "ERROR"));
    CHECK(starts(daemon.handle("start " + service("bad", "Restart=maybe\nCommand=true\n")),
                 "ERROR Restart must be"));
}

// Regression: a service that exited used to show as running forever.
void testExitIsNoticed() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    CHECK(starts(daemon.handle("start " + service("oneshot", "Command=true\n")), "OK"));
    CHECK(superviseUntil(daemon, "oneshot", "exited", seconds(3)));

    CHECK(starts(daemon.handle("start " + service("crasher", "Command=exit 4\n")), "OK"));
    CHECK(superviseUntil(daemon, "crasher", "failed", seconds(3)));
    CHECK(contains(daemon.handle("status crasher"), "exit=4"));

    // And it can be started again afterwards.
    CHECK(starts(daemon.handle("start oneshot"), "OK started oneshot"));
}

void testRestartPolicies() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");

    // on-failure doesn't restart a clean exit.
    daemon.handle("start " + service("clean", "Command=true\nRestart=on-failure\nRestartSec=0\n"));
    CHECK(superviseUntil(daemon, "clean", "exited", seconds(3)));

    // always restarts, and gives up after the start limit.
    daemon.handle("start " + service("flappy", "Command=exit 1\nRestart=always\nRestartSec=0\n"));
    CHECK(superviseUntil(daemon, "flappy", "failed", seconds(5)));
    std::string status = daemon.handle("status flappy");
    CHECK(contains(status, "failed"));
    CHECK(contains(status, "exit=1"));
}

void testDependencies() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    std::string db = service("db", "Command=sleep 30\n");
    std::string web = service("web", "Command=sleep 30\nRequires=db\n");

    CHECK(starts(daemon.handle("start " + web), "ERROR web requires db, which is not loaded"));
    CHECK(contains(daemon.handle("status web"), "stopped"));

    // Loading db and then starting web by name starts db first.
    CHECK(starts(daemon.handle("start " + db), "OK"));
    CHECK(daemon.handle("stop db") == "OK stopped db");
    CHECK(starts(daemon.handle("start web"), "OK started web"));
    CHECK(contains(daemon.handle("status db"), "running"));

    CHECK(starts(daemon.handle("stop db"), "ERROR cannot stop db: required by web"));
    CHECK(daemon.handle("stop web") == "OK stopped web");
    CHECK(daemon.handle("stop db") == "OK stopped db");
}

void testDependencyCycle() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    CHECK(starts(daemon.handle("start " + service("a", "Command=sleep 30\n")), "OK"));
    daemon.handle("start " + service("b", "Command=sleep 30\nRequires=a\n"));

    // Redefining a to require b would close the loop.
    daemon.handle("stop b");
    daemon.handle("stop a");
    CHECK(starts(daemon.handle("start " + service("a", "Command=sleep 30\nRequires=b\n")),
                 "ERROR dependency cycle"));
}

// Regression: long names ran into the STATE column.
void testStatusAlignsLongNames() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    daemon.handle("start " + service("a-really-long-service-name", "Command=sleep 30\n"));
    daemon.handle("start " + service("x", "Command=sleep 30\n"));

    std::string status = daemon.handle("status");
    CHECK(contains(status, "a-really-long-service-name  running"));
    CHECK(contains(daemon.handle("status x"), "x  running"));
}

void testShutdown() {
    ProcessPilotDaemon daemon(dir + "/unused.sock");
    CHECK(daemon.handle("shutdown") == "OK shutting down");
}

}  // namespace

int main() {
    dir = makeTempDir();
    Logger::setLogFile(dir + "/daemon.log");

    testStartStopStatus();
    testErrors();
    testExitIsNoticed();
    testRestartPolicies();
    testDependencies();
    testDependencyCycle();
    testStatusAlignsLongNames();
    testShutdown();
    return finishTests("daemon_tests");
}
