# ChargeCap

A lightweight background sysmodule and [libultrahand](https://github.com/ppkantorski/libultrahand) Tesla overlay for the Nintendo Switch that stops charging once the battery reaches a configurable threshold (50%–99%).

Useful if you keep your Switch docked or plugged in for long periods and want to avoid holding the battery at peak voltage.

---

## Features

- **Small memory footprint**: Pure C sysmodule running a single-thread event loop with on-demand raw FS access (~50–60 KB resident memory).
- **Modern overlay**: Built with `libultrahand` featuring anti-aliased UI, a 50%–99% slider, and an ON/OFF toggle (defaults to OFF on fresh install).
- **Minimal live status**: Displays only battery percentage and charge state.
- **Self-healing**: The charge cutoff lives in the PMIC, not in the sysmodule, so it survives the sysmodule being killed. On every startup the module checks for a cutoff left behind by a previous run and clears it, and while running it re-asserts the limit from the hardware state rather than from its own bookkeeping (Horizon re-arms charging on its own across sleep and `psm` restarts).
- **Toolbox / ovl-sysmodules support**: The sysmodule can be enabled/disabled via ovl-sysmodules (requires reboot to take effect).
- **Safe unavailable state**: If the sysmodule is disabled, the overlay still opens instantly and shows a stub with a clear warning instead of the settings UI. The module's liveness is checked with a fail-fast pm:dmnt process probe (the same approach ovl-sysmodules and KeyX use), and the module's service is never contacted while it is down, so neither the overlay nor the Ultrahand menu that hosts it can hang.

---

## Important Warnings & Disclaimers

### 1. Use at Your Own Risk
This software communicates directly with Horizon OS's power management service (`psm`) and hardware PMIC registers. It is provided as-is without any warranty.

### 2. LLM / Vibe-Coded
This project was vibe-coded with large language model assistance. The charging limit logic is based on proven implementations from Switch-OC-Suite, but the standalone architecture, build scripts, and overlay interface were written with LLM help.

### 3. Battery Fuel Gauge Desync
Keeping a lithium-ion battery capped at a fixed percentage over long periods means the fuel gauge rarely sees a full 100% top-off or full discharge calibration point. Over time, Horizon's battery percentage reading might drift or desync.

**How to recalibrate if this happens:**
- Turn off the charge limit, charge the console to 100%, leave it on the charger for an extra hour, then play until the battery drops down to ~5–10%.
- Alternatively, boot Hekate and run CTCaer's [battery_desync_fix_nx](https://github.com/CTCaer/battery_desync_fix_nx) tool.

---

## Sleep Mode Behavior: Shallow vs Deep Sleep

The Nintendo Switch handles sleep in two distinct states:

- **Shallow Sleep (Screen Off / Active Background Tasks / eShop Downloads)**:
  The CPU cores remain active at low frequencies. The sysmodule keeps polling in the background (every 5 seconds) and disables charging on the first tick after the battery reaches your configured limit.

- **Deep Sleep (SC7 Suspend-to-RAM / Extended Sleep)**:
  The CPU cores are fully powered down and all background sysmodule execution is halted by Horizon OS:
  - If the console enters sleep **after** hitting the limit, charging remains disabled in the PMIC hardware.
  - If the console enters sleep **while still charging below the limit**, hardware charging continues. The instant the console wakes up, the sysmodule resumes execution and immediately cuts off charging if the limit has been reached.

---

## Upgrading from charge-limit-NX

ChargeCap is the renamed continuation of **charge-limit-NX**. To upgrade,
replace `atmosphere/contents/42000000000000C0/` and add
`switch/.overlays/ChargeCap.ovl` (removing the old
`switch/.overlays/charge-limit-NX.ovl`), then reboot once. The title id is
unchanged, and nothing else user-facing keeps the old name.

Your settings are carried over automatically: until
`sdmc:/config/chargecap/config.ini` exists, both the sysmodule and the overlay
read the old `sdmc:/config/charge-limit-NX/config.ini`. The new file is
created the first time you save settings from the overlay; after that the old
`/config/charge-limit-NX` folder can be deleted.

---

## Installation

1. Grab the latest `ChargeCap.zip` from [Releases](../../releases) or build it from source.
2. Extract the zip to the root of your SD card:
   - `atmosphere/contents/42000000000000C0/`
   - `switch/.overlays/ChargeCap.ovl`
3. **First-time install note:** If you transfer the files over MTP (DBI / nxmtp) or FTP while the console is running, **reboot your Switch once** so Atmosphere's process manager launches the new sysmodule. The config file will be created automatically on first launch.
4. Open the Tesla overlay menu (`L + D-Pad Down + R3`), choose **ChargeCap**, set your desired percentage, and toggle it ON.

---

## Configuration

Settings are saved at `sdmc:/config/chargecap/config.ini`:

```ini
[ChargeCap]
enabled = 0
limit = 80
```

- `enabled`: `0` = disabled (normal charging to 100%), `1` = limit active
- `limit`: target charge percentage (`50` to `99`)

The 5 second poll interval (`CHARGECAP_POLL_NS` in `common/include/chargecap.h`) is
a compile-time constant, not a setting.

---

## Building from Source

Requires [devkitPro](https://devkitpro.org) with `devkitA64`, `libnx`, and Switch portlibs (`switch-curl`, `switch-zlib`, `switch-mbedtls`). Uses libultrahand v2.4.3.

```bash
make zip
```

The output zip will be generated in the root directory.

---

## Credits & Provenance

- Battery limit PSM implementation lifted from [Switch-OC-Suite](https://github.com/hanai3Bi/Switch-OC-Suite) by **hanai3Bi** and **KazushiMe** (GPLv2).
- IPC server and background event loop architecture based on [sys-clk](https://github.com/retronx-team/sys-clk) by **p-sam**, **natinusala**, and **m4x** (GPLv2 / Beerware).
- Overlay powered by [libultrahand](https://github.com/ppkantorski/libultrahand) by **ppkantorski** and [libtesla](https://github.com/WerWolv/libtesla) by **WerWolv**.
- PSM reverse engineering research by **masagrator** ([ReverseNX-RT](https://github.com/masagrator/ReverseNX-RT)) and **CTCaer** ([Hekate](https://github.com/CTCaer/hekate)).

---

## License

GPLv2. See [LICENSE](LICENSE) for details.
