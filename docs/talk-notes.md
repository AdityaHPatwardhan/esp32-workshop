# Talk Notes: ESP32-S3 Fundamentals + TinyML (4 hours)

Presenter script. **Say** = what to explain, **Ask** = questions for the room
(answers in italics), **Do** = live demo / hands-on.

| Time | Block | Slides (`slides/index.html`) |
|---|---|---|
| 0:00–0:10 | Intro, teams, open the Launchpad link | 1–4 |
| 0:10–0:30 | Part 1 · Hands-on 1: Blink | 5–9 |
| 0:30–1:20 | Part 2 · Inside the chip, read from the boot log (+ the board report from Blink) | 10–19 |
| 1:20–1:30 | Break | none |
| 1:30–2:05 | Part 3 · Machine Learning on ESP32: neurons, training, quantization | 20–30 |
| 2:05–2:45 | Part 4 · Hands-on 3: person detector + live dashboard games | 31–36 |
| 2:45–2:55 | Break (participants install the ESP RainMaker app) | none |
| 2:55–3:50 | Part 5 · Device / phone / cloud, then Hands-on 4: RainMaker switch | 37–44 |
| 3:50–4:00 | Recap, what next, Q&A | 45–47 |

The slide deck has speaker notes on every slide (press `S` in the reveal.js deck for the speaker view).
Slide 33 embeds the live dashboard if `dashboard.py` is running on the same laptop.
This file is the longer script behind them.

---|---|
| 0:00–0:15 | Intro, teams, open the Launchpad link |
| 0:15–0:35 | Hands-on 1: Blink |
| 0:35–1:50 | Fundamentals, read from the boot log and the board report |
| 1:50–2:05 | Break |
| 2:05–3:35 | TinyML: person detection + live dashboard |
| 3:35–4:00 | Q&A, industry talk, what next |

---

## 0:00 – Intro (15 min)

**Say**
- Who you are, what you do with ESP32 at work. One story of a real product.
- Today's promise: *"By lunch you'll know what happens inside the chip from the moment
  you plug it in, and you'll have a neural network running on it, live on that screen."*
- Teams of 3–4, one board per team. Pick a team name now (needed later, max 24 chars,
  letters/digits/space/`-_.`).

**Do**
- Show the QR code / link for ESP Launchpad. Chrome or Edge only.
- Everyone plugs in the board. Check: does a serial port show up? (Data cable, not charge-only.)

---

## 0:15 – Hands-on 1: Blink (20 min)

**Do**
1. Launchpad link (on the slide) → Connect → *1-Hello-Board* → Flash. Then `team <name>` in the Console.
2. LED blinks. Open the console: the **board report** (chip, PSRAM, memory, partitions, camera, mic, Wi-Fi networks, tasks, counters), then `blink #1, #2…`.
3. **Press RST and watch the console.** Tell them:
   > "Everything I'll explain in the next hour is in these 40 lines."

**Ask**: *"What did we just do when we clicked Flash?"*
→ *Wrote bytes into the external flash chip over USB. The chip itself did the rest.*

---

## 0:35 – Fundamentals, read from the boot log (75 min)

Real log captured from an ESP32-S3-EYE (full version in `docs/boot-log-s3eye.txt`).
Put it on a slide:

```
ESP-ROM:esp32s3-20210327                                          ① ROM
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2a (SPI_FAST_FLASH_BOOT)
load:0x3fce2820,len:0x1464
load:0x403c8700,len:0xd14
load:0x403cb700,len:0x2e48
entry 0x403c88e4
I (24) boot: ESP-IDF v6.2 2nd stage bootloader                    ② 2nd-stage bootloader
I (25) boot.esp32s3: SPI Flash Size : 4MB
I (26) boot: Partition Table:
I (26) boot:  0 nvs       WiFi data    01 02 00009000 00006000
I (26) boot:  1 phy_init  RF data      01 01 0000f000 00001000
I (27) boot:  2 factory   factory app  00 00 00010000 00100000
I (28) esp_image: segment 0: paddr=00010020 vaddr=3c020020 size=0ad00h ( 44288) map
I (36) esp_image: segment 1: paddr=0001ad28 vaddr=3fc90b00 size=03234h ( 12852) load
I (39) esp_image: segment 2: paddr=0001df64 vaddr=40374000 size=020b4h (  8372) load
I (42) esp_image: segment 3: paddr=00020020 vaddr=42000020 size=15304h ( 86788) map
I (74) boot: Loaded app from partition at offset 0x10000
I (75) cpu_start: Multicore app                                    ③ App startup
I (84) cpu_start: cpu freq: 240000000 Hz
I (86) heap_init: At 3FC947C0 len 00054F50 (339 KiB): RAM
I (90) main_task: Started on CPU0
I (90) main_task: Calling app_main()                               ④ Your code
  Hello World from esp32s3!
```

### First, the map of the chip (10 min)

Draw this (whiteboard or slide). Inside the chip vs. outside:

```
 ┌──────────────── ESP32-S3 chip ────────────────┐      outside, on the module
 │  CPU0  CPU1  (Xtensa LX7, 240 MHz, vector ext) │
 │  ROM  ~384 KB   (boot code, fixed forever)     │ ◄─SPI─►  FLASH  (4–16 MB)
 │  SRAM ~512 KB   (fast, lost at power-off)      │          code + data, survives power-off
 │  RTC memory     (survives deep sleep)          │ ◄─SPI─►  PSRAM  (0/2/8 MB)
 │  Cache + MMU    (makes flash/PSRAM look like   │          extra RAM, slower than SRAM
 │                  normal memory to the CPU)     │
 │  Peripherals: GPIO, UART, SPI, I2C, I2S, ADC,  │ ◄─pins─► LED, camera, sensors...
 │  PWM, RMT, timers, touch, camera, USB, Wi-Fi,  │
 │  BLE   (all reachable via the GPIO matrix)     │
 └────────────────────────────────────────────────┘
```

**Ask**: *"Where is our blink program stored right now?"* → *In the external flash chip.*
**Ask**: *"If it's outside the chip, how does the CPU run it?"* → *Hold that thought. That's the cache/MMU, step ②.*

### ① Power on: the ROM wakes up (15 min)

**Say**
- Power on → reset → CPU0 starts executing at a fixed address **inside ROM**.
  ROM is mask ROM: written when the silicon is made, never changes. It can't be bricked.
- ROM reads the **strapping pins** (GPIO0 especially) at the moment of reset:
  - GPIO0 high (default, pulled up) → `SPI_FAST_FLASH_BOOT`: boot from flash.
    (`rst:0x15 USB_UART_CHIP_RESET` = the PC reset the chip over USB; unplug/replug shows `POWERON`.)
  - GPIO0 low (BOOT button held) → **download mode**: wait for new firmware over UART/USB.
- In normal boot, ROM reads the **2nd-stage bootloader** from flash offset 0x0
  into internal RAM (those `load:` lines) and jumps to it (`entry 0x403c...`).

**Ask**: *"Why do we hold BOOT to flash?"*
→ *It holds GPIO0 low during reset, so the ROM goes to download mode instead of running the old program.*

**Do**: hold BOOT, tap RST, release. Log shows `boot:0x0 (DOWNLOAD(USB/UART0))` and stops. Tap RST again → normal boot.

**Ask**: *"Why doesn't the ROM just start our app directly?"*
→ *ROM is fixed forever. Anything that needs to change (partition layout, OTA, security) lives in the updatable 2nd stage.*

### ② The 2nd-stage bootloader (15 min)

**Say**
- Lives in flash, so it can be updated and configured.
- Reads the **partition table** (at 0x8000): flash is split into named regions.
  Show the table in the log: `nvs` (settings), `phy_init` (radio calibration), `factory` (the app).
  Real products add `ota_0` / `ota_1`: two app slots, so an update can never brick the device.
- Picks which app to boot, checks it isn't corrupt (checksum / hash; secure boot would also check a signature).
- **Sets up the cache + MMU.** Look at the `esp_image: segment` lines:
  - Read the `vaddr` column: `3c..` = flash data, `3fc9..` = internal DRAM,
    `4037..` = internal IRAM, `42..` = flash code. Same ranges the Blink board report prints.
  - `map`: this part stays in flash and is *mapped* into the CPU's address space.
    Code runs straight out of flash through the cache (**XIP, execute in place**).
  - `load`: this part is *copied* into internal RAM (data, and time-critical code).
- Jumps to the app's entry point.

**Ask**: *"Our app is 200 KB but the chip has 512 KB RAM, and a big app could be 3 MB. How?"*
→ *Code isn't copied. It's read from flash on demand through the cache, like a CPU reading from disk via a small fast buffer.*

### ③ App startup (10 min)

**Say**
- `cpu_start`: brings up CPU1 too (dual core), sets clocks.
- `heap_init`: hands all leftover RAM regions to `malloc()` (+ PSRAM if found).
- Starts **FreeRTOS**, a small real-time OS. Creates the `main` task and calls `app_main()`.

### ④ How does the LED actually blink? (10 min)

Show `firmware/01_blink/main/blink_main.c`.

**Say**
- `gpio_set_direction()` → configures the pin's mux and output-enable registers.
- `gpio_set_level(pin, 1)` → writes 1 bit to a **register**: a special memory address
  wired to hardware. `GPIO_OUT_W1TS_REG` = "write 1 to set". The pin goes to 3.3 V, the LED lights.
- Key insight: **controlling hardware = reading and writing special memory addresses.**
  The blink firmware has a `BLINK_USE_REGISTERS` option that does exactly that with `REG_WRITE`, no driver call.
- `vTaskDelay(250 ms)` is not a busy loop: the task sleeps and FreeRTOS gives the CPU to
  other tasks or idles it. That's why the chip can do Wi-Fi at the same time.

**Ask**: *"What's the difference between `vTaskDelay` and a `for` loop counting to a million?"*
→ *The loop burns CPU and blocks everything else; the delay lets other tasks run. The watchdog would also eventually reset a stuck loop.*

### Read your board report (10 min)

The Blink firmware prints a report before it starts blinking. Walk through it:
1. **CHIP**: cores, flash, PSRAM, MAC (unique to every chip), last reset reason.
2. **WHERE THINGS LIVE**: `const` string → FLASH, global/local/malloc → internal RAM,
   function → FLASH, `memcpy` → ROM (some C library functions are baked into the chip).
3. **FLASH PARTITIONS**: the same table as the boot log, read by the app itself.
4. **CAMERA**: model (OV2640) found over I²C, plus average brightness. Cover the lens, press RST, compare.
5. **MICROPHONE**: found over I²S, with a sound level. Clap, press RST, compare.
   (Generic S3-CAM build: "Skipped" until the mic pins are set.)
6. **WI-FI SCAN**: networks nearby with signal strength: the radio works.
7. **TASKS**: `main`, `IDLE0/1`, `wifi`, `tcpip`, `esp_timer`…: an OS is running even in "Blink".
8. **RESTART COUNTERS**: press BOOT (restart), then unplug/replug:

   | | RAM counter | Flash counter |
   |---|---|---|
   | restart | back to 1 | keeps counting |
   | power off | back to 1 | keeps counting |

**Ask**: *"Where should a device keep its Wi-Fi password?"* → *flash (nvs): RAM is wiped at every reset.*

### Peripherals and sensors (wrap-up, 5 min)

**Say**
- Sensors are just peripherals talking a protocol:
  I²C (2 wires, many devices: IMU, temperature), SPI (fast: displays, SD, flash/PSRAM),
  I²S (audio: microphones), ADC (analog: potentiometer, LDR), camera interface (parallel 8-bit DVP).
- The **GPIO matrix** lets almost any peripheral use almost any pin.
- DMA moves data (like camera frames) into RAM without the CPU. That's next.

---

## 1:50 – Break (15 min)

Before the break, start the dashboard and put it on the projector so people come back to it:
`python3 tools/dashboard/dashboard.py`

---

## 2:05 – TinyML block (90 min)

### What and why (15 min)

**Say**
- TinyML = running a trained neural network **on the microcontroller**, not in the cloud.
- Why: **privacy** (images never leave the device), **no internet needed**, **instant**,
  **cheap** (a ₹500 board), **low power** (a battery-powered chip).
- Real products: wake words ("Hey Siri" detection runs on a tiny always-on chip),
  people counters, predictive maintenance (vibration anomaly), gesture remotes.
- How it's made:
  1. Collect + label data (here: the *Visual Wake Words* dataset, from COCO: "person / no person").
  2. Train a small CNN (MobileNet) on a PC/GPU.
  3. **Quantize** float32 → int8: 4× smaller, much faster, tiny accuracy loss.
  4. Convert to a `.tflite` file → a C array compiled into the firmware.
  5. Run it with **TensorFlow Lite Micro** (Espressif's `esp-tflite-micro`, accelerated by **ESP-NN**
     using the S3's vector instructions).

### Hands-on 3: Flash the Person Detector (20 min)

**Do**
1. Launchpad person-detector link → Flash.
2. In the console command box: `team <your team name>` → Enter.
   *(Fallback: `python3 tools/flash.py --team "Rockets"` on a laptop with Python.)*
3. Point the camera at a teammate. LED lights, console prints `>>> PERSON detected`.
4. Look at the projector: your team's tile appears.

**Say**: the board joined your Wi-Fi and broadcasts one small JSON message per second,
`{"team":"Rockets","person":true,"score":87,"seen":3,"infer_ms":...}`, to every device on the
network. The dashboard just listens. **Only numbers leave the board, never the picture.**

### Live dashboard games (20 min)

- **Everyone online**: first team to appear gets a cheer.
- **Hide!**: all cameras must show "no person". Who gets fooled?
- **Spot 20**: first team to reach 20 "seen" (each no-person→person change counts).
- **Fool the model**: photo of a person on a phone? A drawing? A mannequin? A dog?
  What does it get wrong, and why? (It learned from photos of people, 96×96 grayscale.)
- **Fastest inference**: the leaderboard shows ms per frame. Why are they all about the same?
  *(Same chip, same model: the work is identical.)*

**Do (wow moment):** point at the dashboard's spotlight: it's the live 96×96 image the model sees.
*"This tiny gray square is everything the AI gets to see."* (96×96 = 9,216 numbers.)
Click a team's tile to pin it to the spotlight. Ask: *"Why so blurry? Why gray?"*
(`tools/snap_to_png.py` saves the same frame as a PNG, if you want it on a slide.)

### How it works, tied back to Part 1 (25 min)

Show `firmware/03_person_detector/main/` and draw the data path:

```
OV2640 camera ──(8-bit parallel + DMA)──► frame buffer 96×96 gray (PSRAM or SRAM)
      │                                   9 KB per frame
      ▼
 pixels 0..255 → int8 -128..127  (camera.c)
      ▼
 TFLite Micro interpreter (detector.cc)
   model weights  ~300 KB ─► stay in FLASH, read via cache (XIP again!)
   tensor arena   ~100 KB ─► RAM: activations between layers
   ESP-NN kernels ─► S3 vector instructions do several multiply-adds at once
      ▼
 person score 0..100 → debounce (2 frames on / 3 off) → LED + "seen" counter (main.c)
      ▼
 Wi-Fi task on core 0 → UDP broadcast → dashboard   (inference runs on core 1)
 same frame → JPEG (~1.4 KB) → UDP to the dashboard → live video tile
```

**Ask**: *"Which partition stores your team name?"* → *`nvs`, the one in the partition table from the boot log.*
**Ask**: *"Why does inference run on core 1?"* → *Wi-Fi mostly runs on core 0; separating them keeps both smooth. Dual core, from Part 1.*
**Ask**: *"Why 96×96 grayscale and not full HD color?"* → *RAM and compute. 96×96×1 = 9 KB; 1920×1080×3 = 6 MB. The model is designed to fit.*
**Ask**: *"It says 'person' but can it count three people?"*
→ *No. It's a classifier (is there a person?), not a detector (where are they, how many?).
Counting needs a detection model, like ESP-DL's pedestrian detection, which needs more RAM (PSRAM).*

### Challenges / stretch (10 min)

- Change the threshold: what happens at 30%? At 90%? (false positives vs. missed people)
- Where would you put this in a real product? What would it cost to send video to the cloud instead?
- Next level: ESP-WHO (face detection), ESP-SR (wake words), ESP-DL, Edge Impulse to train your own model.

---

## 3:35 – Q&A and wrap-up (25 min)

- Your career story: how you got from this college to embedded work.
- Where to go next: ESP-IDF Get Started guide, `esp-tflite-micro` examples, ESP-WHO, ESP-SR, Edge Impulse.
- Give them the repo link so they can rebuild everything themselves.

---

## Part 5 – Device · Phone · Cloud + RainMaker (55 min)

Needs internet on the workshop router (captive portal logged in, or phone tethering) and
one phone per team with the **ESP RainMaker** app installed and signed in.

**Say** (slides 38–42)
- The triangle: phone ↔ device over **BLE** (setup only), device ↔ cloud over **MQTT + TLS**
  (device keeps one connection open, so the cloud can push), phone ↔ cloud over **HTTPS**.
- Why a cloud at all: a device behind a home router has no public address, so both sides connect *out*.
- Provisioning: the phone sends SSID + password to the board over an encrypted BLE session.
- HTTPS = HTTP + TLS: certificate check, key exchange, encryption. The S3 has AES/SHA/RSA hardware.

**Do** (slide 43)
1. Default ESP Launchpad page (`espressif.github.io/esp-launchpad`) lists Espressif's prebuilt
   RainMaker apps → **RainMaker_switch** → ESP32-S3 → Flash.
2. Console shows a QR code → app → Add device → scan → pick Wi-Fi.
3. Toggle in the app → console logs it. Press BOOT → app updates.
4. Turn phone Wi-Fi off, use mobile data: still works. Trace the path (slide 44).

**Rehearse**: run this once on the venue network. Note what the switch drives on your board
(it may not be the LED pin); the console log and app state are the demo either way.
