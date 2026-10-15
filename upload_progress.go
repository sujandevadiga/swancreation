package main

import (
	"regexp"
	"strconv"
	"strings"
	"sync"
)

type uploadImage struct{ Address, Size int64 }

var writeAddress = regexp.MustCompile(`Writing at\s+(0x[0-9a-fA-F]+)`)
var terminalEscape = regexp.MustCompile("\x1b\\[[0-?]*[ -/]*[@-~]")

// esptool reports the acknowledged uncompressed end address as it writes.
// Weight all four images by their sizes so the overall bar never restarts at
// zero between bootloader, partitions, OTA data and application. Completion is
// reserved for the caller after esptool verifies the images and exits cleanly.
type uploadOutput struct {
	mu             sync.Mutex
	Images         []uploadImage
	Line           func(string)
	Progress       func(int, int)
	pending        string
	stage, percent int
}

func (u *uploadOutput) Write(data []byte) (int, error) {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.pending += string(data)
	for {
		i := strings.IndexAny(u.pending, "\r\n")
		if i < 0 {
			break
		}
		line := u.pending[:i]
		u.pending = u.pending[i+1:]
		u.consume(line)
	}
	if len(u.pending) > 16384 {
		u.consume(u.pending)
		u.pending = ""
	}
	return len(data), nil
}
func (u *uploadOutput) Flush() {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.consume(u.pending)
	u.pending = ""
}
func (u *uploadOutput) consume(raw string) {
	line := terminalEscape.ReplaceAllString(raw, "")
	if strings.TrimSpace(line) == "" {
		return
	}
	if u.Line != nil {
		u.Line(line + "\r\n")
	}
	if u.stage >= len(u.Images) {
		return
	}
	var completed, total int64
	for i, image := range u.Images {
		total += image.Size
		if i < u.stage {
			completed += image.Size
		}
	}
	if total <= 0 {
		return
	}
	if match := writeAddress.FindStringSubmatch(line); match != nil {
		address, err := strconv.ParseInt(match[1][2:], 16, 64)
		if err == nil {
			current := u.Images[u.stage]
			written := address - current.Address
			if written < 0 {
				written = 0
			}
			if written > current.Size {
				written = current.Size
			}
			u.emit(int((completed + written) * 100 / total))
		}
	}
	if strings.Contains(line, "Hash of data verified.") {
		completed += u.Images[u.stage].Size
		u.stage++
		u.emit(int(completed * 100 / total))
	}
}
func (u *uploadOutput) emit(percent int) {
	if percent > 99 {
		percent = 99
	}
	if percent < u.percent {
		return
	}
	u.percent = percent
	if u.Progress != nil {
		u.Progress(percent, u.stage)
	}
}
