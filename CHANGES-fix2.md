# fix2: USB (UVC) stutter fix (on top of fix1)

Reported: small stutter / lag over USB with fix1 at 45 fps.

Cause: two frame pacers fought each other in UVC mode.
- `usb_device_uvc` already asks for a frame once per host-negotiated interval, rounded down to whole
  milliseconds (22 ms at 45 fps, 16 ms at 60 fps).
- `UVCStream.cpp` added its own exact pacer (22.222 ms). Every so often a request came "too early", the callback
  returned no frame, and the UVC task skipped that whole slot. The result is a double-length frame gap:
  roughly once every 1-2 seconds at 45 fps, and several times a second at 60 fps (so test-G had it too).

Fix: removed the extra pacer in `camera_fb_get_cb()`. The in-flight guard stays. `esp_camera_fb_get()`
(still `CAMERA_GRAB_LATEST`, 2 buffers) waits for the next sensor frame, so frames go out evenly at the sensor's rate.

Wi-Fi streaming code is unchanged.

Version string is reported as plain `0.2.1rc0`, the same as test-G. A suffix like `0.2.1rc0-venti-fix2` made
Babalonia crash when detecting the tracker (test-G, which reports `0.2.1rc0`, is detected fine).
To tell builds apart, use the commit id in the .bin file name instead.

## Back to 60 fps (20 MHz XCLK)

Power measured on one tracker with a USB tester (Babalonia connected, single readings):

| Build | USB | Wi-Fi |
|---|---|---|
| test-G (60 fps, 20 MHz) | 0.50 W | 0.79 W |
| fix2 IR 30% (45 fps, 15 MHz) | 0.46 W | - |
| fix2 IR 50% (45 fps, 15 MHz) | 0.50 W | 0.79 W |

45 fps / 15 MHz saved no measurable power on this (non-faulty) unit, while adding latency and stutter.
So the camera is back to test-G's clocks and frame rate: USB XCLK 20 MHz, Wi-Fi XCLK 16.5 MHz, UVC 60 fps,
X-Framerate 60. Kept: IR LED on its own LEDC timer (fix1), USB pacer fix, IR brightness builds, version 0.2.1rc0.
Exposure is back to test-G behaviour (the 33% longer exposure at 15 MHz is gone).
