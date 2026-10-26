# smalltv-pro-esphome

ESPHome firmware for the **GeekMagic SmallTV Pro** that shows pages from your own server and reports taps on the touch button. No cloud, no apps from the manufacturer, no images that do not fit into memory: the clock fetches a tiny JSON "draw list" and draws it itself.

## Why

The stock firmware of the SmallTV Pro has a fixed set of apps. Its touch button only switches between them internally; nothing about a tap ever reaches the network, so you cannot use it as a button for your own things. Showing your own content means uploading images, and the Pro has no PSRAM: a full 240 × 240 frame in RGB565 is 115 KB in one piece, more than the largest free memory block under ESPHome (73 to 110 KB at start, 9 to 16 KB after loading two image strips).

This firmware solves both:

- **Draw lists instead of images.** Your server describes the page as shapes and texts (rectangles, circles, polygons, lines, anti-aliased text). That is 0.7 to 3 KB of JSON per page. The clock fetches it every 15 seconds and redraws only when it changed.
- **Touch events.** A single tap and a double tap are sent to your server as HTTP POST. A double tap never triggers the single action as well.
- **Brightness from the server.** The draw list can carry a brightness value; `0` turns the backlight off.
- **Local only.** No time server, no DNS lookups to the internet, no web UI on the clock. It only talks to your server. OTA updates are password protected, the ESPHome API is encrypted.

## Hardware

| Item | SmallTV Pro |
| --- | --- |
| SoC | ESP32 (rev 3.1), 8 MB flash, **no PSRAM** |
| Display | ST7789V, 240 × 240, colors inverted |
| Pins | SPI CLK 18, MOSI 23, DC 2, reset 4, backlight 25 (PWM, inverted) |
| Touch | GPIO32 (touch channel T9), button on top of the case |

The SmallTV **Ultra** is a different device (ESP8266) and is not supported by this configuration.

## Status

This firmware has been running on a SmallTV Pro (stock firmware V3.3.76EN before) since October 2026 with ESPHome 2026.9.1, as a status display with the touch button as input. The version in this repository is the same firmware with English names, a generic button endpoint, Roboto instead of DejaVu fonts and optional text alignment. It was compiled with ESPHome 2026.9.1 before publishing, but this exact build has not been flashed to a device. The example server was tested with HTTP requests, not with a clock.

## Files

| File | Purpose |
| --- | --- |
| `smalltv-pro.yaml` | ESPHome configuration: Wi-Fi, display, touch, fetching and button events |
| `drawlist.h` | Parses the draw list and draws it (included by the yaml) |
| `secrets.yaml.example` | Template for Wi-Fi, OTA password and API key |
| `docker-compose.yaml` | ESPHome build environment in Docker, nothing runs permanently |
| `server/example_server.py` | Example server (Python standard library only): clock page and tap counter |

## Setup

### 1. Run a server

Start the example server on any machine in your LAN:

```bash
python3 server/example_server.py --port 8790
```

Check it in a browser: `http://<server-ip>:8790/api/devices/desk/drawlist` returns the page as JSON.

### 2. Configure

```bash
cp secrets.yaml.example secrets.yaml
```

- `secrets.yaml`: your Wi-Fi, a long OTA password and an API key (`openssl rand -base64 32`). Keep this file. Without the OTA password the clock can only be flashed over UART.
- `smalltv-pro.yaml`, block `substitutions`: `server` is the base URL of your server, `device_key` the name it uses for this clock.

### 3. Build

With Docker (no local ESPHome needed):

```bash
docker compose run --rm esphome compile smalltv-pro.yaml
```

The first build downloads the ESP32 toolchain (about 2 GB, kept in `.platformio`). The result is `.esphome/build/smalltv-pro/build/firmware.ota.bin` (about 0.9 MB; the test build used 26 % RAM and 24 % of the app partition).

### 4. Flash the first time (no cable)

The stock firmware has an update page. Before you replace it:

- Download the stock firmware for the way back from [GeekMagicClock/smalltv-pro](https://github.com/GeekMagicClock/smalltv-pro) and keep it.
- Make sure the clock has a fixed IP address in your router.

Then open `http://<clock-ip>/update` in a browser, choose `firmware.ota.bin` and upload it. The clock restarts with ESPHome, connects to your Wi-Fi and shows a dark screen with a frame until the first draw list arrives.

This replaces the stock firmware. You do this at your own risk. If a firmware does not start at all, only the UART pads inside the case and a 3.3 V USB serial adapter help.

### 5. Later updates

```bash
docker compose run --rm esphome upload smalltv-pro.yaml --device <clock-ip>
docker compose run --rm esphome logs smalltv-pro.yaml --device <clock-ip>
```

Going back to the stock firmware should work the same way with `--file <stock-firmware>.bin`; this has not been tested.

## Server protocol

The clock calls two endpoints, `<server>` and `<key>` come from the substitutions:

| Request | Body | Meaning |
| --- | --- | --- |
| `GET <server>/api/devices/<key>/drawlist` | none | every 15 s; answer with the draw list, HTTP 200 |
| `POST <server>/api/devices/<key>/button` | `{"event": "single"}` or `{"event": "double"}` | after a tap; the clock fetches the page again 0.5 s later |

The draw list:

```json
{
  "brightness": 80,
  "ops": [
    ["f", [16, 16, 20]],
    ["t", 120, 40, "b56", [235, 235, 235], "12:15", "c"],
    ["r", 30, 176, 180, 2, [60, 60, 70]],
    ["c", 225, 15, 5, [80, 200, 120]]
  ]
}
```

| Op | Parameters | Draws |
| --- | --- | --- |
| `f` | color | fills the background; must be the first op |
| `r` | x, y, width, height, color | filled rectangle |
| `c` | x, y, radius, color | filled circle |
| `p` | [x1, y1, x2, y2, ...], color | filled convex polygon |
| `l` | x1, y1, x2, y2, color | line |
| `t` | x, y, font, color, text, align | text; `align` is `l` (default), `c` or `r` relative to x |

Colors are `[r, g, b]`. Fonts are keys from the `font` section of the yaml: `r14`, `r18` (regular), `b16`, `b20`, `b28` (bold) and `b56` (bold, digits only, for a big clock). Unknown ops are skipped. The response may be up to 8 KB.

## Touch calibration

The threshold depends on the device. To measure it, set `setup_mode: true` under `esp32_touch`, flash, and watch the log: the raw value drops when you touch the button. One device read 1905 to 1920 untouched, and taps triggered reliably with `touch_threshold: "1650"`. Set the threshold between the two values, then set `setup_mode` back to `false`.

## Things that cost me time

- **No PSRAM.** Full-frame images do not fit (see "Why"). Image strips were not a solution either: memory fragments after two strips.
- **`online_image` reserves its buffer at boot.** Its `buffer_size` (64 KB by default) is allocated when the device starts, for every image component. A few of them put the Pro into a boot loop.
- **The partial display buffer stops on empty passes.** With `buffer_size: 12.5%` the `mipi_spi` driver draws the page in eight passes and aborts a pass in which nothing is drawn. Fill the full background first (op `f`) and it works.
- **`includes` come after `globals`.** Custom types from `drawlist.h` cannot be used in `globals`; the draw list therefore lives inside `drawlist.h`.
- **Fonts live in two places.** A font key used by the server must exist in the yaml (and in the `font_for` list). A missing key is silently skipped; a missing character is not drawn.
- **Automatic rollback.** If a new firmware does not boot cleanly ten times in a row, ESPHome may fall back to the previous one, although the upload said "OTA successful". The line `compiled on ...` in the log shows which build runs. The bootloader stays the one from GeekMagic.
- **The stock firmware shows your Wi-Fi password.** Firmware V3.3.76EN serves it in plain text without login at `http://<clock-ip>/.sys/config.json`. Another reason to replace it, or at least to keep the clock in a separate network.
- **The stock firmware phones a Chinese time server.** It queried `ntp.aliyun.com` (Alibaba Cloud) until the time server was changed in its settings. With this firmware the clock makes no DNS requests at all.

## Related projects

- [giovi321/smalltv-mod](https://github.com/giovi321/smalltv-mod) also runs on the SmallTV Pro, but does not use the touch button.
- [ESPHome](https://esphome.io) does all the heavy lifting.

This project is not affiliated with or endorsed by GeekMagic.

## License

[MIT](LICENSE). The Roboto font is not part of this repository; ESPHome downloads it from Google Fonts at build time under its own open font license.
