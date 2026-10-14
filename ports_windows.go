//go:build windows

package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
)

type serialPort struct{ Name, Description string }

func (p serialPort) Label() string {
	if p.Description == "" {
		return p.Name + " — Device details unavailable"
	}
	return p.Name + " — " + p.Description
}

type deviceInfo struct {
	Size      uint32
	ClassGUID syscall.GUID
	DevInst   uint32
	Reserved  uintptr
}

var setup = syscall.NewLazyDLL("setupapi.dll")
var registry = syscall.NewLazyDLL("advapi32.dll")

func deviceProperty(set uintptr, device *deviceInfo, property uintptr) string {
	data := make([]uint16, 4096)
	ok, _, _ := setup.NewProc("SetupDiGetDeviceRegistryPropertyW").Call(set, uintptr(unsafe.Pointer(device)), property, 0, uintptr(unsafe.Pointer(&data[0])), uintptr(len(data)*2), 0)
	if ok == 0 {
		return ""
	}
	return syscall.UTF16ToString(data)
}

// Resolve the device's actual PortName through its Windows hardware registry
// key. Friendly labels are for display only and never passed to esptool.
func devicePortName(set uintptr, device *deviceInfo) string {
	key, _, _ := setup.NewProc("SetupDiOpenDevRegKey").Call(set, uintptr(unsafe.Pointer(device)), 1, 0, 1, 0x20019)
	if key == ^uintptr(0) {
		return ""
	}
	defer registry.NewProc("RegCloseKey").Call(key)
	data := make([]uint16, 256)
	size := uint32(len(data) * 2)
	result, _, _ := registry.NewProc("RegQueryValueExW").Call(key, ptr("PortName"), 0, 0, uintptr(unsafe.Pointer(&data[0])), uintptr(unsafe.Pointer(&size)))
	if result != 0 {
		return ""
	}
	return syscall.UTF16ToString(data)
}
func portDescriptions() map[string]string {
	descriptions := map[string]string{}
	// Include all present device classes, since some serial drivers use a
	// vendor-specific class instead of the standard Ports class.
	set, _, _ := setup.NewProc("SetupDiGetClassDevsW").Call(0, 0, 0, 6)
	if set == ^uintptr(0) {
		return descriptions
	}
	defer setup.NewProc("SetupDiDestroyDeviceInfoList").Call(set)
	for index := uintptr(0); ; index++ {
		var device deviceInfo
		device.Size = uint32(unsafe.Sizeof(device))
		ok, _, _ := setup.NewProc("SetupDiEnumDeviceInfo").Call(set, index, uintptr(unsafe.Pointer(&device)))
		if ok == 0 {
			break
		}
		// SPDRP_FRIENDLYNAME is the same description shown by Device Manager.
		friendly := deviceProperty(set, &device, 12)
		name := devicePortName(set, &device)
		if !validCOMName(name) {
			continue
		}
		if friendly == "" {
			friendly = deviceProperty(set, &device, 0)
		}
		// COM name already appears at the beginning of the dropdown entry.
		friendly = strings.TrimSuffix(friendly, " ("+name+")")
		descriptions[name] = friendly
	}
	return descriptions
}
func validCOMName(name string) bool {
	if !strings.HasPrefix(name, "COM") {
		return false
	}
	n, e := strconv.Atoi(name[3:])
	return e == nil && n > 0 && name == fmt.Sprintf("COM%d", n)
}
func listSerialPorts() ([]serialPort, error) {
	buffer := make([]uint16, 65536)
	size, _, err := kernel.NewProc("QueryDosDeviceW").Call(0, uintptr(unsafe.Pointer(&buffer[0])), uintptr(len(buffer)))
	if size == 0 {
		return nil, fmt.Errorf("could not list COM ports: %v", err)
	}
	details := portDescriptions()
	var result []serialPort
	start := 0
	for i, v := range buffer[:size] {
		if v != 0 {
			continue
		}
		if i == start {
			break
		}
		name := syscall.UTF16ToString(buffer[start:i])
		start = i + 1
		if validCOMName(name) {
			result = append(result, serialPort{Name: name, Description: details[name]})
		}
	}
	sort.Slice(result, func(i, j int) bool {
		a, _ := strconv.Atoi(result[i].Name[3:])
		b, _ := strconv.Atoi(result[j].Name[3:])
		return a < b
	})
	return result, nil
}
