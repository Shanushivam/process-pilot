#ifndef PROCESS_MANAGER_HPP
#define PROCESS_MANAGER_HPP

#include <string>
#include <sys/types.h>

// Owns a single child process. The child runs in its own process group so
// that stopping it also stops anything it spawned (e.g. through `sh -c`).
class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    bool start(const std::string& command,
               const std::string& workingDirectory = "");

    // Sends SIGTERM to the process group, waits up to timeoutMs, then SIGKILL.
    // Always reaps the child, so it never blocks forever.
    bool stop(int timeoutMs = 5000);

    // Non-blocking. Reaps the child if it has exited and returns true exactly
    // once for that exit; the exit details are then available below.
    bool checkExited();

    bool isRunning();
    pid_t getPid() const;

    bool lastExitWasClean() const;
    std::string lastExitDescription() const;

private:
    void recordExit(int status);

    pid_t pid;
    bool hasExitInfo;   // current child has exited and been reaped
    bool everExited;    // lastStatus is valid (kept across restarts)
    int lastStatus;
};

#endif
