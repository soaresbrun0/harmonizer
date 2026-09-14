# Harmonizer Automations

Harmonizer button pressed/released events arrive as MQTT device triggers (`button_short_press` /
`button_short_release` + `subtype`). The 13 automations in this folder translate those into
lights, receiver volume, Apple TV / Google TV commands, and TV-activity
scene selection. Shared state lives in `var.*` helpers (click counters,
timestamps, selected light), plus `media_player.tx_rz810` (the AV receiver).

## Prerequisites

These are examples — adapt them before importing. Nothing here works verbatim
on another setup:

- Home Assistant with the **MQTT** integration and discovery enabled (that's
  how the `button_short_press` / `button_short_release` device triggers
  appear).
- The [`home-assistant-variables`](https://github.com/snarky-snark/home-assistant-variables)
  custom integration. Every `var.set` action and `states('var.…')` read below
  depends on it. Declare these variables first (suggested initial values in
  parentheses):
  - `var.harmonizer_tv_click_count` (`0`)
  - `var.harmonizer_movie_click_count` (`0`)
  - `var.harmonizer_selected_light` (e.g. `light.family_room_lights`)
  - `var.harmonizer_light_1_last_pressed_timestamp` (`0`)
  - `var.harmonizer_light_2_last_pressed_timestamp` (`0`)
  - `var.harmonizer_back_last_pressed_timestamp` (`0`)
- Replace the author's `device_id` (`8679b73f0fe819ef8ba86f5ff4579a92`,
  hardcoded in every trigger) with your own Harmonizer device ID — e.g. copy
  it from any device trigger's YAML on your instance.
- Rename the author's entities to yours:
  - Light 1: `light.family_room_lights`
  - Light 2: `light.family_room_accent_lights`
  - AV receiver: `media_player.tx_rz810`
  - Apple TV: `remote.family_room_apple_tv`
  - Google TV: `remote.family_room_google_tv`
  - Scene automations:
    - `automation.watch_apple_tv_on_family_room_tv`
    - `automation.watch_google_tv_on_family_room_tv`
    - `automation.watch_apple_tv_on_family_room_projector`
    - `automation.watch_google_tv_on_family_room_projector`
    - `automation.turn_off_family_room_media_devices`

File map (`harmonizer_<button>.yaml`):

| File | Button | What it does |
| --- | --- | --- |
| `harmonizer_tv.yaml` | tv | 1 click: watch Apple TV on TV; 2 clicks: Google TV on TV |
| `harmonizer_movie.yaml` | movie | 1 click: Apple TV on projector; 2 clicks: Google TV on projector |
| `harmonizer_off.yaml` | off | Runs the turn-off-media-devices automation |
| `harmonizer_light_1.yaml` | light_1 | Family-room lights + selects them for +/− |
| `harmonizer_light_2.yaml` | light_2 | Accent lights + selects them for +/− |
| `harmonizer_plus.yaml` | plus | Brighten selected light while held |
| `harmonizer_minus.yaml` | minus | Dim selected light while held |
| `harmonizer_apple_tv.yaml` | up/down/left/right/select/menu/etc. | Forwards buttons to Apple TV remote |
| `harmonizer_google_tv.yaml` | up/down/left/right/digits/colors/etc. | Forwards buttons to Google TV remote |
| `harmonizer_volume_up.yaml` | volume_up | Raise receiver volume while held |
| `harmonizer_volume_down.yaml` | volume_down | Lower receiver volume while held |
| `harmonizer_mute.yaml` | mute | Mutes/unmutes receiver |
| `harmonizer_back_apple_tv.yaml` | back | Short press: back; hold: top menu (Apple TV only) |

## Patterns used

### 1. Multi-click detection (`tv`, `movie`)

Used to fit two scenes on one button.

- Each press increments a counter var (`var.harmonizer_tv_click_count`,
  `var.harmonizer_movie_click_count`), then waits ~0.5 s.
- After the wait it triggers `automation.watch_*` selected by the count
  (1 → Apple TV, 2 → Google TV) via `automation.trigger`, then resets the
  counter to 0. A third click inside the window matches neither branch, so
  the template renders empty — extend with another `elif` if you need it.
- `mode: restart` is the key: a second press within the window restarts the
  automation, cancelling the first run's pending trigger, so only the final
  count fires.

### 2. Short press vs. long press (`light_1`, `light_2`, `back`)

Used where press-and-hold should mean something different from a tap.
Threshold is 0.5 s (`long_press_delay_in_seconds`).

- The automation listens for both `pressed` and `released` trigger IDs and
  stores `now()` in a timestamp var on press.
- The `pressed` branch does a `delay: 0.5s` before the "long" action.
- The `released` branch runs on early release (elapsed < 0.5 s) with an
  empty sequence — combined with `mode: restart`, this just cancels the
  still-waiting `pressed` run.
- Net effect: hold past the delay → long action runs; release early → long
  action is cancelled.
- `light_1` / `light_2` add a state check: if the light is off, press turns
  it on immediately; if it is on, only a hold turns it off (early release
  does nothing). Both also record their light in
  `var.harmonizer_selected_light`, which is what `plus`/`minus` act on.
- `back` (Apple TV only, gated on receiver source `APPLE TV`): tap sends
  `menu`, hold sends `top_menu` to `remote.family_room_apple_tv`.

### 3. Repeat while held (`plus`, `minus`, `volume_up`, `volume_down`)

Used for smooth dimming / volume ramping.

- `pressed` enters a `repeat / while` loop; `released` is an empty branch.
- With `mode: restart`, releasing restarts the automation into the empty
  branch, which kills the loop.
- `plus` / `minus`: step the selected light (`var.harmonizer_selected_light`)
  by ±10% (`brightness_step_pct`) every 0.2 s until fully bright / dark.
  `minus` additionally requires the light to be on.
- `volume_up` / `volume_down`: nudge `media_player.tx_rz810` by ±0.005 every
  0.2 s while the level is within 0–1. Both require the receiver to be `on`,
  so they do nothing when nothing is playing.

### 4. Button-ID passthrough (`Apple TV`, `Google TV`)

Used to reuse the whole D-pad / transport cluster for whichever streamer is
active.

- One automation declares 12 (Apple TV) or 32 (Google TV) triggers, each
  with its own trigger `id` (`up`, `DPAD_UP`, `MEDIA_PLAY`, `PROG_RED`, …).
- The single action forwards `command: '{{ trigger.id }}'` to the matching
  remote (`remote.family_room_apple_tv` / `remote.family_room_google_tv`).
- A condition gates on the receiver's source attribute (`APPLE TV` vs.
  `GOOGLE TV`), so only the active streamer's automation reacts.
- `mode: queued (max: 10)` preserves rapid button mashes instead of
  cancelling them like `restart` would.

### 5. Simple and guarded one-shots (`off`, `mute`)

- `off`: single MQTT press → `automation.trigger` on the turn-off-media
  automation. No state, `mode: single`.
- `mute`: single press → `media_player.volume_mute` on the receiver. Guarded
  by the receiver being `on`, so the button is dead when nothing is
  playing. The mute flag is templated from the receiver's current
  `is_volume_muted` attribute.

## Timing and step constants

- 0.5 s: multi-click window (`tv`, `movie`) and long-press threshold
  (`light_*`, `back`).
- 0.2 s: hold-to-repeat rate (`plus`, `minus`, `volume_*`).
- ±10% brightness steps; ±0.005 volume steps.
- `restart` = cancellable (click counting, press-vs-hold, repeat loops);
  `queued` = every press counts (remote passthrough); `single` = fire once
  (`off`, `mute`).
