# fix2: smoother Wi-Fi stream (on top of fix1)

Reported: small stutter / lag on the Quest with fix1.

1. Wi-Fi modem sleep off (`esp_wifi_set_ps(WIFI_PS_NONE)` after getting an IP, in wifiManager.cpp).
   The ESP-IDF default lets the radio doze between router beacons, which delays TCP ACKs and causes uneven frame delivery.
   Costs a little power while connected; the radio is mostly busy streaming anyway.
2. Stream no longer ends after one failed frame grab (StreamServer.cpp). Upstream set `response = ESP_FAIL`,
   which stopped sending and closed the stream, forcing the client to reconnect.
3. Three camera frame buffers instead of two (CameraManager.cpp), still `CAMERA_GRAB_LATEST`,
   so the camera always has a free buffer while a frame is being sent. No added latency.
4. TCP send buffer 5760 -> 16384 bytes (board file), so a whole JPEG frame can be in flight without waiting on ACKs.

Unchanged from fix1: 15 MHz XCLK / 45 fps, IR LED PWM on its own timer, sensor settings.
Note: exposure value 300 gives about 33% longer exposure at 15 MHz than at 20 MHz (more motion blur).
