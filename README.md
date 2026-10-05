# Trucker

> 🚧 **Work in progress** — a full README is coming soon.

## About

This is an educational container runtime I started to better understand various Linux concepts, such as:

- **Namespaces**
- **cgroups**
- **Capabilities**

## ⚠️ Disclaimer

This project is **not intended for production use**. It's a solo learning project, built with my current level of knowledge and it has limitations due to it in terms of features, security and robustness compared to established runtimes such as `runc` or `crun`.

## Current state

- Runs rootless (no root needed on the host)
- All namespaces are set up: user, mount, PID, UTS, IPC, cgroup, network, time
- Each container runs in its own cgroup (systemd scope)
- The container has internet access (via `pasta`)

## Next steps

- **Capabilities**: drop them before running the container's program
- **Options**: configure the container from the command line, (cgroup, network, ...)
- **Code cleanup**: the code is not clean yet: memory management and error handling are still incomplete

## Requirements

- Linux with cgroup v2 and unprivileged user namespaces enabled
- A systemd user session (`systemd-run --user`)
- [`pasta`](https://passt.top/) (package `passt`)
- `gcc` and `make`

## Usage

Run these commands from the `trucker` directory:

```bash
make
export TRUCKER="$(pwd)"
export PATH="$TRUCKER:$PATH"
trucker deliver <environment> <program> [program args...]
```

`<environment>` is the directory used as the container's `/`.

## Coming soon

A detailed README covering:

- How the project works
- The design choices made
- Known limitations
- Mistakes I made along the way and how I fixed them
- What I learned during this project
