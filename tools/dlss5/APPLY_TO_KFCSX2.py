from pathlib import Path
import re
import shutil
import sys

source_root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
package_root = Path(__file__).resolve().parents[2]

cpp_path = source_root / "kfcsx2/GS/Renderers/DX11/GSDevice11.cpp"
hdr_path = source_root / "kfcsx2/GS/Renderers/DX11/GSDevice11.h"
cmake_path = source_root / "kfcsx2/CMakeLists.txt"
dest_dir = source_root / "kfcsx2/GS/Renderers/DX11"

for required in (cpp_path, hdr_path, cmake_path):
    if not required.exists():
        raise SystemExit(f"Missing current KFCSX2 source file: {required}")

cpp = cpp_path.read_text(encoding="utf-8")
hdr = hdr_path.read_text(encoding="utf-8")
cmake = cmake_path.read_text(encoding="utf-8")

def insert_after_unique(text, anchor, insertion, description):
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"{description}: expected one anchor, found {count}")
    return text.replace(anchor, anchor + insertion, 1)

# Current PCSX2 master: GSDevice11.cpp includes GSDevice11.h directly.
if '#include "GSDLSS5NR.h"' not in cpp:
    cpp = insert_after_unique(
        cpp,
        '#include "GSDevice11.h"\n',
        '#include "GSDLSS5NR.h"\n',
        "GSDevice11.cpp include"
    )

# Insert before PCSX2's ImGui pass, leaving the emulator UI outside the neural pass.
if 'm_dlss5_nr.Process(' not in cpp:
    pattern = re.compile(
        r'(void\s+GSDevice11::EndPresent\(\)\s*\{\s*\n)(\s*RenderImGui\(\);)',
        re.MULTILINE
    )
    match = pattern.search(cpp)
    if not match:
        raise SystemExit("GSDevice11::EndPresent anchor not found")
    replacement = (
        match.group(1)
        + '\tif (m_swap_chain)\n'
          '\t\tm_dlss5_nr.Process(m_dev.get(), m_ctx.get(), m_swap_chain.get());\n\n'
        + match.group(2)
    )
    cpp = cpp[:match.start()] + replacement + cpp[match.end():]

# Header include.
if '#include "GS/Renderers/DX11/GSDLSS5NR.h"' not in hdr:
    hdr = insert_after_unique(
        hdr,
        '#include "GS/Renderers/DX11/D3D11ShaderCache.h"\n',
        '#include "GS/Renderers/DX11/GSDLSS5NR.h"\n',
        "GSDevice11.h include"
    )

# Renderer state member.
if 'GSDLSS5NR::DirectRenderer m_dlss5_nr;' not in hdr:
    hdr = insert_after_unique(
        hdr,
        '\twil::com_ptr_nothrow<ID3D11RenderTargetView> m_swap_chain_rtv;\n',
        '\tGSDLSS5NR::DirectRenderer m_dlss5_nr;\n',
        "GSDevice11.h member"
    )

# Current PCSX2 master puts all Windows DX11/DX12 sources in a single
# if(WIN32) list(APPEND pcsx2GSSources ...) block.  Do not depend on
# D3D11ShaderCache.cpp being directly adjacent to GSDevice11.cpp.
if 'GS/Renderers/DX11/GSDLSS5NR.cpp' not in cmake:
    anchor = '\t\tGS/Renderers/DX11/GSDevice11.cpp\n'
    count = cmake.count(anchor)
    if count != 1:
        raise SystemExit(f"kfcsx2/CMakeLists.txt GSDevice11.cpp anchor: expected one, found {count}")
    cmake = cmake.replace(
        anchor,
        '\t\tGS/Renderers/DX11/GSDLSS5NR.cpp\n' + anchor,
        1
    )

if 'GS/Renderers/DX11/GSDLSS5NR.h' not in cmake:
    anchor = '\t\tGS/Renderers/DX11/GSDevice11.h\n'
    count = cmake.count(anchor)
    if count != 1:
        raise SystemExit(f"kfcsx2/CMakeLists.txt GSDevice11.h anchor: expected one, found {count}")
    cmake = cmake.replace(
        anchor,
        '\t\tGS/Renderers/DX11/GSDLSS5NR.h\n' + anchor,
        1
    )

cpp_path.write_text(cpp, encoding="utf-8")
hdr_path.write_text(hdr, encoding="utf-8")
cmake_path.write_text(cmake, encoding="utf-8")

for name in ("GSDLSS5NR.cpp", "GSDLSS5NR.h"):
    src = package_root / "kfcsx2/GS/Renderers/DX11" / name
    dest = dest_dir / name

    if not src.exists():
        raise SystemExit(f"Package file missing: {src}")

    # When this integration package is extracted directly into the PCSX2
    # source root, package_root and source_root are identical. In that case
    # src and dest are the same file; Windows CopyFile2 reports WinError 32.
    # The file is already in the correct destination, so do not copy it.
    if src.resolve() == dest.resolve():
        print(f"Already in place: {dest}")
        continue

    shutil.copy2(src, dest)

print("Applied direct DLSS 5 Neural Rendering integration to KFCSX2 D3D11.")
print("Next: configure/build KFCSX2, then place the runtime DLLs beside KFCSX2.exe.")
