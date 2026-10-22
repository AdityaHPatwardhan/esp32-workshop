# ESP32-S3 Workshop: Fundamentals + TinyML

4-hour hands-on workshop. Participants flash **pre-built** firmware from the browser
(ESP Launchpad); nobody compiles anything on the day.

| Path | What |
|---|---|
| `slides/` | The talk, built with **reveal.js** (vendored, works offline). Open `slides/index.html` in Chrome. Sessions go left → right, each session's slides go down. `Space` next (diagrams reveal step by step), `S` speaker view with notes + timer, `O` overview, `F` fullscreen, `B` blank. Names/links in `config.js`, behaviour in `deck.js` (interactive neuron + quantization, live dashboard embed), styles in `deck.css` |
| `docs/talk-notes.md` | Presenter script: timings, what to say, questions to ask, demos |
| `firmware/01_blink` | **Hello Board**: prints a board report (chip, PSRAM, where things live in memory, partitions, camera model + brightness, I2S mic level, Wi-Fi scan, FreeRTOS tasks, RAM vs flash restart counters), then blinks the LED; BOOT restarts. Mic probe needs the board's mic pins (on for S3-EYE) |
| `firmware/03_person_detector` | Camera + TFLite Micro person detection → UDP broadcast with team name |
| `binaries/` | Built output (`tools/build_all.sh`): per-part bins for `flash.py`, `*-merged.bin` for Launchpad |
| `tools/dashboard/dashboard.py` | Projector dashboard (Python stdlib only): team tiles with **live camera video** + a big spotlight view. `--demo` to rehearse without boards, `--no-video` for status only |
| `tools/flash.py` | CLI fallback: flash + write the team name into NVS in one command |
| `tools/snap_to_png.py` | "What the AI sees": saves the 96×96 frame the model just used (console command `snap`) |
| `tools/launchpad/` | Launchpad TOML configs + hosting instructions + participant handout |

## Build

```bash
source ~/esp/esp-idf/export.sh
./tools/build_all.sh
```

Before building for the real boards, set the Wi-Fi name/password the boards should join
in `firmware/03_person_detector/main/Kconfig.projbuild` (default `esp-workshop` /
`esp32workshop`). It can also be changed on the day without a rebuild (`wifi <ssid> <pass>`
in the console, or `flash.py --ssid`).

### ESP32-S3-EYE

Tested end-to-end on an ESP32-S3-EYE (LED GPIO3, native-USB console, 8 MB PSRAM, OV2640):
`BOARD=s3eye ./tools/build_all.sh` → `binaries-s3eye/`. Inference ≈ 60 ms/frame; person
score 80–95 % with someone in view, 8–40 % without; UDP 1 packet/s + instant on change.
Live video (96×96, what the model sees) reaches the dashboard at ~6–7 fps over a phone hotspot.

### How the live video works

The board can't afford a second camera mode, so it streams exactly the 96×96 frame the
model uses. The dashboard sends `watch` to each board's UDP port 5006 once a second; the
board pushes each new frame back as a single UDP packet (`WSF1` + frame number + ~1.4 KB JPEG).
UDP on purpose: phone hotspots are lossy (we measured 30–60 % ping loss), and a TCP/HTTP
MJPEG stream stalled at 2 fps there, while UDP just skips lost frames. Each board also
serves `http://<board-ip>/stream` and `/frame.jpg` for viewing it directly on a good network.

## Rehearsal checklist (do this with ONE board before buying/flashing all)

The board's datasheet is unclear, so these must be confirmed on real hardware:

- [ ] **USB type.** Flash `01_blink`, open the console, type something into the person
      detector's console. If input does nothing, the USB-C goes to native USB: use the
      `03_person_detector-usbconsole` build.
- [ ] **LED pin.** Blink defaults to GPIO2. No blink? Find the LED pin, change
      `BLINK_GPIO` / `WORKSHOP_LED_GPIO` (`idf.py menuconfig`), rebuild.
- [ ] **PSRAM / camera / mic.** Flash `01_blink`: its board report prints the PSRAM size, the camera model and whether a mic answers. Everything works without it.
      If it says "none found" but the module is marked e.g. `N8R2`, it has quad PSRAM:
      change `CONFIG_SPIRAM_MODE_OCT` to `CONFIG_SPIRAM_MODE_QUAD`.
- [ ] **Camera pins.** Person detector defaults to the ESP32-S3-EYE pinout (used by most
      S3-CAM boards). `Camera init failed` → get the pinout from the seller, set it in
      menuconfig → *Workshop: Person Detector → Camera pins*.
- [ ] **Inference time.** Note the `inference NN ms` value, for the talk.
- [ ] **Network.** Phone hotspot on 2.4 GHz (iPhone: *Maximize Compatibility*), 2 boards +
      Mac, run `python3 tools/dashboard/dashboard.py`. Both teams appear? Broadcast works.
      Nothing? macOS *Privacy & Security → Local Network* → allow Terminal.
- [ ] **Launchpad.** Publish (see `tools/launchpad/README.md`), flash a board from the link,
      type `team Test` in Minimal Launchpad's console box.
- [ ] **Real boot log.** Capture it from your board for the slides (`docs/talk-notes.md`).

## Day-of

- Hotspot on, Mac joined, `python3 tools/dashboard/dashboard.py` on the projector
  (split screen: dashboard on one half, your board's console/camera demo on the other).
- Spare boards (pre-flashed), data cables, a laptop with `flash.py` ready.
- Phone hotspots allow ~10 devices: with more teams, use a travel router (repeater mode).

## Image credits (slides/img)

- `esp32-s3-wroom-1.jpg`, `esp32-s3-devkitc.jpg`: Espressif, from the esp-dev-kits ESP32-S3-DevKitC-1 docs
- `esp32-s3-devkit.jpg`: "ESP32-S3 on paper" by VectorVoyager, CC BY-SA 4.0, via Wikimedia Commons
- `esp32-s3-eye*.{png,jpg}`: Espressif, from the ESP-WHO ESP32-S3-EYE Getting Started Guide
- `ai-sees.png`, `dashboard.png`: captured from our own board and dashboard
