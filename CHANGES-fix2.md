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

Wi-Fi streaming code is unchanged. Version string: 0.2.1rc0-venti-fix2.
