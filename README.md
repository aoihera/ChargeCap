# ChargeCap

A lightweight background sysmodule and [libultrahand](https://github.com/ppkantorski/libultrahand) overlay for the Nintendo Switch that stops charging once the battery reaches a configurable threshold (50% - 99%).

Useful if you keep your Switch docked or plugged in for long periods and want to avoid holding the battery at peak voltage.

---

## Features

- **Standalone & Minimal footprint**: Contains only what is necessary to limit charging (single-thread event loop with on-demand raw FS access). Uses just ~60-70 KB resident memory.
- **Minimal dependencies**: Only requires Tesla menu or Ultrahand for the overlay. The overlay provides convenient UI but is optional, the sysmodule works independently with just a config file. The sysmodule doesn't require anything beyond [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).

---

## Important Warnings & Disclaimers

### ⚠️ 1. Use at Your Own Risk
This software communicates directly with Horizon OS's power management service (`psm`) and hardware PMIC registers. It is provided as-is without any warranty.

### ⚠️ 2. LLM / Vibe-Coded
This project was vibe-coded with large language model assistance. The charging limit logic is based on proven implementations from Switch-OC-Suite, and the architecture around it is based on sys-clk, but the actual implementation and interface were written with LLM help.

### ⚠️ 3. Battery Fuel Gauge Desync
Keeping a lithium-ion battery capped at a fixed percentage over long periods means the fuel gauge rarely sees a full 100% top-off or full discharge calibration point. Over time, Horizon OS's battery percentage reading might drift or desync.

**How to recalibrate if this happens:**
- Turn off the charge limit, charge the console to 100%, leave it on the charger for an extra hour, then play until the battery drops down to ~5–10%.
- Alternatively, run CTCaer's [battery_desync_fix_nx](https://github.com/CTCaer/battery_desync_fix_nx) tool.

---

## Sleep Mode Behavior: Shallow vs Deep Sleep

Horizon OS handles sleep in two distinct states:

- **Shallow Sleep (Screen Off / Active Background Tasks / eShop Downloads)**:
  The CPU cores remain active at low frequencies. The sysmodule continues polling in the background every second and will immediately disable charging the moment the battery reaches your configured limit.

- **Deep Sleep (SC7 Suspend-to-RAM / Extended Sleep)**:
  The CPU cores are fully powered down and all background sysmodule execution is halted by Horizon OS:
  - If the console enters sleep **after** hitting the limit, charging remains disabled in the PMIC hardware.
  - If the console enters sleep **while still charging below the limit**, hardware charging continues. The instant the console wakes up, the sysmodule resumes execution and immediately cuts off charging if the limit has been reached.

---

## Installation

1. Grab the latest `ChargeCap.zip` from [Releases](../../releases) or build it from source.
2. Extract the zip to the root of your SD card:
   - `atmosphere/contents/42000000000000C0/`
   - `switch/.overlays/ChargeCap.ovl`
3. **First-time install note:** If you transfer the files over MTP or FTP while the console is running, **reboot your Switch once** so Atmosphère's process manager launches the new sysmodule. The config file will be created automatically on first launch.
4. Open the Tesla/Ultrahand overlay menu (normally `ZL + ZR + D-Pad Down` or `L + D-Pad Down + R3`), choose **ChargeCap**, set your desired percentage, and toggle it ON.

---

## Configuration

Settings are saved at `sdmc:/config/ChargeCap/config.ini`:

```ini
[ChargeCap]
enabled = 0
limit = 80
```

- `enabled`: `0` = disabled (normal charging to 100%), `1` = limit active
- `limit`: target charge percentage (`50` to `99`)

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
