#include "process_manager.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace {

// Runs in the forked child: close everything except stdin/stdout/stderr so
// services don't inherit the daemon's log file, sockets or other files.
void closeInheritedFds() {
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, 3U, ~0U, 0U) == 0) {
        return;
    }
#endif
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536) {
        max = 65536;
    }
    for (int fd = 3; fd < max; ++fd) {
        close(fd);
    }
}

}  // namespace

ProcessManager::ProcessManager()
    : pid(-1), hasExitInfo(false), everExited(false), lastStatus(0) {}

ProcessManager::~ProcessManager() {
    if (pid > 0) {
        stop(1000);
    }
}

bool ProcessManager::start(const std::string& command,
                           const std::string& workingDirectory) {
    if (isRunning()) {
        return false;
    }

    pid_t child = fork();

    if (child < 0) {
        return false;
    }

    if (child == 0) {
        // New process group, so stop() can signal the whole tree.
        setpgid(0, 0);

        // The daemon ignores/handles some signals; the service should start
        // with default dispositions and an empty signal mask.
        std::signal(SIGPIPE, SIG_DFL);
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGCHLD, SIG_DFL);
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, nullptr);

        closeInheritedFds();

        if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) {
            _exit(126);
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    // Also set it from the parent to avoid racing the child.
    setpgid(child, child);

    pid = child;
    hasExitInfo = false;
    return true;
}

bool ProcessManager::stop(int timeoutMs) {
    if (!isRunning()) {
        return false;
    }

    if (kill(-pid, SIGTERM) != 0) {
        kill(pid, SIGTERM);
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        if (checkExited()) {
            // Leader is gone; make sure nothing else in the group survives.
            kill(-pid, SIGKILL);
            pid = -1;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    recordExit(status);
    pid = -1;
    return true;
}

bool ProcessManager::checkExited() {
    if (pid <= 0 || hasExitInfo) {
        return false;
    }

    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == pid) {
        recordExit(status);
        return true;
    }

    if (result < 0 && errno == ECHILD) {
        // Not our child any more (already reaped elsewhere).
        recordExit(0);
        return true;
    }

    return false;
}

bool ProcessManager::isRunning() {
    checkExited();
    return pid > 0 && !hasExitInfo;
}

pid_t ProcessManager::getPid() const {
    return hasExitInfo ? -1 : pid;
}

void ProcessManager::recordExit(int status) {
    lastStatus = status;
    hasExitInfo = true;
    everExited = true;
}

bool ProcessManager::lastExitWasClean() const {
    return everExited && WIFEXITED(lastStatus) && WEXITSTATUS(lastStatus) == 0;
}

std::string ProcessManager::lastExitDescription() const {
    if (!everExited) {
        return "-";
    }
    if (WIFEXITED(lastStatus)) {
        return "exit=" + std::to_string(WEXITSTATUS(lastStatus));
    }
    if (WIFSIGNALED(lastStatus)) {
        return "signal=" + std::to_string(WTERMSIG(lastStatus));
    }
    return "unknown";
}
