//go:build windows

package main

import (
	"embed"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"unsafe"
)

//go:embed assets/*
var assets embed.FS
var user = syscall.NewLazyDLL("user32.dll")
var kernel = syscall.NewLazyDLL("kernel32.dll")
var create = user.NewProc("CreateWindowExW")
var send = user.NewProc("SendMessageW")
var setText = user.NewProc("SetWindowTextW")
var enable = user.NewProc("EnableWindow")
var hwnd, ports, number, preview, logbox, flash, refresh, status, progress, deviceDetails, heading uintptr
var bodyFont, titleFont, previewFont, background, logBackground uintptr
var availablePorts []serialPort
var lastFlashError error
var progressLabel uintptr
var uploadPercent int
var busy bool
var logMu sync.Mutex
var logText string

func wide(s string) *uint16 { p, _ := syscall.UTF16PtrFromString(s); return p }
func ptr(s string) uintptr  { return uintptr(unsafe.Pointer(wide(s))) }
func control(class, title string, style uint32, x, y, w, h int, id int) uintptr {
	r, _, _ := create.Call(0, ptr(class), ptr(title), uintptr(style|0x50000000), uintptr(x), uintptr(y), uintptr(w), uintptr(h), hwnd, uintptr(id), 0, 0)
	if bodyFont != 0 {
		send.Call(r, 0x30, bodyFont, 1)
	}
	return r
}
func textOf(h uintptr) string {
	b := make([]uint16, 4096)
	user.NewProc("GetWindowTextW").Call(h, uintptr(unsafe.Pointer(&b[0])), uintptr(len(b)))
	return syscall.UTF16ToString(b)
}
func label(h uintptr, s string) { setText.Call(h, ptr(s)) }
func appendLog(s string) {
	logMu.Lock()
	defer logMu.Unlock()
	logText += s
	if len(logText) > 100000 {
		logText = logText[len(logText)-100000:]
	}
	user.NewProc("PostMessageW").Call(hwnd, 0x8001, 0, 0)
}

func selectedPort() string {
	index, _, _ := send.Call(ports, 0x147, 0, 0)
	if index >= uintptr(len(availablePorts)) {
		return ""
	}
	return availablePorts[index].Name
}
func refreshState() {
	n, err := getNumber()
	if err != nil {
		label(preview, "Enter a whole number from 0 to 100")
	} else {
		name, _ := hotspot(n)
		label(preview, name)
	}
	enabled := uintptr(0)
	if !busy && err == nil && selectedPort() != "" {
		enabled = 1
	}
	enable.Call(flash, enabled)
	if busy {
		return
	}
	if selectedPort() == "" {
		label(status, "Connect your ESP32 by USB, then select its COM port.")
	} else if err != nil {
		label(status, "Enter a rover number from 0 to 100.")
	} else {
		label(status, "Ready. Click Flash to upload the selected rover number.")
	}
	index, _, _ := send.Call(ports, 0x147, 0, 0)
	if index < uintptr(len(availablePorts)) {
		description := availablePorts[index].Description
		if description == "" {
			description = "Windows has not provided a device description."
		}
		label(deviceDetails, description)
	} else {
		label(deviceDetails, "Device details appear here after you select a port.")
	}
}
func scanPorts() {
	if busy {
		return
	}
	old := selectedPort()
	found, err := listSerialPorts()
	send.Call(ports, 0x14b, 0, 0)
	availablePorts = found
	selected := -1
	for i, p := range found {
		send.Call(ports, 0x143, 0, ptr(p.Label()))
		if p.Name == old {
			selected = i
		}
	}
	// With multiple devices, require an explicit choice instead of silently
	// selecting an unrelated serial port such as Intel AMT.
	if selected < 0 && len(found) == 1 {
		selected = 0
	}
	send.Call(ports, 0x14e, uintptr(selected), 0)
	refreshState()
	if err != nil {
		label(status, "Unable to read COM ports. Reconnect the board and click Refresh.")
		appendLog(err.Error() + "\r\n")
	}
}
func getNumber() (int, error) {
	s := strings.TrimSpace(textOf(number))
	n, e := strconv.Atoi(s)
	if e != nil {
		return 0, fmt.Errorf("Enter a whole rover number from 0 to 100")
	}
	_, e = hotspot(n)
	return n, e
}
func startFlash() {
	if busy {
		return
	}
	n, e := getNumber()
	if e != nil {
		label(status, e.Error())
		return
	}
	port := selectedPort()
	if port == "" {
		label(status, "Select a COM port. Connect the ESP32 and click Refresh.")
		return
	}
	name, _ := hotspot(n)
	busy = true
	for _, h := range []uintptr{flash, refresh, ports, number} {
		enable.Call(h, 0)
	}
	logMu.Lock()
	logText = ""
	logMu.Unlock()
	label(status, "Connecting to "+port+"… Upload: 0%")
	uploadPercent = 0
	setUploadProgress(0)
	send.Call(progress, 0x410, 1, 0)
	appendLog("Preparing " + name + " on " + port + "...\r\n")
	go func() {
		e := runFlash(port, n)
		logMu.Lock()
		lastFlashError = e
		logMu.Unlock()
		if e != nil {
			appendLog("\r\nFAILED: " + e.Error() + "\r\nHold BOOT while connecting if necessary, check drivers/cable, then retry.\r\n")
		} else {
			appendLog("\r\nSUCCESS: firmware written and verified by esptool.\r\nHotspot: " + name + "\r\nPassword: 12345678\r\nOpen http://192.168.4.1 after connecting.\r\n")
		}
		user.NewProc("PostMessageW").Call(hwnd, 0x8002, 0, 0)
	}()
}
func runFlash(port string, n int) error {
	original, e := assets.ReadFile("assets/mars_rover_goodera_ESP.ino.bin")
	if e != nil {
		return e
	}
	image, e := personalize(original, n)
	if e != nil {
		return e
	}
	dir, e := os.MkdirTemp("", "mars-rover-")
	if e != nil {
		return e
	}
	defer os.RemoveAll(dir)
	for _, name := range []string{"esptool.exe", "mars_rover_goodera_ESP.ino.bootloader.bin", "mars_rover_goodera_ESP.ino.partitions.bin", "boot_app0.bin"} {
		b, e := assets.ReadFile("assets/" + name)
		if e != nil {
			return e
		}
		if e = os.WriteFile(filepath.Join(dir, name), b, 0600); e != nil {
			return e
		}
	}
	if e = os.WriteFile(filepath.Join(dir, "application.bin"), image, 0600); e != nil {
		return e
	}
	args := []string{"--chip", "esp32", "--port", port, "--baud", "460800", "--before", "default-reset", "--after", "hard-reset", "write-flash", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "4MB", "0x1000", "mars_rover_goodera_ESP.ino.bootloader.bin", "0x8000", "mars_rover_goodera_ESP.ino.partitions.bin", "0xe000", "boot_app0.bin", "0x10000", "application.bin"}
	cmd := exec.Command(filepath.Join(dir, "esptool.exe"), args...)
	cmd.Dir = dir
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	sizes := []struct {
		name    string
		address int64
	}{{"mars_rover_goodera_ESP.ino.bootloader.bin", 0x1000}, {"mars_rover_goodera_ESP.ino.partitions.bin", 0x8000}, {"boot_app0.bin", 0xe000}, {"application.bin", 0x10000}}
	var images []uploadImage
	for _, file := range sizes {
		info, err := os.Stat(filepath.Join(dir, file.name))
		if err != nil {
			return err
		}
		images = append(images, uploadImage{Address: file.address, Size: info.Size()})
	}
	output := &uploadOutput{Images: images, Line: appendLog, Progress: func(percent, stage int) {
		user.NewProc("PostMessageW").Call(hwnd, 0x8004, uintptr(percent), uintptr(stage))
	}}
	cmd.Stdout = output
	cmd.Stderr = output
	err := cmd.Run()
	output.Flush()
	return err
}

type wndClass struct {
	Size, Style                        uint32
	Proc                               uintptr
	ClsExtra, WndExtra                 int32
	Instance, Icon, Cursor, Background uintptr
	Menu, Name                         *uint16
	SmallIcon                          uintptr
}
type msg struct {
	Window         uintptr
	Message        uint32
	WParam, LParam uintptr
	Time           uint32
	X, Y           int32
	Private        uint32
}

func wndProc(h uintptr, m uint32, w, l uintptr) uintptr {
	switch m {
	case 0x111:
		switch int(w & 0xffff) {
		case 10:
			scanPorts()
		case 11:
			startFlash()
		case 2:
			if w>>16 == 1 {
				refreshState()
			}
		case 20:
			if w>>16 == 0x300 {
				refreshState()
			}
		}
		return 0
	case 0x219: // Device arrival/removal: debounce Windows' notification burst.
		if !busy {
			user.NewProc("SetTimer").Call(h, 1, 500, 0)
		}
		return 0
	case 0x113:
		if w == 1 {
			user.NewProc("KillTimer").Call(h, 1)
			scanPorts()
		}
		return 0
	case 0x138: // WM_CTLCOLORSTATIC: soft background and emphasis colors.
		if l == logbox {
			gdi.NewProc("SetBkColor").Call(w, 0x00372718)
			gdi.NewProc("SetTextColor").Call(w, 0x00f6f2ed)
			return logBackground
		}
		gdi.NewProc("SetBkColor").Call(w, 0x00f6f2ed)
		color := uintptr(0x00513b20)
		if l == preview {
			color = 0x007e7d00
		}
		if l == deviceDetails {
			color = 0x00796858
		}
		gdi.NewProc("SetTextColor").Call(w, color)
		return background
	case 0x8003:
		if err := uiSelfTest(); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		user.NewProc("DestroyWindow").Call(h)
		return 0
	case 0x8001:
		logMu.Lock()
		s := logText
		logMu.Unlock()
		s = strings.ReplaceAll(strings.ReplaceAll(s, "\r\n", "\n"), "\n", "\r\n")
		label(logbox, s)
		send.Call(logbox, 0xb1, ^uintptr(0), ^uintptr(0))
		send.Call(logbox, 0xb7, 0, 0)
		return 0
	case 0x8004:
		if busy {
			setUploadProgress(int(w))
			stage := int(l) + 1
			if stage > 4 {
				stage = 4
			}
			if uploadPercent == 99 {
				label(status, "Upload: 99% — finishing write and verification…")
			} else {
				label(status, fmt.Sprintf("Uploading: %d%% — image %d of 4. Keep ESP32 connected.", uploadPercent, stage))
			}
		}
		return 0
	case 0x8002:
		busy = false
		for _, c := range []uintptr{refresh, ports, number} {
			enable.Call(c, 1)
		}
		refreshState()
		logMu.Lock()
		err := lastFlashError
		logMu.Unlock()
		if err == nil {
			setUploadProgress(100)
			label(status, "Upload: 100% — finished. Firmware verified; connect to rover Wi-Fi.")
		} else {
			send.Call(progress, 0x410, 2, 0)
			label(status, fmt.Sprintf("Upload failed at %d%%. See the log; check USB cable and BOOT button.", uploadPercent))
		}
		return 0
	case 0x10:
		if busy {
			label(status, "Flashing is in progress. Wait for completion before closing.")
			return 0
		}
		user.NewProc("DestroyWindow").Call(h)
		return 0
	case 2:
		user.NewProc("PostQuitMessage").Call(0)
		return 0
	}
	r, _, _ := user.NewProc("DefWindowProcW").Call(h, uintptr(m), w, l)
	return r
}
func selfTest() error {
	original, err := assets.ReadFile("assets/mars_rover_goodera_ESP.ino.bin")
	if err != nil {
		return err
	}
	for n := 0; n <= 100; n++ {
		if _, err = personalize(original, n); err != nil {
			return err
		}
	}
	dir, err := os.MkdirTemp("", "mars-selftest-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(dir)
	tool, err := assets.ReadFile("assets/esptool.exe")
	if err != nil {
		return err
	}
	exe := filepath.Join(dir, "esptool.exe")
	if err = os.WriteFile(exe, tool, 0600); err != nil {
		return err
	}
	cmd := exec.Command(exe, "version")
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("bundled esptool: %w: %s", err, output)
	}
	if !strings.Contains(string(output), "5.5.0") {
		return fmt.Errorf("unexpected esptool version: %s", output)
	}
	return nil
}

var gdi = syscall.NewLazyDLL("gdi32.dll")

func font(height, weight int) uintptr {
	h, _, _ := gdi.NewProc("CreateFontW").Call(uintptr(-height), 0, 0, 0, uintptr(weight), 0, 0, 0, 1, 0, 0, 5, 0, ptr("Segoe UI"))
	return h
}
func setUploadProgress(percent int) {
	if percent < 0 {
		percent = 0
	}
	if percent > 100 {
		percent = 100
	}
	uploadPercent = percent
	send.Call(progress, 0x402, uintptr(percent), 0)
	label(progressLabel, fmt.Sprintf("%d%%", percent))
}

// Exercise real Win32 controls and selection mapping without touching hardware.
// The simulated entry exists only for this test and is never shown in normal use.
func uiSelfTest() error {
	for _, h := range []uintptr{ports, number, preview, logbox, flash, refresh, status, progress, deviceDetails, heading} {
		ok, _, _ := user.NewProc("IsWindow").Call(h)
		if ok == 0 {
			return fmt.Errorf("a UI control failed to initialize")
		}
	}
	for _, n := range []int{0, 7, 26, 100} {
		label(number, strconv.Itoa(n))
		expected, _ := hotspot(n)
		if textOf(preview) != expected {
			return fmt.Errorf("hotspot preview mismatch for %d: %s", n, textOf(preview))
		}
	}
	availablePorts = []serialPort{{Name: "COM7", Description: "Silicon Labs CP210x USB to UART Bridge (UI test device)"}}
	send.Call(ports, 0x14b, 0, 0)
	send.Call(ports, 0x143, 0, ptr(availablePorts[0].Label()))
	send.Call(ports, 0x14e, 0, 0)
	refreshState()
	if selectedPort() != "COM7" || !strings.Contains(textOf(ports), "Silicon Labs CP210x") {
		return fmt.Errorf("COM-port description or selection mapping failed")
	}
	for _, invalid := range []string{"", "101", "-1", "abc"} {
		label(number, invalid)
		if _, err := getNumber(); err == nil {
			return fmt.Errorf("invalid rover input accepted: %q", invalid)
		}
		enabled, _, _ := user.NewProc("IsWindowEnabled").Call(flash)
		if enabled != 0 {
			return fmt.Errorf("Flash enabled for invalid number %q", invalid)
		}
	}
	label(number, "7")
	enabled, _, _ := user.NewProc("IsWindowEnabled").Call(flash)
	if enabled == 0 || textOf(preview) != "MARS ROVER-007" {
		return fmt.Errorf("Flash not enabled for valid selection")
	}
	for _, percent := range []int{0, 42, 99, 100} {
		setUploadProgress(percent)
		actual, _, _ := send.Call(progress, 0x408, 0, 0)
		if int(actual) != percent || textOf(progressLabel) != fmt.Sprintf("%d%%", percent) {
			return fmt.Errorf("progress UI mismatch for %d", percent)
		}
	}
	return nil
}
func main() {
	if len(os.Args) > 1 && os.Args[1] == "--self-test" {
		if err := selfTest(); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		os.Exit(0)
	}
	runtime.LockOSThread()
	releaseStyles := activateVisualStyles()
	defer releaseStyles()
	syscall.NewLazyDLL("comctl32.dll").NewProc("InitCommonControls").Call()
	instance, _, _ := kernel.NewProc("GetModuleHandleW").Call(0)
	cursor, _, _ := user.NewProc("LoadCursorW").Call(0, 32512)
	background, _, _ = gdi.NewProc("CreateSolidBrush").Call(0x00f6f2ed)
	logBackground, _, _ = gdi.NewProc("CreateSolidBrush").Call(0x00372718)
	bodyFont = font(17, 400)
	titleFont = font(30, 700)
	previewFont = font(22, 700)
	wc := wndClass{Proc: syscall.NewCallback(wndProc), Instance: instance, Cursor: cursor, Background: background, Name: wide("MARSFlasher")}
	wc.Size = uint32(unsafe.Sizeof(wc))
	user.NewProc("RegisterClassExW").Call(uintptr(unsafe.Pointer(&wc)))
	hwnd, _, _ = create.Call(0, ptr("MARSFlasher"), ptr("MARS Rover Flasher"), 0x10ca0000, 100, 70, 790, 740, 0, 0, instance, 0)
	if hwnd == 0 {
		os.Exit(1)
	}
	heading = control("STATIC", "MARS Rover Flasher", 0, 28, 24, 710, 44, 100)
	send.Call(heading, 0x30, titleFont, 1)
	control("STATIC", "Connect your ESP32, choose its rover number, and flash.", 0, 30, 78, 710, 26, 101)
	control("STATIC", "USB / COM port", 0, 30, 126, 710, 24, 102)
	ports = control("COMBOBOX", "", 0x00210003, 30, 157, 590, 260, 2)
	refresh = control("BUTTON", "Refresh", 0x10000, 635, 153, 110, 34, 10)
	send.Call(ports, 0x1703, 0, ptr("Select your ESP32 COM port"))
	send.Call(ports, 0x160, 710, 0) // Wide dropdown for full driver descriptions.
	deviceDetails = control("STATIC", "", 0, 30, 199, 710, 26, 103)
	control("STATIC", "Rover number (0–100)", 0, 30, 244, 710, 25, 104)
	number = control("EDIT", "0", 0x00812080, 30, 276, 715, 34, 20)
	send.Call(number, 0xc5, 3, 0)
	preview = control("STATIC", "MARS ROVER-000", 0, 30, 328, 710, 33, 105)
	send.Call(preview, 0x30, previewFont, 1)
	control("STATIC", "Wi-Fi password: 12345678   •   Rover controls: 192.168.4.1", 0, 30, 370, 715, 25, 106)
	flash = control("BUTTON", "Flash", 0x10001, 30, 413, 180, 42, 11)
	status = control("STATIC", "", 0, 30, 477, 715, 27, 107)
	progress = control("msctls_progress32", "", 0, 30, 512, 645, 14, 108)
	progressLabel = control("STATIC", "0%", 2, 685, 505, 60, 26, 110)
	send.Call(progress, 0x406, 0, 100)
	logbox = control("EDIT", "Flash activity will appear here.", 0x002008c4, 30, 542, 715, 137, 109)
	logFont, _, _ := gdi.NewProc("CreateFontW").Call(uintptr(0xffffffF0), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 5, 0, ptr("Consolas"))
	send.Call(logbox, 0x30, logFont, 1)
	// Apply modern Windows visual styles where available, without extra DLLs.
	theme := syscall.NewLazyDLL("uxtheme.dll").NewProc("SetWindowTheme")
	for _, h := range []uintptr{ports, number, flash, refresh, progress} {
		theme.Call(h, ptr("Explorer"), 0)
	}
	scanPorts()
	user.NewProc("SetFocus").Call(number)
	send.Call(number, 0xb1, 0, ^uintptr(0))
	if len(os.Args) > 1 && os.Args[1] == "--ui-self-test" {
		user.NewProc("PostMessageW").Call(hwnd, 0x8003, 0, 0)
	}
	var m msg
	for {
		r, _, _ := user.NewProc("GetMessageW").Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		handled, _, _ := user.NewProc("IsDialogMessageW").Call(hwnd, uintptr(unsafe.Pointer(&m)))
		if handled != 0 {
			continue
		}
		user.NewProc("TranslateMessage").Call(uintptr(unsafe.Pointer(&m)))
		user.NewProc("DispatchMessageW").Call(uintptr(unsafe.Pointer(&m)))
	}
}
