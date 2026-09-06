from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: CHECK_PCSX2_DEPS.py <deps-dir>")

root = Path(sys.argv[1]).resolve()
if not root.exists():
    print(f"Dependency prefix does not exist: {root}")
    raise SystemExit(1)

missing = []

required_exact = [
    root / "bin" / "Qt6Core.dll",
    root / "bin" / "SDL3.dll",
    root / "lib" / "libpng16.lib",
    root / "lib" / "z.lib",
    root / "include" / "shaderc" / "shaderc.h",
]

for p in required_exact:
    if not p.exists():
        missing.append(str(p.relative_to(root)))

# Cache the CMake filenames once, case-insensitively.
cmake_names = {p.name.lower() for p in root.rglob("*.cmake")}

def require_any(label, names):
    if not any(name.lower() in cmake_names for name in names):
        missing.append(label)

require_any("Qt6 package config", [
    "Qt6Config.cmake",
])
require_any("PlutoVG package config", [
    "plutovgConfig.cmake",
    "plutovg-config.cmake",
])
require_any("PlutoSVG package config", [
    "plutosvgConfig.cmake",
    "plutosvg-config.cmake",
])
require_any("RapidYAML/ryml package config", [
    "rymlConfig.cmake",
    "ryml-config.cmake",
])

# Microsoft DirectX-Headers installs the config with a lowercase/hyphenated
# filename: directx-headers-config.cmake.  CMake accepts that as the normal
# <lowercase-package>-config.cmake form for find_package(DirectX-Headers).
require_any("DirectX-Headers package config", [
    "directx-headers-config.cmake",
    "DirectX-HeadersConfig.cmake",
])

# PCSX2 uses its own FindShaderc.cmake, so shadercConfig.cmake is NOT required.
# The finder wants shaderc/shaderc.h plus a shaderc_shared import library.
shaderc_libs = (
    list((root / "lib").glob("shaderc_shared*.lib"))
    if (root / "lib").exists()
    else []
)
if not shaderc_libs:
    missing.append("shaderc_shared*.lib")

# KDDockWidgets' config name/layout has changed between releases.
kddock_found = any(
    "kddockwidgets" in name and name.endswith("config.cmake")
    for name in cmake_names
)
if not kddock_found:
    missing.append("KDDockWidgets package config")

if missing:
    print("KFCSX2 dependency prefix is incomplete. Missing:")
    for item in missing:
        print(f"  - {item}")
    raise SystemExit(1)

print("KFCSX2 dependency prefix passes the current Windows dependency checks.")
raise SystemExit(0)
