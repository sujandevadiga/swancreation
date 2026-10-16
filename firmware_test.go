package main

import (
	"bytes"
	"crypto/sha256"
	"os"
	"testing"
)

func TestAllRoverNumbers(t *testing.T) {
	original, e := os.ReadFile("assets/MarsRover.ino.bin")
	if e != nil {
		t.Fatal(e)
	}
	for n := 0; n <= 100; n++ {
		b, e := personalize(original, n)
		if e != nil {
			t.Fatalf("%d: %v", n, e)
		}
		name, _ := hotspot(n)
		if !bytes.Contains(b, append([]byte(name), 0)) {
			t.Fatal(name)
		}
		sum := sha256.Sum256(b[:len(b)-32])
		if !bytes.Equal(sum[:], b[len(b)-32:]) {
			t.Fatal("digest")
		}
		// Differences may occur only in the three SSID digits, XOR checksum and digest.
		idx := bytes.Index(original, []byte("MarsRover - 001"))
		for i := range b {
			if b[i] != original[i] && !(i >= idx+12 && i < idx+15) && i != len(b)-33 && i < len(b)-32 {
				t.Fatalf("unexpected change at %d", i)
			}
		}
	}
	for _, n := range []int{-1, 101} {
		if _, e := personalize(original, n); e == nil {
			t.Fatal("accepted invalid number")
		}
	}
	bad := bytes.Clone(original)
	bad[100] ^= 1
	if _, e := personalize(bad, 0); e == nil {
		t.Fatal("accepted corruption")
	}
	if _, e := personalize(original[:100], 0); e == nil {
		t.Fatal("accepted truncation")
	}
}
