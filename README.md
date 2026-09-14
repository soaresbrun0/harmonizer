# Harmonizer

Firmware for an ESP32 that turns a Logitech Harmony Companion remote into a Home Assistant controller. An nRF24L01+ radio listens for the remote's RF commands and each button press is published to Home Assistant over MQTT as an individual device trigger, ready to use in automations — no Harmony Hub software or cloud account involved at runtime.

## Features

- **Config portal, no reflashing.** Wi-Fi, Smart Hub endpoint, and MQTT broker settings are all configured from a step-by-step web wizard served by the device itself (captive AP on first boot, then on its LAN address). Saving the MQTT or hub settings never requires recompiling.
- **Assisted Smart Hub setup.** The portal can scan for your hub (power it on, press pair/reset) and captures its channel and address, or you can enter them manually. A live test area confirms remote presses are received before you finish.
- **One trigger per button.** All 48 remote buttons are exposed as Home Assistant device triggers (press + release each), so automations target buttons directly instead of parsing payload strings.
- **Multi-button (chord) support.** Up to 6 simultaneous presses are decoded; each newly pressed button fires its own trigger and each released button fires its release trigger, with no repeat spam while held.
- **MQTT discovery.** The device (`Harmonizer Smart Hub`) and all triggers self-register with Home Assistant — no manual YAML entities.

## How it works

```
Harmony remote --(nRF24 RF)--> ESP32 --(MQTT)--> broker --> Home Assistant
```

On boot the ESP32 joins Wi-Fi, listens on the saved hub channel/address for remote packets, decodes button presses, and publishes them. The portal (`/review`, also linked from the HA device page) stays available for reconfiguration and remote testing.

## Hardware

- ESP32 dev board
- nRF24L01+ module (3.3 V) wired over SPI: `CE = GPIO4`, `CSN = GPIO5` by default (overridable with `-D` build flags), plus the standard SPI pins (SCK/MOSI/MISO)
- A Harmony (Smart) Hub + Companion remote, needed once to discover the hub's channel/address during setup

## Firmware setup

Requires [PlatformIO](https://platformio.org/):

```bash
pio run -t upload       # flash
pio device monitor      # serial console, 115200 baud
```

Dependencies (`RF24`, ArduinoHA) resolve automatically; the portal web pages are embedded into flash at build time by `scripts/embed_portal.py`.

## First run

1. Power on — with no Wi-Fi saved, the device starts an access point named `SmartHub_<id>`. Join it (or later open `http://<device-ip>/` on your LAN).
2. **Network** — pick your Wi-Fi network and enter the password.
3. **Smart Hub** — press **Start scan**, power on the hub, press its pair/reset button. The captured channel/address can be saved, or entered manually.
4. **MQTT** — broker IP, port (default 1883), and optional username/password.
5. **Review** — verify everything, test remote buttons live, then apply (reboots only if the network changed).

## Home Assistant usage

1. Enable the **MQTT** integration (discovery on — the default).
2. The `Harmonizer Smart Hub` device appears with a trigger per button, e.g. `button_short_press` / `button_short_release` on subtype `ok`, `volume_up`, `tv`, …
3. Use the device as a trigger in any automation — e.g. *when `volume_up` pressed → raise media volume*.

Button names are `snake_case` (`number_0`–`number_9`, `channel_up`, `skip_forward`, …); activity buttons drop the `Activity` prefix (`tv`, `music`, `movie`).

## Differences from pkscout's Harmoino

Harmonizer builds on the RF work of [pkscout/Harmoino](https://github.com/pkscout/Harmoino) (itself a fork of [joakimjalden/Harmoino](https://github.com/joakimjalden/Harmoino)) but changes how the bridge is configured and consumed:

- **Browser setup vs editing code.** pkscout's approach requires entering the nRF24 network key, Wi-Fi/MQTT credentials, and topic in the sketch and reflashing. Harmonizer does all of it (including hub scanning) from the on-device portal.
- **Per-button triggers vs one topic of strings.** pkscout publishes each press as a short string to a single MQTT topic you then wire up yourself. Harmonizer registers a discrete press/release trigger per button via MQTT discovery, so automations attach directly.
- **Chord support.** Simultaneous multi-button presses are decoded into individual per-button events; held buttons don't re-fire.

## Credits

- [pkscout/Harmoino](https://github.com/pkscout/Harmoino) — Home Assistant-oriented fork: inspiration for this project and source of RF logic and command knowledge.
- [joakimjalden/Harmoino](https://github.com/joakimjalden/Harmoino) — the original minimal Harmony-remote-via-nRF24 work, including the protocol research and RF parameters this project relies on.
- [ArduinoHA](https://github.com/dawidchyrzynski/arduino-home-assistant) (`home-assistant-integration`) for the MQTT/HA layer, and [nRF24/RF24](https://github.com/nRF24/RF24) for the radio driver.

## License

Copyright (C) 2026 soaresbrun0.

This project is licensed under the GNU Affero General Public License v3.0 — see [LICENSE](LICENSE) for the full text. In short: you may use, modify, and share it (including selling devices running it), but any distribution must include the full corresponding source under the same terms, with this copyright intact.
