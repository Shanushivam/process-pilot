# ProcessPilot Requirements

## Functional Requirements

1. Start, stop and restart multiple configured services.
2. Detect when a service exits and record its exit status.
3. Restart services according to their restart policy, with a start limit.
4. Stop services gracefully, escalating to SIGKILL after a timeout.
5. Report service status.
6. Read and validate service configuration from a file.
7. Communicate with the daemon using a Unix domain socket that only the
   daemon's user can use.
8. Provide basic process memory information.
9. Start dependencies first, reject dependency cycles, and refuse to stop a
   service that a running service depends on.
10. Provide timestamped logging to stdout or a file.
11. Provide a command-line interface.
12. Include unit and integration tests runnable with CTest.

## Platform

ProcessPilot uses Linux/POSIX APIs such as:

- fork()
- exec()
- setpgid()
- kill()
- waitpid()
- sigaction()
- poll()
- Unix domain sockets
- /proc

Therefore it should be built and tested on Linux or WSL. It also builds on
macOS, where `/proc` memory information is unavailable.
