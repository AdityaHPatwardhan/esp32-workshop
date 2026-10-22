# ESP Launchpad configs

Participants flash from Chrome/Edge with no installs, using Espressif's
[ESP Launchpad](https://espressif.github.io/esp-launchpad/). Launchpad reads a TOML file
that says which binaries to download, so the TOML and the binaries must be hosted
somewhere public **with CORS headers** — GitHub Pages works.

| File | Page | Use it for |
|---|---|---|
| `launchpad.toml` | Quick Start: dropdown of both apps | Blink (Hello Board) |
| `minimal-03_person_detector.toml` | Minimal Launchpad: one-click flash, console **with a command input box** | Person Detector (teams type `team <name>`) |
| `minimal-01_blink.toml` | Minimal Launchpad | Blink on its own page |

## Host on GitHub Pages

1. Push the repo (at least `binaries/*-merged.bin` and `tools/launchpad/`) to a public
   GitHub repo named `esp32-workshop`.
2. Repo **Settings → Pages → Deploy from branch → main / (root)**.
3. Replace `YOUR-GITHUB-USER` in every `.toml` with your GitHub username, push again.
4. Check that `https://YOUR-GITHUB-USER.github.io/esp32-workshop/binaries/01_blink-merged.bin`
   downloads in a browser.

## Links to share (make QR codes of these)

```
Quick Start (all apps):
https://espressif.github.io/esp-launchpad/?flashConfigURL=https://YOUR-GITHUB-USER.github.io/esp32-workshop/tools/launchpad/launchpad.toml

Person Detector (with console input):
https://espressif.github.io/esp-launchpad/minimal-launchpad/?flashConfigURL=https://YOUR-GITHUB-USER.github.io/esp32-workshop/tools/launchpad/minimal-03_person_detector.toml
```

If the TOML is hosted somewhere without CORS headers, append `&crossDomain=true` to
route it through Espressif's CORS proxy (binaries still need CORS).

## Notes

- If typing in the console does nothing, the board's USB-C is wired to the chip's native USB
  instead of a USB-UART chip. Point the TOML at `03_person_detector-usbconsole-merged.bin` instead
  (built by `tools/build_all.sh`). Find out which one your boards need at rehearsal.

- Merged binaries are flashed at `0x0` (bootloader + partition table + app).
  **Minimal Launchpad erases the whole flash first** (`eraseAll: true` in its source), so
  the team name is wiped on every re-flash — teams set it again in the console afterwards.
  Quick Start does not erase, **but the merged binary covers the NVS region (0x9000) with
  padding, so the team name is wiped there too** (verified on an ESP32-S3-EYE). After any
  Launchpad flash, teams set `team <name>` again. `flash.py` writes the parts separately and keeps NVS.
- Only Minimal Launchpad documents a console command box (`[[portConnectionOptions]]`).
  Rehearse typing `team Rockets` there before the workshop.
- Fallback without a browser: `python ../flash.py --team "Rockets"` (writes the team name
  straight into NVS at flash time).
