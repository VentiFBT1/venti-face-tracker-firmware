# Venti face tracker firmware: fix1 (based on the test-G source reconstruction)

Status: source changes only. Not compiled or hardware-tested yet.
Build it, flash one spare tracker, and compare it against the original test-G BIN before shipping.

## Changes

1. IR LED PWM no longer shares the camera clock hardware (main fix)
   - The camera driver generates XCLK (the camera's master clock, GPIO4) with LEDC timer 0 / channel 0.
   - The IR LED driver (LEDManager, GPIO1 = IR_CTRL) used the same timer 0 / channel 0.
   - The LED was set up first, then camera init reconfigured timer 0 to 20 MHz and re-pointed channel 0 to GPIO4.
     GPIO1 stayed connected to channel 0, so the IR MOSFET gate (Q1, AO3400) received the 20 MHz camera clock.
   - The result is a MOSFET that is only partly on. IR brightness depends on each MOSFET's threshold voltage,
     which varies from unit to unit. The MOSFET dissipates heat, and a 20 MHz signal runs along the IR trace
     next to the camera lines.
   - The LED duty-cycle setting at boot was ignored. A duty change at runtime (from the app or API) wrote into
     the camera clock channel instead.
   - Fix: the IR LED now uses LEDC timer 1 / channel 1 with the APB clock source (the same source as XCLK).

2. Lower camera clock for about 45 fps
   - CONFIG_CAMERA_USB_XCLK_FREQ: 20000000 -> 15000000. UVC-capable builds always use this value, in Wi-Fi mode too.
   - CONFIG_CAMERA_WIFI_XCLK_FREQ: 16500000 -> 15000000, for consistency.
   - The OV3660 frame rate scales with XCLK, so expect roughly 45 fps at 240x240. Check the stream log line that
     reports fps.
   - UVC pacing and descriptors: 60 -> 45 fps. The X-Framerate header in Wi-Fi mode is now 45.

3. IR LED PWM settings
   - PWM frequency: 5 kHz -> 20 kHz.
   - Default duty stays at 30%, the default Venti asked upstream to use. Because of the timer conflict, that 30% never
     actually applied before; with the fix it does. Trackers keep any duty saved in NVS ("led_ext_pwm").
   - Check IR brightness on the image after flashing and adjust it in the app. The setting now works.

4. OV3660 only
   - Removed the upstream OV2640 bank and clock register writes (0xFF, 0xD3) from setupCameraSensor().
     They were meant for the OV2640, and on the OV3660 they wrote to unrelated 16-bit register addresses.
     Venti hardware uses only the OV3660.

## Build (ESP-IDF v5.4.2, from the assembled tree; see README)

    python tools/switchBoardType.py --board venti_babble_n4r2 --clear-wifi
    idf.py build
    idf.py merge-bin -f raw

Output: build/merged-binary.bin. In this repo, tools/build-local.ps1 does all of this on Windows,
and the "Build firmware" GitHub Actions workflow builds it on every push to main.

## Test plan (one spare tracker first)

1. Flash fix1 and erase NVS (or set the LED duty in the app once). Set the Wi-Fi credentials and mode.
2. On the Quest over Wi-Fi for 20 minutes: check the fps log, the black-line or striped frames, and the board temperature.
3. Compare against the original test-G BIN on the same tracker and network.
4. Check IR brightness. The image should be well lit, not blown out.
5. Test once on a unit that a customer reported as glitchy, if you have one back.
