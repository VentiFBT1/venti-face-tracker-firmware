# ESP-NOW link test (tracker -> dongle)

A standalone test of whether ESP-NOW can carry face-tracker video from a tracker to a USB dongle
reliably and smoothly. It sends **dummy** frames (10 KB, 60 fps, like 240x240 MJPEG). No camera, no
video on the PC yet.

This folder is separate from the face tracker firmware (`overlay/`) and from the SlimeVR ESP-NOW
dongle firmware. Building or changing it does not affect either.

| App | Flash it on | What it does |
|---|---|---|
| `sender/` | A face tracker (ESP32-S3 N4R2) | Finds the dongle, sends 60 frames/s, prints send stats |
| `receiver/` | A spare Venti dongle (ESP32-S3FN8) | Announces itself, counts complete/broken/missing frames and stutters |

Both are built by the **ESP-NOW link test** GitHub Actions workflow. Each artifact holds one merged
`.bin`; flash it at 0x0. Afterwards, flash the normal firmware back (Babble firmware on the tracker,
SlimeVR firmware on the dongle).

## Running the test

1. Flash `receiver` on the spare dongle and plug it into the PC (or a USB extension cable near you).
2. Flash `sender` on a tracker and power it the normal way (USB power is fine).
3. Open the dongle's COM port in a serial monitor (any baud rate). It prints one line per second.
   The tracker's COM port shows the sender's side, if it's plugged into the PC too.
4. The sender cycles through 4 data rates, 20 seconds each: MCS1, MCS3, MCS5, MCS7 (13-65 Mbps).
5. Test in your real setup: tracker on your head, Virtual Desktop / Steam Link running, SlimeVR
   trackers on. Save the dongle's output from at least one full cycle (80 s).

Reading the dongle's line:
- `frames complete N/60`: frames that arrived whole this second. 60 is perfect.
- `broken`: frames where some packets were lost (would be dropped, not shown).
- `missing`: frames where nothing arrived.
- `frame gap ... stutters`: time between complete frames; a stutter is a gap over 25 ms.
- `frame arrives over X ms`: how long one frame takes on air. Shorter means the radio could sleep longer.

The channel is set in `common/espnow_test_protocol.h` (`ESPNOW_TEST_CHANNEL`, default 11).
The tracker's IR LEDs stay off during the test.
