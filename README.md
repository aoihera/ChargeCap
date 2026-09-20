# ChargeCap

A lightweight background sysmodule and [libultrahand](https://github.com/ppkantorski/libultrahand) overlay for the Nintendo Switch that stops charging once the battery reaches a configurable threshold (50% - 99%).

Useful if you keep your Switch docked or plugged in for long periods and want to avoid holding the battery at peak voltage.

---

## Features

- **Standalone & small footprint**: Contains only what is necessary to limit charging. Uses just ~70 KB resident memory.
- **Minimal dependencies**: Requires [Tesla Menu](https://github.com/WerWolv/Tesla-Menu) or [Ultrahand](https://github.com/ppkantorski/Ultrahand-Overlay) for the overlay. The overlay provides a convenient UI but is optional; the sysmodule works independently with just a config file. Only requires [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).
- **Smart sleep alarm support (Optional)**: Can schedule hardware RTC wake alarms to periodically check charge levels while the console is asleep with the screen completely off, stopping charging when the set battery limit is reached.

<img width="1280" height="720" alt="2026092020005000-57B4628D2267231D57E0FC1078C0596D" src="https://github.com/user-attachments/assets/e03fe2a4-4a90-4585-a544-93a0ca8bc48c" />

---

## Important Warnings & Disclaimers

### ⚠️ 1. Use at Your Own Risk
This software communicates directly with Horizon OS's power management service (`psm`) and hardware PMIC registers. It is provided as-is without any warranty.

### ⚠️ 2. LLM / Vibe-Coded
This project was vibe-coded with large language model assistance. The charging limit logic is based on proven implementations from [Switch-OC-Suite](https://github.com/hanai3Bi/Switch-OC-Suite), and the architecture around it is based on [sys-clk](https://github.com/retronx-team/sys-clk), but the actual implementation and interface were written with LLM help.

### ⚠️ 3. Battery Fuel Gauge Desync
Keeping a lithium-ion battery capped at a fixed percentage over long periods means the fuel gauge rarely sees a full 100% top-off or full discharge calibration point. Over time, Horizon OS's battery percentage reading might drift or desync.

**How to recalibrate if this happens:**
- Turn off the charge limit, charge the console to 100%, leave it on the charger for an extra hour, then play until the battery drops down to ~5–10%.
- Alternatively, run the [battery_desync_fix_nx](https://github.com/CTCaer/battery_desync_fix_nx) tool.

---

## Sleep Mode Behavior: "Limit in Sleep Mode" Toggle

### 1. Limit in Sleep Mode: ON (Default)
- When entering sleeping AND charging AND below the limit, ChargeCap sets a periodic "alarm" (wake timer).
- System sleeps until the "alarm" goes off, then the system semi-wakes (half-awake / shallow sleep state, screen stays off) and ChargeCap checks the current battery charge, if still below the set limit, repeat.
- Once the charge limit is reached: all alarms are cleared, charging is stopped and system is allowed to enter sleep again.

### 2. Limit in Sleep Mode: OFF
- If the console enters sleep mode when at the limit, charging remains stopped as expected.
- If the console enters deep sleep while charging below the limit, hardware charging continues silently. As soon as you wake the console manually, ChargeCap immediately detects the battery level and cuts charging if the limit has been reached/surpassed.

---

## Installation

1. Grab the latest `ChargeCap.zip` from Releases or build it from source.
2. Extract the zip to the root of your SD card:
   - `atmosphere/contents/42000000000000C0/`
   - `switch/.overlays/ChargeCap.ovl`
3. **First-time install note:** If you transfer the files over MTP or FTP while the console is running, **reboot your Switch once** so Atmosphère launches the sysmodule.
4. Open the Tesla/Ultrahand overlay menu (normally `ZL + ZR + D-Pad Down` or `L + D-Pad Down + R3`), select **ChargeCap**, adjust your limit, and toggle it ON.

---

## Configuration

Settings are saved at `sdmc:/config/chargecap/config.ini`:

```ini
[ChargeCap]
; 0 = off (no limit is applied at all), 1 = on
enabled=0
; stop charging at this percentage (50-99)
limit=80
; 0 = off in sleep mode, 1 = periodic background wake to apply limit while asleep
sleep_limit=1
```

- `enabled`: `0` = disabled (normal charging to 100%), `1` = charge limit active. Default: `0` (OFF).
- `limit`: target charge percentage (`50` to `99`). Default: `80`.
- `sleep_limit`: `0` = charge limiting while awake only, `1` = periodic background wake with screen off to apply limit while asleep. Default: `1` (ON).

---

## Building from Source

Requires [devkitPro](https://devkitpro.org) with `devkitA64`, `libnx`, and Switch portlibs (`switch-curl`, `switch-zlib`, `switch-mbedtls`).

```bash
make zip
```

The output zip will be generated in the root directory.

---

## Credits & Provenance

- Battery limit PSM implementation based on [Switch-OC-Suite](https://github.com/hanai3Bi/Switch-OC-Suite) by **hanai3Bi** and **KazushiMe** (GPLv2).
- IPC server and background event loop architecture based on [sys-clk](https://github.com/retronx-team/sys-clk) by **p-sam**, **natinusala**, and **m4x** (GPLv2 / Beerware).
- Overlay powered by [libultrahand](https://github.com/ppkantorski/libultrahand) by **ppkantorski**.
- PSM reverse engineering research by **masagrator** ([ReverseNX-RT](https://github.com/masagrator/ReverseNX-RT)) and **CTCaer** ([Hekate](https://github.com/CTCaer/hekate)).

---

## License

GPLv2 / MIT. See [LICENSE](LICENSE) for details.
