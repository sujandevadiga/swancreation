# MARS Rover ESP32 Flasher

Single-screen **Windows 10/11, 64-bit** desktop flasher for the supplied MARS rover firmware. No Python, Arduino IDE, Go, or .NET installation is required to run the EXE. Espressif's standalone esptool 5.5.0 is bundled inside it and extracted to a temporary directory while flashing.

## Downloads

- [Download Windows x64 EXE](https://github.com/sujandevadiga/swancreation/raw/refs/heads/main/dist/MARS-Rover-Flasher-x64.exe)
- [Download source and build inputs](https://github.com/sujandevadiga/swancreation/raw/refs/heads/main/dist/MARS-Rover-Flasher-source.zip)
- [Instructions](https://github.com/sujandevadiga/swancreation/blob/main/README.md)
- [Windows build/self-test status](https://github.com/sujandevadiga/swancreation/actions/workflows/windows.yml)

The EXE is unsigned. Use the hashes in `dist/SHA256SUMS.txt` to check your download. A USB-to-serial driver appropriate to your board (for example CP210x or CH340) may still be needed.

## Flash a rover

1. Connect the ESP32 using a USB data cable. Close Arduino Serial Monitor and other applications using its port.
2. Open `MARS-Rover-Flasher-x64.exe`.
3. Enter a whole **Rover number** from **0 to 100**. The hotspot preview updates immediately, for example `7` → `MarsRover - 007`.
4. Select the board's **USB / COM port**. Entries include Windows device details, for example `COM7 — Silicon Labs CP210x USB to UART Bridge` or a CH340/FTDI driver name. With multiple ports, choose the correct board explicitly. Click **Refresh** if needed; connection changes also trigger a refresh.
5. Check the selected port and hotspot preview, then click **Flash**. Upload starts immediately without a dialog box; progress, errors and completion appear in the same window.
6. Watch the upload percentage increase from **0% to 100%** and wait for **SUCCESS**. The bar uses esptool's acknowledged write addresses across all four images, without restarting for each image. It stays below 100% until esptool completes verification and exits successfully. The log includes connection, writing, verification and reset output; a failed upload retains its last reported percentage. Do not unplug the board during flashing.
7. Connect to its Wi-Fi hotspot using password **12345678**, then open **http://192.168.4.1** to use the existing rover controls.

| Rover number | Hotspot |
| --- | --- |
| 0 | `MarsRover - 000` |
| 7 | `MarsRover - 007` |
| 26 | `MarsRover - 026` |
| 100 | `MarsRover - 100` |

The port selector, rover-number field, live hotspot name, Wi-Fi password, single Flash button, 0–100% upload bar, percentage label, and IDE-style monospaced activity log are all on one screen. Device details come from the installed Windows driver; if Windows supplies no description, the COM port is still listed with “Device details unavailable.” Flash is disabled until a port and a valid number are selected.

The number defaults to 0 each time the app starts. Changing the field alone does not change an already-flashed rover; flash it again to apply a different number. The current rover number is built into the flashed firmware and survives a board reset.

If connection fails, confirm the selected COM port, check the cable and driver, hold the board's **BOOT** button while the tool connects, then release it when writing begins. If needed, press **EN/RESET** after flashing. The tool targets the original ESP32, not ESP32-S2/S3/C3/C6 or other variants.

## Included firmware and board settings

The user's `MarsRover.zip` supplied `MarsRover.ino`, `pixelcar-control.h`, the HTML control reference, bootloader, partition table and compiled application. The original application names its hotspot `MarsRover - 001`. The flasher validates that application's segment checksums and SHA-256, replaces its three number digits without changing string length, then recalculates the ESP image XOR checksum and appended SHA-256. It does not rebuild the Arduino sketch or change its motor, sensor, matrix, or web-control logic.

This personalization method supports only these supplied unsigned firmware assets. It rejects an unexpected, corrupted, truncated or signed image. Do not replace the assets with a different build without updating and validating the personalization code. The embedded application descriptor/ELF metadata remains that of the original build.

The supplied flash arguments specify ESP32 / 4 MB flash / DIO / 80 MHz:

| Address | Image |
| --- | --- |
| `0x1000` | `MarsRover.ino.bootloader.bin` |
| `0x8000` | `MarsRover.ino.partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | Personalized application |

Flashing replaces the application, bootloader and partition table. It does not request a whole-chip erase. The original build settings and partition CSV are retained under `assets/`.

The original source uses motor pins ENA 16, ENB 17, IN1 26, IN2 27, IN3 12, IN4 13; IR sensor GPIO15; and an MD_MAX72XX FC16 four-module display on DATA 18, CLK 23, CS 19. Ensure this matches your rover hardware.

## Build the Windows EXE

Install Go **1.25.1** (or a compatible newer Go toolchain). There are no third-party Go modules and no C compiler requirement. From this repository:

**Linux/macOS:**

```sh
bash scripts/build.sh
```

**Windows PowerShell:**

```powershell
go test -v ./...
$env:GOOS = 'windows'
$env:GOARCH = 'amd64'
$env:CGO_ENABLED = '0'
go build -buildvcs=false -trimpath -ldflags='-H=windowsgui -s -w' -o dist/MARS-Rover-Flasher-x64.exe .
$p = Start-Process -FilePath .\dist\MARS-Rover-Flasher-x64.exe -ArgumentList '--self-test' -Wait -PassThru
$p.ExitCode # 0 means all numbers personalize and bundled esptool starts
```

The app embeds every file in `assets/`, including the Windows standalone esptool executable and firmware. Its normal runtime requires no Internet access and no administrator privileges. Temporary files are removed after esptool finishes.

To modify the Arduino firmware, open `firmware/MarsRover/MarsRover.ino` with Arduino IDE, install the Espressif ESP32 board package and MD_MAX72XX library, and select the ESP32 Dev Module settings recorded in `assets/build.options.json`. The archive did not identify the exact Arduino core/library versions, so rebuilding the sketch byte-for-byte is not established. The supplied compiled binaries are the inputs used for this flasher build.

## Validation and limitations

`go test -v ./...` exercises all 101 rover numbers, their embedded names and SHA-256 digests, preserves bytes outside the intended changes, and checks rejection of invalid numbers and corrupted/truncated images. The Windows workflow additionally builds the EXE and runs `--self-test`, which starts bundled esptool without needing an ESP32. It also runs `--ui-self-test` against real Windows controls: number/preview updates, out-of-range and invalid inputs, Flash enable/disable behavior, and a simulated CP210x port entry whose friendly description must map to the raw COM name. The simulated entry is used only by the test; normal operation lists actual Windows ports.

The initial EXE was cross-compiled on Linux. Windows GUI interaction with physical USB devices and actual hardware flashing cannot be tested in that environment. Check the linked Windows workflow for its separate result; a successful build or self-test does not establish physical rover behavior.

## Source and third-party notices

- Flasher source: `main_windows.go`, `ports_windows.go`, `theme_windows.go`, `firmware.go`, tests, and build scripts.
- Original rover source: `firmware/MarsRover/`.
- esptool 5.5.0: official [Windows release](https://github.com/espressif/esptool/releases/tag/v5.5.0), [corresponding source](https://github.com/espressif/esptool/tree/v5.5.0), the bundled source archive `third_party/esptool-v5.5.0-source.tar.gz`, and `assets/ESPTOOL-LICENSE.txt` / `assets/ESPTOOL-README.md`. Its standalone distribution includes its own Python runtime; users do not install Python separately.
- Asset origin and SHA-256 fingerprints: `ASSETS.json`.
