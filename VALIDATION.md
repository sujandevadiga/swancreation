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
