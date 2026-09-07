# Third-party notices

## Switch-OC-Suite — GPLv2

https://github.com/hanai3Bi/Switch-OC-Suite

The battery charge-limit mechanism (pausing and resuming charging through PSM
based on the reported charge percentage) originates here. This project is a
standalone re-implementation of that feature and nothing else.

## Horizon-OC — GPLv2

https://github.com/Horizon-OC/Horizon-OC (tag `2.3.1`)

`sysmodule/src/battery.{c,h}` is a trimmed-down version of
`Source/hoc-clk/sysmodule/src/pwr/battery.{cpp,h}`. Only the charge info
struct, the charging flag and enable/disable charging were kept.

Copyright (c) Souldbminer, Lightos_ and Horizon OC Contributors.

## sys-clk — "THE BEER-WARE LICENSE" (Revision 42)

https://github.com/retronx-team/sys-clk

`sysmodule/src/ipc_server.{c,h}` is p-sam / natinusala / m4xw's minimal HIPC
server, vendored via Horizon-OC. The only change is that `ipcServerProcess()`
takes a timeout so a single-threaded sysmodule can serve IPC and run a periodic
tick without a second thread. Notice retained in the files.

## libultrahand — GPLv2 (libultra under CC-BY-4.0)

https://github.com/ppkantorski/libultrahand

Used for the overlay UI. Vendored into `overlay/lib/libultrahand` by
`scripts/vendor-libultrahand.sh` rather than added as a submodule.

Copyright (c) 2023-2026 ppkantorski.

## ovl-sysmodules — GPLv2

https://github.com/ppkantorski/ovl-sysmodules

Not used as code, but its `toolbox.json` graceful-shutdown contract
(`shutdown_service` / `shutdown_cmd`) is what this sysmodule implements so it
can be stopped cleanly at runtime.
