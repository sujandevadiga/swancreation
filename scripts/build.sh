#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
go test -v ./...
mkdir -p dist
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build -buildvcs=false -trimpath -ldflags='-H=windowsgui -s -w' -o dist/MARS-Rover-Flasher-x64.exe .
