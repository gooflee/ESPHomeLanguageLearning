# Korean Sentence Reader (ESPHome — Seeed Studio reTerminal Sticky)

A **100% Offline**, pure-Hangul Korean Sentence Reader built with **ESPHome** for the **Seeed Studio reTerminal Sticky** (`3.97"` `800×480` monochrome e-ink display powered by ESP32-S3 with 8 MB Octal PSRAM and 32 MB Flash).

## Key Features

- **100% Offline Operation (No Wi-Fi Required):**
  - Runs completely standalone with no `wifi:` or `api:` blocks in [`sticky-korean.yaml`](sticky-korean.yaml) — boots directly into the Korean Sentence Reader and never reboots searching for Wi-Fi or Home Assistant.
- **Dynamic MicroSD Card Sentence Loading (`Korean_sentences.txt`):**
  - Reads `/Korean_sentences.txt` from the root of a FAT32-formatted MicroSD card inserted into the Sticky (`SPI2`, `CS = GPIO8`, `EN = GPIO10`).
  - Displays `[외장]` in the top header when reading from the MicroSD card.
  - Supports any number of UTF-8 Korean sentences (1 line = 1 sentence).
  - Automatically falls back to the built-in 1,000-sentence deck in [`include/default_sentences.h`](include/default_sentences.h) if no MicroSD card is inserted (displays `[내장]` in the header).
- **Zero-Font-Dependency Algorithmic Hangul Renderer (`U+AC00–U+D7A3`):**
  - [`include/hangul_canvas.h`](include/hangul_canvas.h) algorithmically decomposes and draws all **11,172 Korean Hangul syllables**, standalone Jamo (`U+3131–U+3163`), digits, and symbols directly onto the ESPHome display buffer.
  - No external `.ttf` font files are required, and **any new Korean sentence** you add to `Korean_sentences.txt` on the MicroSD card in the future will render every syllable without ever reflashing the device.
- **10-Minute Random Auto-Advance (`interval: 10min`):**
  - Automatically picks and displays a new random Korean sentence every 10 minutes (avoiding back-to-back repeats) and keeps a 30-sentence history stack.
- **Mark for Later Review (`Korean_review_sentences.txt`) & On-Sticky Review Mode (`★ 복습`):**
  - Pressing the middle **`AI` button (`GPIO 4`)** (or **double-pressing `DOWN`**) toggles `★ 복습` on the current sentence and saves your review list directly to `/Korean_review_sentences.txt` on the MicroSD card.
  - **Double-pressing `UP` (`GPIO 5`)** toggles between **Normal Mode (`전체 모드`)** and **Review Mode (`★ 복습`)**.

---

## Physical Button Mapping (Seeed Studio reTerminal Sticky)

| Physical Button | GPIO Pin | Action |
| :--- | :--- | :--- |
| **Single-press `DOWN`** | `GPIO 6` | Immediately pick and display a new random Korean sentence |
| **Single-press `UP`** | `GPIO 5` | Step backward to the previously displayed sentence (history stack) |
| **Single-press `AI / OK`** *(or **Double-press `DOWN`**)* | `GPIO 4` | Toggle **★ Marked for Review** (`☆ 복습 저장` / `★ 복습 완료`) and save to `/Korean_review_sentences.txt` on MicroSD |
| **Double-press `UP`** | `2x GPIO 5` | Toggle between **Normal Mode (`전체 모드`)** and **Review Mode (`★ 복습`)** |

---

## Hardware Pinout Summary (`sticky-korean.yaml`)

| Peripheral | GPIO | Notes |
| :--- | :--- | :--- |
| **Power Latch (Critical)** | `GPIO 45`, `GPIO 46` | Driven `HIGH` at `on_boot` (`priority: 600`) so the Sticky stays powered on battery |
| **Shared SPI2 Bus** | `GPIO 13` (CLK), `GPIO 14` (MOSI), `GPIO 12` (MISO) | Shared between SSD1677 ePaper display and MicroSD slot |
| **ePaper Display (`epaper_spi`)** | `CS=GPIO15`, `DC=GPIO16`, `RST=GPIO17`, `BUSY=GPIO18`, `PWR=GPIO47` | Uses ESPHome `model: seeed-reterminal-sticky` (`800×480`) |
| **MicroSD Card Slot** | `CS=GPIO8`, `PWR=GPIO10`, `DET=GPIO11` | Mounted dynamically via `esp_vfs_fat_sdspi_mount` and unmounted before display refresh |

---

## Project Structure

- [`sticky-korean.yaml`](sticky-korean.yaml) — Main offline ESPHome configuration for the Seeed Studio reTerminal Sticky.
- [`include/hangul_canvas.h`](include/hangul_canvas.h) — Pure C++ algorithmic Hangul syllable (`U+AC00–U+D7A3`), Jamo, digit, and symbol vector renderer + multi-line word wrapper for ESPHome `Display`.
- [`include/sd_sentences.h`](include/sd_sentences.h) — MicroSD FAT32 reader/writer (`Korean_sentences.txt` & `Korean_review_sentences.txt`), history/review state manager, and `800×480` UI layout renderer.
- [`include/default_sentences.h`](include/default_sentences.h) — Auto-generated built-in fallback sentence array synced from [`data/Korean_sentences.txt`](data/Korean_sentences.txt).
- [`data/Korean_sentences.txt`](data/Korean_sentences.txt) — Master Korean sentences file (1 sentence per line, UTF-8). Copy this file to the root of your MicroSD card as `Korean_sentences.txt`.
- [`Build-Firmware.ps1`](Build-Firmware.ps1) / [`build.cmd`](build.cmd) / [`flash.cmd`](flash.cmd) — Syncs [`data/Korean_sentences.txt`](data/Korean_sentences.txt) into [`include/default_sentences.h`](include/default_sentences.h) and compiles/flashes via ESPHome.

---

## Quick Start

### 1. Prepare the MicroSD Card
1. Format a MicroSD card (≤ 32 GB recommended) as **FAT32**.
2. Copy [`data/Korean_sentences.txt`](data/Korean_sentences.txt) to the root of the MicroSD card so its path on the card is `/Korean_sentences.txt` (UTF-8 encoding, 1 Korean sentence per line).
3. Insert the MicroSD card into the Seeed Studio reTerminal Sticky.

### 2. Install ESPHome (if not already installed)
```powershell
pip install esphome
```

### 3. Build & Flash via USB-C
Connect your Seeed Studio reTerminal Sticky to your computer via USB-C and run:
```powershell
.\flash.cmd
```
Or using PowerShell directly:
```powershell
# Regenerate C++ fallback header from data/Korean_sentences.txt and flash over USB:
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Firmware.ps1 -Run

# Or only compile the firmware binary:
powershell -NoProfile -ExecutionPolicy Bypass -File .\Build-Firmware.ps1 -Compile
```
