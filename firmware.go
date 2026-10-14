package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
)

func hotspot(n int) (string, error) {
	if n < 0 || n > 100 {
		return "", fmt.Errorf("Rover number must be a whole number from 0 to 100")
	}
	return fmt.Sprintf("MARS ROVER-%03d", n), nil
}

// Personalize only the supplied unsigned ESP32 application image. Validate its
// segment layout, XOR checksum and appended SHA-256 before touching any bytes.
func personalize(original []byte, n int) ([]byte, error) {
	name, err := hotspot(n)
	if err != nil {
		return nil, err
	}
	b := bytes.Clone(original)
	if len(b) < 24 || b[0] != 0xe9 || b[23] != 1 || b[1] == 0 {
		return nil, fmt.Errorf("unsupported ESP32 image")
	}
	p := 24
	checksum := byte(0xef)
	type span struct{ start, end int }
	var spans []span
	for i := 0; i < int(b[1]); i++ {
		if p+8 > len(b) {
			return nil, fmt.Errorf("truncated segment header")
		}
		size := uint64(binary.LittleEndian.Uint32(b[p+4 : p+8]))
		p += 8
		if size > uint64(len(b)-p) {
			return nil, fmt.Errorf("truncated segment")
		}
		end := p + int(size)
		spans = append(spans, span{p, end})
		for _, v := range b[p:end] {
			checksum ^= v
		}
		p = end
	}
	digestStart := (p/16 + 1) * 16
	if digestStart+32 != len(b) {
		return nil, fmt.Errorf("unexpected image trailer or signed image")
	}
	digest := sha256.Sum256(b[:digestStart])
	if b[digestStart-1] != checksum || !bytes.Equal(digest[:], b[digestStart:]) {
		return nil, fmt.Errorf("firmware integrity check failed")
	}
	old := []byte("MARS ROVER-026\x00")
	if bytes.Count(b[:p], old) != 1 {
		return nil, fmt.Errorf("expected exactly one original hotspot name")
	}
	idx := bytes.Index(b[:p], old)
	inside := false
	for _, s := range spans {
		if idx >= s.start && idx+len(old) <= s.end {
			inside = true
		}
	}
	if !inside {
		return nil, fmt.Errorf("hotspot is outside image data")
	}
	copy(b[idx:], []byte(name))
	checksum = 0xef
	for _, s := range spans {
		for _, v := range b[s.start:s.end] {
			checksum ^= v
		}
	}
	b[digestStart-1] = checksum
	digest = sha256.Sum256(b[:digestStart])
	copy(b[digestStart:], digest[:])
	return b, nil
}
