# Validation of the initial build

Executed in the Linux cloud build environment:

- Go archive SHA-256 matched official Go release metadata.
- `go test -v ./...`: passed `TestAllRoverNumbers`, including all 101 values, expected hotspot bytes, digest verification, unchanged-byte checks, out-of-range values, corrupted image and truncated image.
- Independent Espressif esptool 5.1.0 `ESP32FirmwareImage` parser: loaded all 101 images produced by the Go personalization function; each stored XOR checksum and stored SHA-256 matched esptool's calculation.
- `GOOS=windows GOARCH=amd64 go vet ./...`: passed.
- `scripts/build.sh`: passed; generated Windows AMD64 PE executable.
- Both flasher EXE and bundled esptool EXE were checked for PE machine type `0x8664` (AMD64).

Not executed in the Linux build environment:

- Windows GUI interaction and bundled Windows esptool execution.
- USB/COM-port communication, physical flashing, hotspot connection, or motor/display behavior on a real rover.

The Windows GitHub Actions workflow runs the tests and the EXE's `--self-test` (including bundled esptool `version`). Its result is separate from the checks above. Neither that self-test nor cross-compilation validates physical rover hardware.

GitHub Windows workflow run 38569737764 passed the native Windows build, all firmware tests, and bundled EXE/esptool self-test. Physical hardware checks remain unrun.

## Single-screen interface update

The Windows workflow includes a native GUI self-test for control creation, live hotspot previews, invalid-number rejection, Flash enablement, and mapping a simulated `COM7 — Silicon Labs CP210x USB to UART Bridge` entry back to `COM7`. Device descriptions in normal operation are queried from Windows SetupAPI/Device Manager registry properties. Real CP210x/CH340/FTDI hardware and USB flashing still require physical validation.

## Updated firmware: MarsRover.zip

The updated source and compiled ESP32 images replace the original firmware inputs. The uploaded sketch uses `MarsRover - 001`; the flasher preserves this prefix and personalizes only its three digits for rover numbers 0–100. The application image is now 997,728 bytes. The flash layout remains ESP32 / DIO / 80 MHz / 4 MB, with addresses 0x1000, 0x8000, 0xe000 and 0x10000.
