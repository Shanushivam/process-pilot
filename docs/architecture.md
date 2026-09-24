# ProcessPilot Architecture

ProcessPilot is divided into small components:

- **Daemon**: owns the loaded services, handles CLI commands and runs the
  supervision loop.
- **Process Manager**: starts one child in its own process group, reaps it
  without blocking, and stops it (`SIGTERM`, then `SIGKILL` on timeout).
- **Service Config**: loads and validates `.service` files.
- **Config Parser**: reads simple `key=value` files.
- **Resource Monitor**: reads process memory usage from `/proc`.
- **Dependency Graph**: stores `Requires=` relationships, computes start order
  and detects cycles.
- **Unix Socket IPC**: connects the CLI to the daemon.
- **Logger**: writes timestamped events to stdout or a log file.
- **CLI**: provides user commands.

## Flow

```text
User
 |
 v
processpilot-cli
 |
 v
Unix Domain Socket (mode 0600, same-user check)
 |
 v
ProcessPilot Daemon
 |
 +--> Service Config --> Config Parser
 +--> Dependency Graph (start order, cycle checks)
 +--> Process Manager --> Linux process group
 +--> Resource Monitor --> /proc
 +--> Logger
```

## Supervision loop

The daemon is single-threaded. Each iteration of its loop:

1. Waits up to 200 ms for a CLI request (`poll`) and answers it.
2. Calls `waitpid(..., WNOHANG)` for every running service. A service that
   exited is marked `exited`/`failed`, or scheduled for restart according to
   its `Restart=` policy.
3. Starts any service whose restart delay has passed. A service started 5
   times within 60 seconds is marked `failed` instead of restarting again.

`SIGINT`/`SIGTERM` set a flag that ends the loop. The daemon then stops every
service, dependents first, and removes its socket.

Polling instead of a `SIGCHLD` handler keeps all process state in one thread.
The cost is a restart delay of up to 200 ms.
