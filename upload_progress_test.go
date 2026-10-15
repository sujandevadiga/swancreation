package main

import (
	"fmt"
	"strings"
	"testing"
)

func TestUploadProgressAcrossImagesAndSplitOutput(t *testing.T) {
	images := []uploadImage{{0x1000, 100}, {0x8000, 100}, {0xe000, 100}, {0x10000, 700}}
	var percents []int
	var output strings.Builder
	tracker := &uploadOutput{Images: images, Line: func(s string) { output.WriteString(s) }, Progress: func(p, _ int) { percents = append(percents, p) }}
	for _, image := range images {
		for _, n := range []int64{0, image.Size / 2, image.Size} {
			line := fmt.Sprintf("\x1b[32mWriting at 0x%08x [====] 50.0%%\x1b[0m\r\n", image.Address+n)
			for _, b := range []byte(line) {
				tracker.Write([]byte{b})
			}
		}
		tracker.Write([]byte("Hash of data verified.\n"))
	}
	tracker.Write([]byte("Hard resetting via RTS pin..."))
	tracker.Flush()
	if len(percents) == 0 || percents[0] != 0 || percents[len(percents)-1] != 99 {
		t.Fatal(percents)
	}
	for i, p := range percents {
		if p < 0 || p >= 100 || (i > 0 && p < percents[i-1]) {
			t.Fatal(percents)
		}
	}
	found := false
	for _, p := range percents {
		if p == 65 {
			found = true
		}
	}
	if !found {
		t.Fatal("application must contribute to overall progress", percents)
	}
	if tracker.stage != 4 || strings.Contains(output.String(), "\x1b") || !strings.Contains(output.String(), "Hard resetting") {
		t.Fatal("stream or stage parsing failed")
	}
}
func TestUploadFailureAndRetryDoNotFakeCompletion(t *testing.T) {
	var p []int
	u := &uploadOutput{Images: []uploadImage{{0x1000, 1000}}, Progress: func(n, _ int) { p = append(p, n) }}
	u.Write([]byte("Connecting...\nWriting at 0x000011f4 [==] 50%\nLost connection, retrying...\nWriting at 0x00001000 [ ] 0%\nFatal error"))
	u.Flush()
	if u.percent != 50 || u.stage != 0 {
		t.Fatal("failed upload must retain last real progress", p)
	}
}
