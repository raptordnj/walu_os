# WaluOS System Blueprint

Version: 0.1  
Date: 2026-02-26  
Audience: kernel/userland implementers, security reviewers, release engineers

## 1. EXECUTIVE SUMMARY
WaluOS is a Unix-like operating system and terminal userland designed to deliver a rich Ubuntu-like CLI experience with strict security defaults and realistic implementation scope.

Target use:
- Practical server/lab/developer system with predictable CLI behavior.
- Educationally inspectable internals (small core, clear APIs).

Scope boundaries:
- In scope (MVP): boot chain, init/service manager, shell UX, FHS layout, users/groups/permissions, sudo/audit, storage stack with safety gates, udev-like device handling, USB automount policy, baseline networking, 80-100 core commands, man/help.
- Deferred: full desktop stack, complete GNU/Linux command parity, advanced storage orchestration (LVM/RAID) in initial release.

MVP vs full feature set:
- MVP: secure multi-user terminal environment and day-to-day administration toolkit.
- Full: larger compatibility surface, package ecosystem growth, advanced hardening and optional custom kernel evolution.

## 2. ARCHITECTURE OVERVIEW (HIGH-LEVEL)
### 2.1 Kernel vs userland boundaries
Kernel provides:
- Syscall ABI and process/thread scheduling.
- Virtual memory management.
- VFS and filesystem drivers.
- Block/char device I/O.
- IPC primitives and signal delivery.
- Networking stack and socket layer.
- Device hotplug event emission.

Userland provides:
- `walud` (PID 1 init/service orchestration).
- `authd` (PAM-like auth broker).
- `udevd` + `automountd` (device node and media policy).
- `logd` (journal/syslog collection and rotation handoff).
- `netd` (persistent + dynamic network config).
- shell + core command suite + admin tools.

### 2.2 Process model, scheduling, memory, IPC, signals
Process model:
- POSIX-like `fork/exec/wait`.
- process groups/sessions for terminal job control.
- controlling TTY semantics for interactive shells.

Scheduling:
- preemptive scheduler (CFS-like fair scheduling).
- `nice` range `-20..19`.
- realtime classes deferred to phase 3.

Memory model:
- per-process virtual address spaces.
- copy-on-write `fork`.
- demand paging and page cache.
- `mmap` for file-backed and anonymous memory.
- hardened defaults: ASLR + NX + stack canaries in userland.

IPC:
- pipes/FIFOs.
- Unix domain sockets.
- shared memory (`mmap` + file descriptor backing).
- signal events and `poll/epoll` style readiness.

Signals:
- minimum POSIX set: `SIGINT SIGTERM SIGKILL SIGCHLD SIGHUP SIGSTOP SIGCONT SIGUSR1 SIGUSR2`.

### 2.3 Filesystem + VFS approach, device model, drivers plan
VFS core objects:
- `vnode` (`inode` abstraction).
- `dentry` (name cache).
- `file` (open file state).
- `superblock` (mounted filesystem instance).

VFS operation table sketch:
```c
typedef struct vfs_ops {
  int (*lookup)(struct vnode *dir, const char *name, struct vnode **out);
  int (*create)(struct vnode *dir, const char *name, mode_t mode, struct vnode **out);
  int (*unlink)(struct vnode *dir, const char *name);
  int (*read)(struct file *f, void *buf, size_t len, off_t *off);
  int (*write)(struct file *f, const void *buf, size_t len, off_t *off);
  int (*readdir)(struct file *f, struct dirent *out, size_t max);
  int (*chmod)(struct vnode *n, mode_t mode);
  int (*chown)(struct vnode *n, uid_t uid, gid_t gid);
} vfs_ops_t;
```

Device model:
- block devices expose queue + geometry + hotplug metadata.
- char devices expose stream/message semantics.
- kernel emits netlink-like uevents (`add`, `remove`, `change`).

Drivers plan:
- MVP drivers: AHCI/NVMe (one stable path), USB mass storage, PS/2 keyboard, basic framebuffer/VGA, virtio-net.
- defer broad hardware matrix to phase 3.

### 2.4 Boot process and init plan
Boot flow:
1. UEFI/BIOS -> bootloader.
2. bootloader loads kernel + initramfs + cmdline.
3. kernel initializes MMU/interrupts/devices and mounts initramfs.
4. `/init` mounts root fs, checks integrity, performs `switch_root`.
5. `walud` (PID 1) starts `basic.target`, then `multi-user.target`.

Init states:
- `early-init`: mount pseudo filesystems and create `/run`.
- `local-fs`: root + configured mounts.
- `network-pre`: device enumeration.
- `multi-user`: login and system services.

### 2.5 Input, TTY, and text encoding architecture
Input pipeline:
1. keyboard driver emits raw scancodes (PS/2 set 1 and USB HID usage pages).
2. kernel translates to internal keycodes (`KEY_A`, `KEY_ENTER`, `KEY_F1`, ...).
3. keymap layer resolves keycode + modifiers to keysym/Unicode code point.
4. TTY layer emits UTF-8 byte stream to foreground process.
5. line discipline handles canonical mode, echo, erase, signal keys (`Ctrl-C`, `Ctrl-Z`).

Output pipeline:
1. process writes UTF-8 bytes to PTY/TTY.
2. terminal parser decodes UTF-8 + ANSI/VT escape sequences.
3. terminal renderer updates screen buffer and attributes.

Feasibility note:
- full Unicode display is not realistic on legacy VGA text mode.
- MVP keeps VGA for early boot and serial logs, then switches to framebuffer terminal for UTF-8 and ANSI fidelity.

## 3. FILESYSTEM & DIRECTORY LAYOUT (FHS-LIKE)
Canonical tree:
```text
/
|-- bin
|-- sbin
|-- boot
|-- dev
|-- etc
|-- home
|-- lib
|-- lib64
|-- media
|-- mnt
|-- opt
|-- proc
|-- root
|-- run
|-- srv
|-- sys
|-- tmp
|-- usr
|   |-- bin
|   |-- sbin
|   |-- lib
|   |-- share
|   `-- local
`-- var
    |-- cache
    |-- lib
    |-- log
    |-- spool
    `-- tmp
```

Purpose highlights:
- `/etc`: host-specific config only.
- `/usr`: read-mostly programs/data.
- `/var`: mutable persistent data.
- `/run`: volatile runtime state (tmpfs).
- `/tmp`: world-writable temporary storage with sticky bit.
- `/proc` and `/sys`: virtual kernel state views.

Config conventions:
- primary config: `/etc/<component>.conf`.
- directory overrides: `/etc/<component>.d/*.conf`.
- vendor defaults: `/usr/lib/<component>.d/*.conf`.
- precedence: vendor < primary < drop-ins by lexical order.

Log layout:
- structured journal: `/var/log/journal/<machine-id>/`.
- compatibility logs: `/var/log/messages`, `/var/log/auth.log`, `/var/log/kern.log`, `/var/log/sudo.log`.
- rotation state: `/var/lib/logrotate/status`.

## 4. USERS, GROUPS, PERMISSIONS, AUTH
### 4.1 Account database formats
`/etc/passwd` format:
```text
name:x:uid:gid:gecos:home:shell
```

`/etc/shadow` format:
```text
name:hash:lastchg:min:max:warn:inactive:expire:reserved
```

`/etc/group` format:
```text
name:x:gid:user1,user2,user3
```

UID/GID policy:
- root: `0`.
- system accounts: `1-999`.
- human users: `1000+`.
- locked shell for service users: `/usr/sbin/nologin`.

### 4.2 Permissions and ownership
- mode bits: owner/group/other with `rwx`.
- special bits: setuid, setgid, sticky.
- default file mode from process umask.
- default umask: `022`, hardened profile `027`.
- ACL support deferred to milestone 2.

Permission check pseudo-code:
```c
bool may_access(inode_t *n, cred_t *c, int req) {
  if (c->euid == 0) return true; // root short-circuit for DAC
  mode_t bits = select_bits(n->mode, n->uid, n->gid, c);
  return (bits & req) == req;
}
```

### 4.3 Authentication flow (login, su, sudo)
Login flow:
1. `agetty` spawns `login`.
2. `login` sends auth request to `authd`.
3. `authd` evaluates `/etc/pam.d/login`.
4. on success, session setup (`/etc/profile`, limits, lastlog).
5. audit event written.

`su`:
- default requires target user password.
- root may switch without password.
- policy module may allow wheel-group controls.

`sudo`:
- policy from `/etc/sudoers` and `/etc/sudoers.d/*`.
- timestamp cache with configurable timeout.
- environment sanitized and secure path enforced.

Password hashing policy:
- MVP hash: yescrypt.
- optional: argon2id in phase 2.
- min length 12 by default.
- account lock by setting hash to `!` or `*`.

## 5. PRIVILEGE MODEL: ROOT/SUDO/CAPABILITIES
Root model:
- UID 0 retains full administrative power.
- remote root login disabled by default.
- operational recommendation: admin via sudo, not direct root shell.

Sudo policy files:
- `/etc/sudoers`.
- `/etc/sudoers.d/*.conf`.

Sudo security defaults:
- `env_reset` enabled.
- `secure_path` fixed path list.
- deny dangerous env vars (`LD_PRELOAD`, `LD_LIBRARY_PATH`, etc.).
- optional `requiretty` policy profile.

Mandatory audit fields for privileged exec:
- actor UID/name.
- target UID/name.
- timestamp UTC.
- tty/pty.
- current working directory.
- command path + normalized args.
- exit status + runtime.

Audit log format (JSON line):
```json
{"ts":"2026-02-26T14:00:03Z","actor":"alice","target":"root","tty":"/dev/pts/1","cwd":"/home/alice","cmd":"/usr/bin/mount","args":["/dev/sdb1","/mnt"],"exit":0,"dur_ms":102}
```

Linux-like capabilities (optional milestone):
- support `permitted`, `effective`, `inheritable`, `ambient`, `bounding`.
- reduce root requirements for network bind, raw sockets, and selected device operations.

## 6. PROCESS & SERVICE MANAGEMENT
Init approach:
- custom `walud`, systemd-like dependency graph, smaller feature surface.
- explicit unit state machine: `inactive -> activating -> active -> failed`.

Unit types:
- `service`, `target`, `mount`, `timer`.

Unit file format:
```ini
[Unit]
Description=...
After=network.target
Requires=network.target

[Service]
Type=simple
ExecStart=/usr/sbin/exampled -f
User=example
Group=example
Restart=on-failure
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

Dependency semantics:
- `Requires`: hard dependency.
- `Wants`: soft dependency.
- `After/Before`: ordering only.

Target concept:
- `rescue.target` (single-user recovery).
- `multi-user.target` (normal terminal operation).
- `shutdown.target`.

Logging architecture:
- `logd` collects kernel + service stdout/stderr + syslog messages.
- persistent journal database in `/var/log/journal`.
- optional text forwarding to compatibility log files.
- rotation via size/time policy and retention caps.

## 7. TERMINAL UX (UBUNTU-LIKE)
Shell choice:
- `/bin/sh`: POSIX shell.
- default interactive shell: Bash-compatible (`bash` initially).

Required shell features:
- prompt theming with colors and exit status.
- command history with timestamps.
- job control (`bg`, `fg`, `jobs`).
- redirection, pipes, command substitution, globbing.
- aliases, shell functions.
- programmable tab completion.

Prompt baseline:
```sh
PS1='\[\e[01;32m\]\u@\h\[\e[00m\]:\[\e[01;34m\]\w\[\e[00m\]\$ '
```

Terminal assumptions:
- ANSI/VT100 compatible emulator.
- UTF-8 default locale (`C.UTF-8` fallback).
- 256-color support with graceful fallback.

Docs/help:
- all base commands include `--help` and man page.
- man page sections: `NAME`, `SYNOPSIS`, `DESCRIPTION`, `OPTIONS`, `EXAMPLES`, `SAFETY`.

### 7.1 Full keyboard support plan
Required key classes:
- alphanumeric and punctuation keys.
- modifiers: Shift, Ctrl, Alt, AltGr, Meta/Super.
- lock keys: CapsLock, NumLock, ScrollLock.
- editing/navigation: Insert/Delete/Home/End/PageUp/PageDown/Arrows.
- function keys: F1-F12 (F13+ optional phase 2).
- keypad with NumLock-aware mappings.

Layout model:
- default layout configured by `/etc/vconsole.conf`.
- runtime keymap switching via `kbdctl set-layout <layout>`.
- compose/dead-key support for accented and international input.

Kernel/userland boundary:
- kernel owns scancode ingestion, repeat timing, key state.
- userland owns layout packs, compose tables, locale policy.

### 7.2 ANSI/VT control sequence support
MVP parser support:
- C0 controls (`\n`, `\r`, `\b`, `\t`, BEL, ESC).
- CSI cursor movement (`A B C D H f`).
- erase commands (`J`, `K`).
- SGR text attributes/colors (`0`, `1`, `4`, `7`, `30-37`, `40-47`, `90-97`, `100-107`).

Phase 2:
- DEC private modes subset.
- alternate screen buffer.
- bracketed paste mode.
- mouse reporting optional.

### 7.3 ASCII and Unicode support scope
Encoding rules:
- TTY and PTY streams are UTF-8 by default.
- printable ASCII (`0x20..0x7E`) is strict subset and must remain exact.
- invalid UTF-8 is replaced with U+FFFD in display path and logged in debug mode.

Display support milestones:
- MVP: full ASCII input/output, UTF-8 pass-through, BMP subset rendering in framebuffer terminal.
- Phase 2: wide-character width handling (CJK, combining marks basic support).
- Phase 3: full Unicode line editing expectations (grapheme cluster aware cursor movement).

Non-goal in MVP:
- guaranteeing perfect rendering of every Unicode grapheme cluster in VGA text mode.

## 8. DISK & STORAGE STACK
### 8.1 Block and partition model
Block device identity:
```c
typedef struct {
  char name[32];       // sda, nvme0n1
  uint64_t size_bytes;
  uint32_t logical_bs;
  uint32_t physical_bs;
  bool removable;
  bool read_only;
} blkdev_info_t;
```

Partition support:
- GPT read/write (primary).
- MBR read/write (legacy).

Optional advanced layers:
- LVM in phase 2.
- software RAID in phase 3.

### 8.2 Filesystem support and tools
MVP filesystem drivers:
- ext4 (rw).
- vfat (rw).
- iso9660 (ro).

Commands:
- inspect: `lsblk`, `blkid`.
- partition: `fdisk`/`parted` equivalent.
- create/check: `mkfs.*`, `fsck.*`.
- usage: `df`, `du`.
- mount control: `mount`, `umount`.

### 8.3 Safety requirements (mandatory)
No destructive default actions.

Rules:
- all destructive storage commands require `--force`.
- interactive typed confirmation required after `--force`.
- optional `--dry-run` for partitioning and formatting.
- unknown removable media mounted read-only by default.
- commands refuse mounted targets unless explicit override and safety checks pass.
- no automatic formatting under any circumstance.

Example guarded flow:
```text
$ mkfs.ext4 /dev/sdb1
ERROR: --force is required for destructive operation.

$ mkfs.ext4 --force /dev/sdb1
Type device path to confirm: /dev/sdb1
Type YES to continue: YES
Proceeding...
```

## 9. DEVICE MANAGEMENT & AUTO USB MOUNT
Event pipeline:
1. kernel emits uevent on hotplug/change.
2. `udevd` receives and enriches metadata.
3. rules assign naming/perms/tags in `/dev`.
4. tagged block devices trigger `automountd`.
5. `automountd` applies trust policy and mount options.

Rule engine fields:
- `SUBSYSTEM`, `DEVTYPE`, `ACTION`.
- `ID_BUS`, `ID_VENDOR_ID`, `ID_MODEL_ID`, `ID_SERIAL`.
- `ID_FS_TYPE`, `ID_FS_LABEL`, `ID_FS_UUID`.

Auto-mount policy:
- mount path: `/media/$USER/<LABEL_OR_UUID>`.
- defaults: `nosuid,nodev,noexec,relatime`.
- untrusted media mounted read-only first.
- duplicate labels receive suffix (`-2`, `-3`, ...).
- on unplug: lazy unmount fallback with audit event.

Pseudo-config:
```toml
[default]
mount_root="/media"
options=["nosuid","nodev","noexec","relatime"]
unknown_read_only=true
duplicate_suffix="-{n}"

[trusted]
uuid=["1234-ABCD"]
allow_exec=false
```

Sample rule:
```ini
SUBSYSTEM=="block", ENV{ID_BUS}=="usb", ENV{ID_FS_USAGE}=="filesystem", GROUP="plugdev", MODE="0660", TAG+="walu_automount"
```

## 10. SYSTEM INFO & HARDWARE INSPECTION TOOLS
Required commands:
- `uname`, `lscpu`, `lsusb`, `lspci`, `free`, `vmstat`, `dmesg`, `uptime`.

Keyboard/input inspection additions:
- `showkey` (raw scancode/keycode stream inspection).
- `kbdctl` (active layout, compose table, repeat rate).
- `locale`/`localectl` (encoding and locale visibility).

Virtual filesystem data model:
- `/proc`: process/runtime counters.
- `/sys`: device topology and attributes.

Key `/proc` entries:
- `/proc/cpuinfo`
- `/proc/meminfo`
- `/proc/uptime`
- `/proc/loadavg`
- `/proc/<pid>/stat`
- `/proc/<pid>/status`
- `/proc/net/dev`

Key `/sys` entries:
- `/sys/class/block/*`
- `/sys/class/net/*`
- `/sys/bus/usb/devices/*`
- `/sys/devices/system/cpu/*`

Tool data mapping:
- `free` parses `/proc/meminfo`.
- `vmstat` uses `/proc/stat` and `/proc/vmstat`.
- `lscpu` combines `/proc/cpuinfo` and `/sys` topology.
- `lsblk` combines `/sys/class/block` and filesystem signatures.

## 11. NETWORKING (BASELINE)
Model:
- kernel sockets and routing tables.
- `netd` applies static/DHCP config and manages link transitions.

Config format (`/etc/net/ifaces/<name>.toml`):
```toml
[link]
name="eth0"
mtu=1500

[ipv4]
mode="dhcp" # static|dhcp
address=""
prefix=24
gateway=""

[dns]
servers=["1.1.1.1","8.8.8.8"]
search=[]
```

Required tools:
- `ip` primary.
- `ifconfig` compatibility shim.
- `ping`, `traceroute`, `ss` (`netstat` compatibility mode).
- optional in MVP: lightweight `curl`/`wget`.

Optional security milestone:
- nftables-based firewall manager in phase 2.

## 12. CORE UNIX COMMANDS & COMPATIBILITY STRATEGY
Strategy:
- ship high-value subset first.
- maximize POSIX behavior for scripts.
- provide Linux/GNU compatibility where high impact.

Packaging model for commands:
- rescue/initramfs: BusyBox-like multicall binary (`wbox`).
- full install: separate executables for compatibility and independent updates.

MVP commands (80+ target):
- shell: `sh`, `bash`, `env`, `export`, `alias`, `type`.
- file ops: `ls`, `cat`, `cp`, `mv`, `rm`, `ln`, `mkdir`, `rmdir`, `touch`, `stat`, `chmod`, `chown`, `chgrp`, `pwd`, `readlink`, `realpath`.
- text/stream: `echo`, `printf`, `head`, `tail`, `tee`, `wc`, `cut`, `paste`, `tr`, `sort`, `uniq`, `grep`, `sed`, `awk`.
- search/transform: `find`, `xargs`, `diff`, `patch`.
- archive/compress: `tar`, `gzip`, `gunzip`, `xz`, `unxz`, `zip`, `unzip`.
- process: `ps`, `kill`, `pkill`, `killall`, `top`, `nice`, `renice`.
- system info: `uname`, `date`, `uptime`, `dmesg`, `free`, `vmstat`.
- storage/admin: `lsblk`, `blkid`, `df`, `du`, `mount`, `umount`, `fsck`, `mkfs.ext4`.
- identity/auth: `id`, `whoami`, `who`, `groups`, `passwd`, `useradd`, `usermod`, `userdel`, `groupadd`, `groupmod`, `groupdel`, `su`, `sudo`, `visudo`.
- services/logging: `wctl`, `service`, `journalctl`, `logread`.
- networking: `ip`, `ping`, `traceroute`, `ss`, `hostname`.
- docs: `man`, `less`.
- input/locale: `showkey`, `kbdctl`, `loadkeys`, `dumpkeys`, `locale`.

Phase 2:
- `rsync`, `ssh/scp/sftp`, `cron`, `at`, ACL tools, LVM/mdadm tools, full `curl/wget`, `tcpdump`, `strace`, `iconv`.

Phase 3:
- extended perf tools, container tooling, advanced security tools.

POSIX target:
- POSIX.1-2017 shell + core utilities behavior for primary script paths.

Intentionally non-POSIX:
- unit manager interface (`wctl`/`service`).
- journal model.
- Linux-like `/proc` and `/sys` extensions.

## 13. BUILT-IN TOOLS (HTOP/TOP, EDITOR, PAGER)
Top/htop-like tool design:
- refresh period default: 1 second.
- data from `/proc/stat`, `/proc/meminfo`, `/proc/<pid>/stat`, `/proc/<pid>/status`.
- sortable columns: CPU, memory, PID, runtime, IO wait proxy.
- interactive keys: `q` quit, `k` kill, `r` renice, `P` sort CPU, `M` sort memory, `/` filter.

Data structure:
```c
typedef struct {
  int pid;
  int ppid;
  uid_t uid;
  char state;
  double cpu_pct;
  double mem_pct;
  uint64_t rss_kib;
  char comm[64];
} top_row_t;
```

Editor (optional, phase 2 recommended):
- minimal nano-like `wedit`.
- open/save/search/line numbers.
- safe writes with temp file + atomic rename.

Pager/man:
- `wpager` less-like navigation/search.
- `man` parser for roff subset and preformatted cache.

## 14. PACKAGE MANAGEMENT (OPTIONAL BUT RECOMMENDED)
Decision:
- include minimal package manager in milestone 3.

Package format:
- `.wpk` archive (`tar.zst`) with:
  - `manifest.toml`
  - payload tree
  - lifecycle scripts
  - detached signature

Manifest example:
```toml
name="coreutils"
version="1.0.3"
arch="x86_64"
depends=["libc>=1.0","libwalu>=0.9"]
conflicts=[]
provides=["coreutils"]
```

Repository metadata:
- `index.json` + `index.sig`.
- Ed25519 signatures.
- trust roots in `/etc/wpkg/keys.d`.

Package operations:
- `wpkg update`
- `wpkg install`
- `wpkg remove`
- `wpkg upgrade`
- `wpkg verify`
- `wpkg search`

Dependency resolution:
- SAT-based solver (minimal feature set for MVP repository scale).
- transaction journal with rollback on failure.

## 15. CONFIGURATION & POLICY
Config layering:
- vendor defaults: `/usr/lib/walu/*.conf`.
- system overrides: `/etc/walu/*.conf`.
- runtime generated state: `/run/walu/*.state`.

Environment and locale:
- `/etc/environment`
- `/etc/profile`
- `/etc/profile.d/*.sh`
- `~/.profile`
- `~/.bashrc`
- `/etc/locale.conf`
- `/etc/localtime` and `/etc/timezone`

Security policies:
- default removable mount options: `nodev,nosuid,noexec`.
- password complexity and aging in `/etc/security`.
- sudo policy with explicit least-privilege command rules.
- update policy supports staged rollout and signature checks.

## 16. TESTING, VALIDATION, & HARDENING
Unit tests:
- parsers for passwd/shadow/group/sudoers/unit files.
- permission evaluation engine and umask handling.
- command option parsing and guard rails.

Integration tests:
- user lifecycle and auth flows.
- file ownership/permission operations.
- mount/umount/mkfs safety checks.
- sudo audits and service lifecycle.
- USB add/remove with automount policy.

Fuzzing targets:
- command-line parsers.
- partition/filesystem metadata parsers.
- archive extractors.
- rule parsers (`udev`, service units, sudoers subset).

Threat model summary:
- local privilege escalation.
- malicious USB media.
- network service exploitation.
- package supply-chain tampering.
- operator mistakes on destructive commands.

Mitigations checklist:
- least privilege service accounts.
- required audit logging for auth/privilege events.
- package signature verification.
- hardening flags and memory-safe coding where practical.
- explicit force + confirmation for destructive storage actions.
- subsystem fault counters and safe fallback paths (VGA fallback on framebuffer failure, queue overflow accounting).

## 17. IMPLEMENTATION PLAN & MILESTONES
Milestone 0: boot + init + shell + basic fs
- deliverables: bootable image, `walud` skeleton, shell login, FHS mount layout, `/proc` and `/sys`, baseline keyboard IRQ input.
- acceptance: boot in QEMU, interactive shell usable, ASCII input and command editing functional, logs collected.
- risks: early init ordering and hardware assumptions.

Milestone 1: users/groups/permissions + core commands
- deliverables: auth database, sudo/audit stack, permission enforcement, 80+ commands, UTF-8 TTY transport plumbing.
- acceptance: integration tests pass for auth and permission semantics, UTF-8 bytes preserved through PTY/shell path.
- risks: command compatibility and security regressions.

Milestone 2: device mgmt + auto-mount + monitoring tools
- deliverables: `udevd`, `automountd`, top-like monitor, storage policy enforcement, ANSI parser + framebuffer terminal path.
- acceptance: USB hotplug policy works, unknown media defaults to read-only, ANSI color/cursor control works in terminal apps.
- risks: race conditions in hotplug and mount path conflicts.

Milestone 3: networking + package manager + polish
- deliverables: `netd`, `ip/ping/ss` workflows, `wpkg`, hardening pass, docs finalization, advanced keymaps/compose and Unicode width handling.
- acceptance: signed package lifecycle works, network config supports DHCP + static, international keyboard layouts and common Unicode workflows validated.
- risks: dependency solver complexity and network edge cases.

## 18. APPENDICES
### A) Example configs
Files provided in repository:
- `docs/examples/etc/passwd`
- `docs/examples/etc/shadow`
- `docs/examples/etc/group`
- `docs/examples/etc/sudoers`
- `docs/examples/etc/sudoers.d/admin`
- `docs/examples/etc/vconsole.conf`
- `docs/examples/etc/locale.conf`
- `docs/examples/etc/input/layout.toml`
- `docs/examples/walu/units/sshd.service`
- `docs/examples/udev/rules.d/80-removable.rules`
- `docs/examples/walu/automount.toml`

### B) Example command help outputs
`mount`:
```text
Usage: mount [OPTIONS] DEVICE DIR
  -o, --options OPTS   mount options
  -r, --read-only      mount read-only
      --dry-run        print action without executing
Safety: unknown removable media default to read-only policy.
```

`mkfs.ext4`:
```text
Usage: mkfs.ext4 [OPTIONS] DEVICE
      --force          required for destructive operation
      --dry-run        simulate only
Safety: requires --force and interactive confirmation.
```

`useradd`:
```text
Usage: useradd [OPTIONS] LOGIN
  -m, --create-home
  -u, --uid UID
  -g, --gid GROUP
  -s, --shell SHELL
```

`sudo`:
```text
Usage: sudo [OPTIONS] COMMAND [ARG]...
  -u, --user USER
  -l, --list
  -k
```

`wctl`:
```text
Usage: wctl <command> [unit]
Commands: start stop restart status enable disable list
```

`top`:
```text
Usage: top [OPTIONS]
  -d, --delay SEC
  -u, --user USER
Interactive: q quit, k kill, r renice, P cpu sort, M mem sort, / filter
```

`kbdctl`:
```text
Usage: kbdctl <command> [args]
Commands:
  show-layout
  set-layout <layout>
  show-repeat
  set-repeat <delay_ms> <rate_hz>
```

### C) File and API interface sketches
See:
- `docs/interfaces/syscalls.h`
- `docs/interfaces/procfs.md`
- `docs/interfaces/input_tty.md`

Service control socket example (`/run/walud.sock`):
```json
{"id":1,"method":"unit.start","params":{"name":"sshd.service"}}
{"id":1,"ok":true}
```

Storage guard helper:
```c
typedef struct {
  bool dry_run;
  bool force;
  bool confirmed;
  char confirm_token[64];
} destructive_guard_t;

int guard_require_confirmation(const char *device, destructive_guard_t *g);
```
