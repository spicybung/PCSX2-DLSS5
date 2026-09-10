# KFCSX2 — DLSS 5 Neural Rendering

KFCSX2 is a PCSX2 fork focused on Direct3D 11 DLSS 5 neural-rendering integration. Upstream PCSX2 attribution and licensing remain intact.

Run one file:

    BUILD-INSTALL-KFCSX2-DLSS5.cmd

Default source path: `C:\KFCSX2`.

Optional override:

    BUILD-INSTALL-KFCSX2-DLSS5.cmd "D:\src\KFCSX2"

The batch initializes VS2022 Build Tools, clones PCSX2 if needed, applies the D3D11 patch, builds missing PCSX2 dependencies, configures and builds Release, downloads the latest DLSS5oneclick release, and installs its generic DX11/no-native-DLSS stack into the built KFCSX2 directory.

PCSX2 imports both D3D11 and D3D12, while DLSS5oneclick prefers D3D12 when both are present. AUTO v3 therefore builds a temporary DX11-only probe executable in the PCSX2 output directory, targets that exact probe with DLSS5oneclick, then deletes it. This makes the installer stage the DX11 Feeder/ReShade stack into the correct folder without misclassifying KFCSX2 itself.

The in-source GSDLSS5NR path also checks for the DLSS5oneclick/Feeder installation and disables its experimental direct feature-18 path when the external stack is present. That prevents two neural passes from running on one frame.

After installation, choose `Direct3D 11` in KFCSX2. Home opens ReShade; enable the DLSS 5 Neural Rendering panel. F6 toggles neural rendering.

DLSS5oneclick and the NVIDIA/community runtime components are downloaded at install time and keep their own licenses/terms; they are not bundled here.
