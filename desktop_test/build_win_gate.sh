#!/bin/sh
# Windows port gate. Build only architectures configured by package.json (or
# the build script's defaults), then validate every executable that was
# actually produced. Optional toolchains may be skipped by build-windows.sh;
# this gate must not assume x64+ARM64 when a project selects another matrix.
set -eu
cd "$(dirname "$0")/.."

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "SKIP: x86_64-w64-mingw32-gcc not installed (Windows gate not run)"
    exit 0
fi

bash sources/build-windows.sh >/tmp/cwv-windows-build-gate.log 2>&1 || {
    cat /tmp/cwv-windows-build-gate.log
    exit 1
}
tail -8 /tmp/cwv-windows-build-gate.log

python3 - <<'PY2'
import glob, os, struct, sys
machines = {"x86": 0x014c, "x64": 0x8664,
            "arm32": 0x01c4, "arm64": 0xaa64}
paths = sorted(glob.glob("build/windows/*/CWebView.exe"))
if not paths:
    print("FAIL: Windows build produced no CWebView.exe files")
    sys.exit(1)
for path in paths:
    arch = path.split(os.sep)[2]
    if arch not in machines:
        print("FAIL: unknown output architecture directory:", path)
        sys.exit(1)
    with open(path, "rb") as f:
        data = f.read(4096)
    if len(data) < 64 or data[:2] != b"MZ":
        print("FAIL: invalid PE executable:", path)
        sys.exit(1)
    off = struct.unpack_from("<I", data, 0x3c)[0]
    if off + 6 > len(data) or data[off:off+4] != b"PE\0\0":
        print("FAIL: missing PE header:", path)
        sys.exit(1)
    machine = struct.unpack_from("<H", data, off + 4)[0]
    if machine != machines[arch]:
        print("FAIL: %s machine 0x%04x != expected 0x%04x" %
              (path, machine, machines[arch]))
        sys.exit(1)
    print("PASS %s (machine 0x%04x)" % (path, machine))
print("WIN GATE PASSED (all produced architecture binaries match their targets)")
PY2
