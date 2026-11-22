# Venti Face Tracker firmware

Firmware for the Venti face tracker: ESP32-S3-WROOM-1U-N4R2 with an OV3660 camera.
It is based on [OpenIris-ESPIDF](https://github.com/EyeTrackVR/OpenIris-ESPIDF) by EyeTrackVR.

All work happens on the `main` branch of this repository.

## How this repo is organized

This repo stores only the files Venti changed. A build downloads the pinned upstream commit and copies the Venti files over it.

| Path | What it is |
|---|---|
| `upstream.env` | Pinned upstream repo and commit, plus the board profile to build |
| `overlay/` | Venti's changed and added files, at the same paths as in OpenIris-ESPIDF |
| `tools/assemble.sh` | Downloads upstream, applies the overlay and selects the board, all into `work/` |
| `tools/build-local.ps1` | Builds the same way on Windows with ESP-IDF v5.4.2 |
| `.github/workflows/build.yml` | Builds on every push to `main` and uploads the `.bin` as an artifact |
| `CHANGES-fix1.md`, `CHANGES-fix2.md` | What each fix changes and why, plus the test plan |
| `recovery/` | Notes from recovering the test-G source |

### Change a file

1. If the file is already in `overlay/`, edit it there.
2. If it isn't, copy it from upstream at the pinned commit into `overlay/` at the same path, then edit it.
3. Commit to `main`. The workflow builds the firmware automatically.

### Get the firmware

**Actions → Build firmware → latest run → Artifacts.** There is one artifact per IR LED brightness:
`venti-face-tracker-firmware-ir30`, `-ir50`, `-ir75` and `-ir100` (PWM duty %). Each holds a single merged `.bin`.
Flash it at offset 0x0 with the Venti Face Tracker Flasher.

IR brightness can only be set in firmware. Flashing the full merged image also wipes the saved settings area (NVS),
so the build's brightness always takes effect, and Wi-Fi has to be set again. To change the list of brightness
builds, edit `ir_duty` in `.github/workflows/build.yml`. The board file's default (30%) is used by local builds.

### Build locally (Windows, ESP-IDF v5.4.2 terminal)

    powershell -ExecutionPolicy Bypass -File tools\build-local.ps1

## Status

fix1 has **not been hardware-tested yet**. Before shipping, compare it on a spare tracker against the original
test-G build (`venti-n4r2-test-G-original-defaults-60fps-latest-frame.bin`, which is kept outside this repo).
The test steps are in `CHANGES-fix1.md`.

The base source is a reconstruction of test G. The original test-G source tree was not recovered,
so details are in `recovery/manifest.json`.

## License

At the pinned commit, OpenIris-ESPIDF does not include a license file. The files in `overlay/` are modified
upstream files, so check EyeTrackVR's licensing terms before redistributing builds commercially.
