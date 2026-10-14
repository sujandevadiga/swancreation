//go:build windows

package main

import (
	"embed"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
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
var show = user.NewProc("ShowWindow")
var setText = user.NewProc("SetWindowTextW")
var enable = user.NewProc("EnableWindow")
var hwnd, tab, ports, number, preview, logbox, flash, refresh, apply uintptr
var flashPage, numberPage []uintptr
var busy bool
var logMu sync.Mutex
var logText string
var selectedNumber int

func wide(s string) *uint16 { p, _ := syscall.UTF16PtrFromString(s); return p }
func ptr(s string) uintptr  { return uintptr(unsafe.Pointer(wide(s))) }
func control(class, title string, style uint32, x, y, w, h int, id int) uintptr {
	r, _, _ := create.Call(0, ptr(class), ptr(title), uintptr(style|0x50000000), uintptr(x), uintptr(y), uintptr(w), uintptr(h), hwnd, uintptr(id), 0, 0)
	return r
}
func textOf(h uintptr) string {
	b := make([]uint16, 4096)
	user.NewProc("GetWindowTextW").Call(h, uintptr(unsafe.Pointer(&b[0])), uintptr(len(b)))
	return syscall.UTF16ToString(b)
}
func label(h uintptr, s string) { setText.Call(h, ptr(s)) }
func message(s string) {
	user.NewProc("MessageBoxW").Call(hwnd, ptr(s), ptr("MARS Rover Flasher"), 0x30)
}
func appendLog(s string) {
	logMu.Lock()
	defer logMu.Unlock()
	logText += s
	if len(logText) > 100000 {
		logText = logText[len(logText)-100000:]
	}
	user.NewProc("PostMessageW").Call(hwnd, 0x8001, 0, 0)
}

type logger struct{}

func (logger) Write(p []byte) (int, error) { appendLog(string(p)); return len(p), nil }
func scanPorts() {
	old := textOf(ports)
	send.Call(ports, 0x14b, 0, 0)
	buffer := make([]uint16, 65536)
	r, _, _ := kernel.NewProc("QueryDosDeviceW").Call(0, uintptr(unsafe.Pointer(&buffer[0])), uintptr(len(buffer)))
	if r == 0 {
		message("Could not enumerate COM ports. Reconnect the USB cable and refresh.")
		return
	}
	var names []string
	// QueryDosDevice returns a MULTI_SZ rather than a single terminated string.
	start := 0
	for i, v := range buffer[:r] {
		if v == 0 {
			if i == start {
				break
			}
			s := syscall.UTF16ToString(buffer[start:i])
			start = i + 1
			if strings.HasPrefix(s, "COM") {
				if n, e := strconv.Atoi(strings.TrimPrefix(s, "COM")); e == nil && n > 0 {
					names = append(names, s)
				}
			}
		}
	}
	sort.Slice(names, func(i, j int) bool {
		a, _ := strconv.Atoi(names[i][3:])
		b, _ := strconv.Atoi(names[j][3:])
		return a < b
	})
	index := 0
	for i, s := range names {
		send.Call(ports, 0x143, 0, ptr(s))
		if s == old {
			index = i
		}
	}
	if len(names) > 0 {
		send.Call(ports, 0x14e, uintptr(index), 0)
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
func switchPage() {
	i, _, _ := send.Call(tab, 0x130b, 0, 0)
	for _, h := range flashPage {
		v := uintptr(5)
		if i != 0 {
			v = 0
		}
		show.Call(h, v)
	}
	for _, h := range numberPage {
		v := uintptr(5)
		if i != 1 {
			v = 0
		}
		show.Call(h, v)
	}
}
func startFlash() {
	if busy {
		return
	}
	n, e := getNumber()
	if e != nil {
		message(e.Error())
		return
	}
	port := textOf(ports)
	if port == "" {
		message("Select a COM port. Connect the ESP32 and click Refresh.")
		return
	}
	name, _ := hotspot(n)
	r, _, _ := user.NewProc("MessageBoxW").Call(hwnd, ptr("Flash "+name+" to "+port+"?\nThis replaces the ESP32 application and partition table. Existing settings are not erased."), ptr("Confirm ESP32 flash"), 0x24)
	if r != 6 {
		return
	}
	busy = true
	selectedNumber = n
	for _, h := range []uintptr{flash, refresh, ports, number, apply} {
		enable.Call(h, 0)
	}
	logMu.Lock()
	logText = ""
	logMu.Unlock()
	appendLog("Preparing " + name + " on " + port + "...\r\n")
	go func() {
		e := runFlash(port, n)
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
	cmd.Stdout = io.Writer(logger{})
	cmd.Stderr = io.Writer(logger{})
	return cmd.Run()
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
type notify struct {
	From, ID uintptr
	Code     int32
}
type tabItem struct {
	Mask, State, StateMask uint32
	Text                   *uint16
	TextMax, Image         int32
	Param                  uintptr
}

func wndProc(h uintptr, m uint32, w, l uintptr) uintptr {
	switch m {
	case 0x111:
		switch int(w & 0xffff) {
		case 10:
			scanPorts()
		case 11:
			startFlash()
		case 12:
			n, e := getNumber()
			if e != nil {
				message(e.Error())
			} else {
				selectedNumber = n
				send.Call(tab, 0x130c, 0, 0)
				switchPage()
			}
		case 20:
			if w>>16 == 0x300 {
				n, e := getNumber()
				if e != nil {
					label(preview, "Enter a whole number from 0 to 100")
				} else {
					name, _ := hotspot(n)
					label(preview, "Hotspot: "+name)
				}
			}
		}
		return 0
	case 0x4e:
		// The tab is the only common control with ID 1. Query its current
		// selection for notifications without dereferencing OS-owned memory.
		if w == 1 {
			switchPage()
		}
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
	case 0x8002:
		busy = false
		for _, c := range []uintptr{flash, refresh, ports, number, apply} {
			enable.Call(c, 1)
		}
		return 0
	case 0x10:
		if busy {
			message("Flashing is in progress. Wait for completion before closing.")
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
func main() {
	if len(os.Args) > 1 && os.Args[1] == "--self-test" {
		if err := selfTest(); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		os.Exit(0)
	}
	runtime.LockOSThread()
	syscall.NewLazyDLL("comctl32.dll").NewProc("InitCommonControls").Call()
	instance, _, _ := kernel.NewProc("GetModuleHandleW").Call(0)
	cursor, _, _ := user.NewProc("LoadCursorW").Call(0, 32512)
	wc := wndClass{Proc: syscall.NewCallback(wndProc), Instance: instance, Cursor: cursor, Background: 6, Name: wide("MARSFlasher")}
	wc.Size = uint32(unsafe.Sizeof(wc))
	user.NewProc("RegisterClassExW").Call(uintptr(unsafe.Pointer(&wc)))
	hwnd, _, _ = create.Call(0, ptr("MARSFlasher"), ptr("MARS Rover ESP32 Flasher — Windows 64-bit"), 0x10ca0000, 100, 100, 740, 565, 0, 0, instance, 0)
	if hwnd == 0 {
		return
	}
	tab = control("SysTabControl32", "", 0, 12, 12, 700, 490, 1)
	for i, title := range []string{"Flash ESP32", "Rover number"} {
		item := tabItem{Mask: 1, Text: wide(title)}
		send.Call(tab, 0x133e, uintptr(i), uintptr(unsafe.Pointer(&item)))
	}
	addF := func(h uintptr) uintptr { flashPage = append(flashPage, h); return h }
	addN := func(h uintptr) uintptr { numberPage = append(numberPage, h); return h }
	addF(control("STATIC", "ESP32 Dev Module • 4 MB flash • supplied rover firmware", 0, 30, 52, 650, 24, 0))
	addF(control("STATIC", "COM port:", 0, 30, 88, 80, 24, 0))
	ports = addF(control("COMBOBOX", "", 0x00200003, 115, 84, 190, 220, 2))
	refresh = addF(control("BUTTON", "Refresh", 0, 320, 84, 100, 28, 10))
	flash = addF(control("BUTTON", "Flash rover", 1, 440, 84, 150, 30, 11))
	addF(control("STATIC", "Set the rover number in the Rover number tab before flashing.", 0, 30, 125, 650, 24, 0))
	logbox = addF(control("EDIT", "Connect the ESP32 by USB, select its COM port, set its rover number, then flash.", 0x002008c4, 30, 155, 660, 325, 3))
	addN(control("STATIC", "Rover number (0–100):", 0, 35, 65, 230, 25, 0))
	number = addN(control("EDIT", "0", 0x00800000, 270, 60, 100, 30, 20))
	send.Call(number, 0xc5, 3, 0)
	preview = addN(control("STATIC", "Hotspot: MARS ROVER-000", 0, 35, 110, 600, 28, 0))
	addN(control("STATIC", "000 → MARS ROVER-000     7 → MARS ROVER-007     100 → MARS ROVER-100", 0, 35, 155, 650, 45, 0))
	addN(control("STATIC", "The number is applied when you flash. Changing this field alone does not update a connected rover.", 0, 35, 210, 630, 55, 0))
	apply = addN(control("BUTTON", "Use number and return to flashing", 0, 35, 285, 330, 35, 12))
	switchPage()
	scanPorts()
	var m msg
	for {
		r, _, _ := user.NewProc("GetMessageW").Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		user.NewProc("TranslateMessage").Call(uintptr(unsafe.Pointer(&m)))
		user.NewProc("DispatchMessageW").Call(uintptr(unsafe.Pointer(&m)))
	}
}
