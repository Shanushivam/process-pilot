#include "../src/process/process_manager.hpp"
#include "test_util.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <thread>

using namespace std::chrono;

namespace {

bool waitUntilExited(ProcessManager& manager, milliseconds timeout) {
    auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        if (!manager.isRunning()) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return false;
}

bool processGone(pid_t pid, milliseconds timeout) {
    auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(20));
    }
    return false;
}

void testStartAndStop() {
    ProcessManager manager;
    CHECK(!manager.isRunning());
    CHECK(manager.start("sleep 5"));
    CHECK(manager.isRunning());
    CHECK(manager.getPid() > 0);
    CHECK(!manager.start("sleep 5"));  // already running
    CHECK(manager.stop());
    CHECK(!manager.isRunning());
    CHECK(!manager.stop());  // nothing to stop
}

// Regression: a child that exits by itself used to stay a zombie and be
// reported as running forever, blocking any new start.
void testExitedChildIsDetected() {
    ProcessManager manager;
    CHECK(manager.start("true"));
    CHECK(waitUntilExited(manager, seconds(2)));
    CHECK(manager.lastExitWasClean());
    CHECK(manager.lastExitDescription() == "exit=0");
    CHECK(manager.getPid() == -1);

    CHECK(manager.start("true"));  // can start again
    CHECK(waitUntilExited(manager, seconds(2)));
}

void testExitCodeIsRecorded() {
    ProcessManager manager;
    CHECK(manager.start("exit 3"));
    CHECK(waitUntilExited(manager, seconds(2)));
    CHECK(!manager.lastExitWasClean());
    CHECK(manager.lastExitDescription() == "exit=3");
}

void testCheckExitedReportsOnce() {
    ProcessManager manager;
    CHECK(manager.start("true"));
    bool seen = false;
    auto deadline = steady_clock::now() + seconds(2);
    while (!seen && steady_clock::now() < deadline) {
        seen = manager.checkExited();
        std::this_thread::sleep_for(milliseconds(10));
    }
    CHECK(seen);
    CHECK(!manager.checkExited());
}

// Regression: stop() used to block forever on a child ignoring SIGTERM.
void testStopEscalatesToSigkill() {
    ProcessManager manager;
    CHECK(manager.start("trap '' TERM; while :; do sleep 1; done"));
    std::this_thread::sleep_for(milliseconds(200));  // let the trap install

    auto begin = steady_clock::now();
    CHECK(manager.stop(300));
    auto elapsed = steady_clock::now() - begin;

    CHECK(elapsed < seconds(3));
    CHECK(!manager.isRunning());
    CHECK(manager.lastExitDescription() == "signal=9");
}

// Regression: grandchildren started through `sh -c` used to survive stop().
void testStopKillsProcessGroup() {
    std::string dir = makeTempDir();
    std::string pidFile = dir + "/grandchild.pid";

    ProcessManager manager;
    CHECK(manager.start("sleep 30 & echo $! > '" + pidFile + "'; wait"));

    pid_t grandchild = 0;
    auto deadline = steady_clock::now() + seconds(2);
    while (grandchild <= 0 && steady_clock::now() < deadline) {
        grandchild = std::atoi(readFile(pidFile).c_str());
        std::this_thread::sleep_for(milliseconds(20));
    }
    CHECK(grandchild > 0);

    CHECK(manager.stop(1000));
    if (grandchild > 0) {
        CHECK(processGone(grandchild, seconds(3)));
    }
}

void testWorkingDirectory() {
    std::string dir = makeTempDir();
    ProcessManager manager;
    CHECK(manager.start("pwd -P > out.txt", dir));
    CHECK(waitUntilExited(manager, seconds(2)));
    CHECK(manager.lastExitWasClean());
    CHECK(!readFile(dir + "/out.txt").empty());

    ProcessManager badDir;
    CHECK(badDir.start("true", dir + "/does-not-exist"));
    CHECK(waitUntilExited(badDir, seconds(2)));
    CHECK(badDir.lastExitDescription() == "exit=126");
}

// Regression: services used to inherit the daemon's open files.
void testFileDescriptorsNotInherited() {
    std::string dir = makeTempDir();
    int fd = open((dir + "/private.txt").c_str(), O_CREAT | O_RDWR, 0600);
    CHECK(fd > 2);

    ProcessManager manager;
    CHECK(manager.start("[ -e /dev/fd/" + std::to_string(fd) + " ] && exit 9 || exit 0"));
    CHECK(waitUntilExited(manager, seconds(2)));
    CHECK(manager.lastExitDescription() == "exit=0");
    close(fd);
}

}  // namespace

int main() {
    testStartAndStop();
    testExitedChildIsDetected();
    testExitCodeIsRecorded();
    testCheckExitedReportsOnce();
    testStopEscalatesToSigkill();
    testStopKillsProcessGroup();
    testWorkingDirectory();
    testFileDescriptorsNotInherited();
    return finishTests("process_tests");
}
