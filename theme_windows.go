//go:build windows

package main

import (
	"os"
	"path/filepath"
	"unsafe"
)

// Activate Windows' built-in modern common controls for this UI thread. This
// needs neither an installer nor an external runtime. Fall back to standard
// native controls if Windows cannot create the activation context.
func activateVisualStyles() func() {
	empty := func() {}
	dir, err := os.MkdirTemp("", "mars-ui-")
	if err != nil {
		return empty
	}
	path := filepath.Join(dir, "controls.manifest")
	const manifest = `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
<assemblyIdentity version="1.0.0.0" processorArchitecture="amd64" name="MARS.Rover.Flasher" type="win32"/>
<dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="amd64" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency>
</assembly>`
	if os.WriteFile(path, []byte(manifest), 0600) != nil {
		os.RemoveAll(dir)
		return empty
	}
	type context struct {
		Size, Flags                      uint32
		Source                           *uint16
		Architecture, Language           uint16
		Directory, Resource, Application *uint16
		Module                           uintptr
	}
	c := context{Source: wide(path)}
	c.Size = uint32(unsafe.Sizeof(c))
	handle, _, _ := kernel.NewProc("CreateActCtxW").Call(uintptr(unsafe.Pointer(&c)))
	if handle == ^uintptr(0) {
		os.RemoveAll(dir)
		return empty
	}
	var cookie uintptr
	ok, _, _ := kernel.NewProc("ActivateActCtx").Call(handle, uintptr(unsafe.Pointer(&cookie)))
	if ok == 0 {
		kernel.NewProc("ReleaseActCtx").Call(handle)
		os.RemoveAll(dir)
		return empty
	}
	return func() {
		kernel.NewProc("DeactivateActCtx").Call(0, cookie)
		kernel.NewProc("ReleaseActCtx").Call(handle)
		os.RemoveAll(dir)
	}
}
