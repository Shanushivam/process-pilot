# ProcessPilot

## Linux Service Manager and Process Supervisor

ProcessPilot is a lightweight Linux service manager and process supervisor written in C++.

It demonstrates core Linux system-programming concepts including process creation
and supervision, process groups, signal handling, configuration parsing, Unix
domain socket IPC, resource monitoring, dependency management and logging.

## Features

- Supervise multiple services at once
- Restart policies (`no`, `on-failure`, `always`) with a restart delay and a
  start limit (a service that crashes 5 times within 60s is marked `failed`)
- Service dependencies (`Requires=`): dependencies start first, cycles are
  rejected, and a service can't be stopped while something running needs it
- Graceful stop: `SIGTERM` to the whole process group, then `SIGKILL` after
  `StopTimeoutSec`
- Exit status tracking (`exit=N` / `signal=N`)
- Memory usage per service through `/proc`
- Unix domain socket control interface, restricted to the daemon's own user
- Optional background mode (`--daemon`) with a log file
- Clean shutdown on `SIGINT`/`SIGTERM`: services are stopped in reverse
  dependency order
- Command-line interface
- Unit and integration tests run with CTest

## Project Structure

```text
ProcessPilot/
├── src/
│   ├── main.cpp
│   ├── daemon/        # supervisor loop and command handling
│   ├── process/       # fork/exec, process groups, stop, reaping
│   ├── service/       # .service file loading and validation
│   ├── config/        # generic key=value parser
│   ├── monitor/       # /proc resource usage
│   ├── dependency/    # dependency graph, start order, cycle detection
│   ├── ipc/           # Unix domain socket server/client
│   └── logging/
├── cli/
├── tests/
├── configs/
├── docs/
├── CMakeLists.txt
└── README.md
```

## Build

Linux or WSL is recommended. It also builds and runs on macOS; memory
usage then shows as `n/a` because there is no `/proc`.

```bash
cmake -S . -B build
cmake --build build
```

## Run

Start the daemon in the foreground:

```bash
./build/processpilot
```

Or in the background with a log file:

```bash
./build/processpilot --daemon --log processpilot.log
```

In another terminal:

```bash
./build/processpilot-cli start configs/demo.service
./build/processpilot-cli status
./build/processpilot-cli restart demo
./build/processpilot-cli stop demo
```

Example `status` output:

```text
NAME            STATE       PID      MEMORY      RESTARTS  LAST EXIT
db              running     4121     1024 kB     0         -
web             running     4188     2048 kB     1         signal=15
```

To stop all services and the daemon:

```bash
./build/processpilot-cli shutdown
```

### CLI commands

| Command | Description |
| --- | --- |
| `start <file\|name>` | Load a service file (or use an already-loaded name) and start it, plus anything it requires |
| `stop <name>` | Stop a service |
| `restart <name>` | Stop and start a service |
| `status [name]` | Show all services, or one |
| `shutdown` | Stop all services and the daemon |

### Service states

| State | Meaning |
| --- | --- |
| `running` | Process is alive |
| `stopped` | Loaded but not started, or stopped with `stop` |
| `restarting` | Exited; will be restarted after `RestartSec` |
| `exited` | Exited with status 0 and the policy says not to restart |
| `failed` | Exited with an error and won't be restarted (or hit the start limit) |

### Control socket

The daemon listens on the first of:

1. `$PROCESSPILOT_SOCKET`
2. `$XDG_RUNTIME_DIR/processpilot.sock`
3. `/tmp/processpilot-<uid>.sock`

Both the daemon and the CLI accept `--socket PATH` to override this. The
socket is created with mode `0600`, and the daemon also checks that each
client runs as the same user. Starting a second daemon on the same socket is
refused.

## Service Files

`configs/demo.service`:

```ini
[Service]
Name=demo
Command=/bin/sleep 60
Restart=always
```

All supported keys:

| Key | Required | Default | Description |
| --- | --- | --- | --- |
| `Name` | yes | | Unique name, `[A-Za-z0-9._-]`, up to 64 characters |
| `Command` | yes | | Run with `/bin/sh -c` |
| `Restart` | no | `no` | `no`, `on-failure` or `always` |
| `RestartSec` | no | `1` | Delay before restarting (0-3600) |
| `StopTimeoutSec` | no | `5` | Wait this long after `SIGTERM` before `SIGKILL` (0-60) |
| `Requires` | no | | Space- or comma-separated names of services to start first |
| `WorkingDirectory` | no | daemon's cwd | Relative paths are resolved against the service file's directory |

A service's dependencies must be loaded (started once from their file)
before it can be started. After that, `start <name>` starts them
automatically.

## Tests

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Technologies

- C++17
- Linux/POSIX APIs (`fork`, `exec`, `setpgid`, `waitpid`, `sigaction`, `poll`)
- CMake / CTest
- Unix Domain Sockets
- `/proc` filesystem

## Author

**Shanu Shivam**

## Note

This is an educational lightweight service manager. It is not intended to replace
production service managers such as systemd.
