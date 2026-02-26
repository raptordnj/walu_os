# procfs and sysfs Interface Sketch

This file defines the first stable virtual filesystem endpoints needed by core tools.

## /proc required files
- `/proc/cpuinfo`
- `/proc/meminfo`
- `/proc/stat`
- `/proc/uptime`
- `/proc/loadavg`
- `/proc/cmdline`
- `/proc/version`
- `/proc/partitions`
- `/proc/mounts`

Per-process:
- `/proc/<pid>/stat`
- `/proc/<pid>/status`
- `/proc/<pid>/cmdline`
- `/proc/<pid>/fd/`

Network:
- `/proc/net/dev`
- `/proc/net/route`
- `/proc/net/tcp`
- `/proc/net/udp`

## /sys required directories
- `/sys/class/block/<dev>/`
- `/sys/class/net/<if>/`
- `/sys/bus/usb/devices/`
- `/sys/devices/system/cpu/`

## Example formats
`/proc/meminfo` (subset):
```text
MemTotal:       16364256 kB
MemFree:         2864120 kB
MemAvailable:    8421004 kB
Buffers:          188272 kB
Cached:          4677404 kB
```

`/proc/uptime`:
```text
12345.67 12001.24
```

`/proc/loadavg`:
```text
0.34 0.27 0.18 2/431 2210
```

## Tool mappings
- `free` -> `/proc/meminfo`
- `vmstat` -> `/proc/stat`, `/proc/vmstat`, `/proc/meminfo`
- `top` -> `/proc/stat`, `/proc/<pid>/*`
- `lsblk` -> `/sys/class/block`, `/proc/partitions`
- `ip/ss` -> `/proc/net/*` plus netlink APIs

