# Meshtastic on LilyGO T-Deck-MAX — UX review of the classic Screen UI on e-paper + keyboard + touch (no trackball)

Date: 2026-09-04. Scope: read-only review of this checkout (env `t-deck-max`). Line numbers are from commit c4d615a.

## 0. Two corrections to assumptions

1. **No virtual keyboard ever appears on this build.** The touch "[-- Free Text --]" entry is gated on `USE_VIRTUAL_KEYBOARD` (`src/modules/CannedMessageModule.cpp:176-183`), which is not defined for `t-deck-max`, and the fallback path needs `osk_found`, which is only set for trackball/encoder boards **without** `HAS_PHYSICAL_KEYBOARD` (`src/main.cpp:1001-1005`). Free text starts by typing a printable character from any frame (`CannedMessageModule.cpp:463-470`).
2. **Touch dies at ~60 s idle (~30 s with Bluetooth off), not 120 s.** `USE_POWERSAVE` forces `screen_on_secs = 30` and `wait_bluetooth_secs = 30` (`src/mesh/NodeDB.cpp:1155-1159`). ON→DARK after 30 s (`PowerFSM.cpp:423`), DARK→LS after `getBluetoothWaitMs()` (`PowerFSM.cpp:70-76, 449`). `SLEEP_TIME 120` is the light-sleep *cycle* length. Wake sources are `KB_INT` and `BUTTON_PIN` only (`src/sleep.cpp:458-467`); the three bezel keys go through the same Hyn touch controller (`variant.cpp:47-65`), so **touch and all bezel keys are dead in light sleep.**

Enter emits `0x0d` = `TCA8418Key::SELECT` (`TCA8418KeyboardBase.h:18`) → `INPUT_BROKER_SELECT` (`kbI2cBase.cpp:296`), so "type, Enter, sent" works.

## A. Cognitive walkthrough

Input vocabulary on this board:

| Gesture / key | Event | Source |
|---|---|---|
| Swipe L/R | LEFT / RIGHT (prev/next frame; in banners prev/next option) | `TouchScreenImpl1.cpp:89-96` |
| Swipe U/D | UP / DOWN | `:97-104` |
| Tap (release < 400 ms) | USER_PRESS (= next frame / next option) | `:109-111`, `TouchScreenBase.cpp:9` |
| Hold ≥ 400 ms | SELECT (fires before release) | `TouchScreenBase.cpp:170-175` |
| Bezel heart / circle / plane | USER_PRESS / SELECT / SEND_PING | `variant.cpp:32-45` |
| BOOT | USER_PRESS; hold 500 ms = SELECT; long-long = shutdown | `InputBroker.cpp:345-349` |
| alt+E/X/S/F | UP/DOWN/LEFT/RIGHT; alt+Q = CANCEL; alt+T = TAB | `TDeckProKeyboard.cpp:34-54` |

Two structural facts drive almost every problem:

- **Every screen change costs ~700 ms of blocking partial refresh plus a 200 ms throttle** (`GxEPD2_310_GDEQ031T10.h:34 partial_refresh_time = 700`; `EInkDisplay2.h:13-15`). Nothing acknowledges an input for most of a second.
- **There is no BACK/CANCEL and no UP/LEFT on touch or bezel.** Tap and bezel-heart both mean "next". Escaping needs alt+Q, and every alt-navigation is a two-press latch that clears after one use (`TDeckProKeyboard.cpp:100, 159-160`).

### A1. Read a new message
1. Because `HAS_I2S` is defined, `installDefaultModuleConfig` turns external notification **on** with the I2S ringtone and `nag_timeout = 15 s` (`NodeDB.cpp:1222-1230`, `Default.h:58`). `shouldWakeOnReceivedMessage()` returns **false whenever external notification is enabled** (`Screen.cpp:2231-2233`), so `handleNewMessage` skips `screen->setOn(true)` (`MessageRenderer.cpp:1153-1155`). Result: ringtone + 4× DRV2605 buzz, **e-paper stays dark.**
2. User taps. `InputBroker::handleInputEvent` eats that input to stop the nag (`InputBroker.cpp:115-120`). If the screen was off, the next input is also dropped as the wake tap (`:123-127`). If already in light sleep, taps do nothing.
3. Reaching the message frame is a ring-walk (A4).

### A2. Reply
- Menu path: hold → "Message Action" → "Reply" → ReplyMenu → compose. Three modal banners at ~900 ms each; no "back one option" on touch/bezel; options wrap (`NotificationRenderer.cpp:660-664`).
- Fast path: just type (`CannedMessageModule.cpp:463-470`). Destination is whatever was last used; only the header says "To:".
- `EINK_FORCE_DISPLAY_THROTTLE_MS 200` plus 700 ms blocking updates means bursts of keystrokes render in batches. Shift/sym latch expires after 1500 ms with no on-screen modifier indicator (the badge is `INPUTBROKER_SERIAL_TYPE == 1` only, `CannedMessageModule.cpp:1948-1980`).
- **Draft loss:** `INACTIVATE_AFTER_MS 20000` wipes `freetext` after 20 s without a keystroke (`:49, 1224-1235`).

### A3. Pick a recipient / channel
- Destination picker is reached only via Tab (alt+T) (`handleTabSwitch`, `:521-530`). The on-screen "Dest:" hint is compiled out on this build (`:1948-2028`). Nothing tells the user how to change destination.
- In the picker, tap = move down one row (`:549-551`), no hit-test on the row under the finger. Type-to-filter exists (`:556-565`) but is undiscoverable. Every arrow press forces a 700 ms redraw (`:598`).

### A4. Node list and signal
- On `USE_EINK` the node list is **four separate frames** (`Screen.cpp:1324-1349`). Ring ≈ 12+ frames, linear, wrap only. Home→System ≈ 8 s of blocking refreshes.
- Direct-jump already exists (`FN_F1..F5`, `Screen.cpp:2105-2139`) but the TCA8418 map never emits `FUNCTION_F1..F5`.
- In a list, tap = next frame, the opposite of what a touchscreen user expects.

### A5. Position / GPS
- Hold on GPS frame opens `positionBaseMenu` (`Screen.cpp:2156-2158`). alt+G toggles GPS with no visible confirmation.

### A6. Settings (region / preset / role)
- Hold on LoRa frame → LoRa Actions → Region/Preset picker (`MenuHandler.cpp:140-171, 495-534`). Tap-to-advance, wrap-only, ~900 ms per step. Number-entry banners accept digits (`NotificationRenderer.cpp:316-321`); option lists do not accept type-to-jump.
- **Brightness is hidden.** `hasSupportBrightness = false` for this variant (`MenuHandler.cpp:2413-2420`) though the frontlight is driven at `Screen.cpp:617/678/792`. `SystemCommandsModule` handles `MSG_BRIGHTNESS_UP/DOWN` (`:37`) but no key emits them.

### A7. Mute / notify
- alt+M toggles mute (`SystemCommandsModule.cpp:46`). Per-channel mute under Message Action (`MenuHandler.cpp:764-771`). Default profile is "ringtone over the speaker for 15 s".

### A8. Sleep / wake
- After 30 s the screensaver overlay (`Screen.cpp:1203-1256`) appears with no hint that touch will stop working; after ~60 s light sleep, only keyboard/BOOT wake. Upstream draft PR #10679 reports "UI stays dark after wake" on T-Deck Pro.

### A9. Display health
- **The plain `EInkDisplay2` path never issues a full refresh after boot.** `connect()` sets a full-screen *partial window* (`EInkDisplay2.cpp:264`); `nextPage()` with `_using_partial_mode` → `_Update_Part` (`GxEPD2_BW.h:433-441`, `GxEPD2_310_GDEQ031T10.cpp:277-292`); only `_initial_refresh` is full. Every `EINK_ADD_FRAMEFLAG(...)` in Screen.cpp expands to nothing without `USE_EINK_DYNAMICDISPLAY` (`EInkDynamicDisplay.h:153`). Ghosting accumulates for the life of the session. `EINK_NOT_HIBERNATE` keeps the panel powered between updates.

## B. Upstream T-Deck Pro and InkHUD

**T-Deck Pro** is on the same code path: plain `EInkDisplay2`, `USE_EINK_DYNAMICDISPLAY` commented out since #6936 with no recorded rationale. Upstream fixes are patches on this model:
- forceDisplay throttle 1000→200 ms (panel "didn't repaint after typing ~40% of the time") — meshtastic/firmware#9303
- freetext hang killing touch, fixed 2.7.7 — #7714 / #7781
- keyboard backlight moved alt+B → alt+Space — #10213; frontlight hotkey request closed not-planned — #9288
- light-sleep wake not recognised by PowerFSM, UI stays dark — #10679 (draft)
- touch active while display off → accidental sends — #8559
- large touch/haptics/240x320 e-ink UI rework closed unmerged, split into drafts #11524 (touch pipeline), #11525 (240x320 e-ink UI), #11526 (DRV2605 haptics) — #11402
- LilyGO store reviews (2.2/5): "clicking cycles through navigation options in a loop rather than what you expect from a touch screen", 5–6 s touch lag

**InkHUD** solves the display half: `setDisplayResilience(N, x)` = N fast refreshes per full (`variants/esp32s3/heltec_wireless_paper/nicheGraphics.h:70-73`), async updates, one-button short/long semantics.

**Could InkHUD run here?** Architecturally yes (several ESP32-S3 InkHUD envs exist; `t5s3_epaper` proves touch via `inkhud->touchTap/touchNav*`). Blockers:
1. **No driver for GDEQ031T10 / UC8253** in `src/graphics/niche/Drivers/EInk/`. Upstream issue #11354 (open) has a `GDEQ031T10.h` that wrongly extends SSD16XX. GxEPD2's class is the init/LUT reference.
2. **No hardware-keyboard text path.** `[inkhud]` sets `MESHTASTIC_EXCLUDE_INPUTBROKER` and `MESHTASTIC_EXCLUDE_SCREEN`; the Keyboard applet is on-screen only. TCA8418 → InkHUD compose would be new plumbing.
3. All touch/bezel wiring, `kbI2cBase`, and PowerFSM interactions need a `nicheGraphics.h` rewrite.

Verdict: right long-term destination, but XL. Borrow its refresh policy now (C1).

## C. Prioritised improvements

Effort: S < 1 day, M 1–3 days, L a week+.

### C1. Periodic full refresh (ghosting) — S
- No full refresh ever after boot (A9).
- Option A (config, test-flash): add `-D USE_EINK_DYNAMICDISPLAY` to `variants/esp32s3/t-deck-max/platformio.ini`; makes `EINK_LIMIT_FASTREFRESH` / `EINK_LIMIT_GHOSTING_PX` live (`EInkDynamicDisplay.cpp:54-65, 321-340`). Set `EINK_LIMIT_FASTREFRESH=5` per vendor FAQ. Upstream left this off for T-Deck Pro with no reason recorded; `EINK_NOT_HIBERNATE` hints at earlier wake trouble, so soak-test.
- Option B (code, minimal): in `EInkDisplay::forceDisplay` (`EInkDisplay2.cpp:55-102`) add a `T_DECK_MAX` partial counter; every 5 updates do `setFullWindow(); nextPage(); setPartialWindow(...)`. Also full-refresh on wake from screensaver (`Screen.cpp:1246-1255`).
- Risk: 1100 ms blocking flash, harmless.

### C2. Message arrival wakes the screen; don't eat the user's taps — S
- `HAS_I2S` default enables ext-notification, which disables wake-on-message (`Screen.cpp:2231`) and makes the first input a nag-stopper (`InputBroker.cpp:115-120`).
- In `Screen.cpp:2225-2244`, treat ext-notification-enabled as wake-eligible for `T_DECK_MAX`. In `NodeDB.cpp:1222-1230` give `T_DECK_MAX` quieter defaults: buzzer off, `nag_timeout = 0`, DRV2605 vibra on. Don't drop the nag-stop input when it is a keyboard char.

### C3. Full nav set on touch/bezel: LEFT / SELECT / RIGHT plus a CANCEL key — S
- `variant.cpp:32-45`: heart → `INPUT_BROKER_LEFT`, circle → `SELECT`, plane → `RIGHT` (matches `NotificationRenderer.cpp:640-646`, `Screen.cpp:2101-2104`). Ping stays on alt+P (or long-hold plane if Hyn exposes duration). Map the unmodified `$` key (`TDeckProTapMap` row 22) or mic key (row 34) to `Key::ESC`. Optionally top/bottom 20 % tap zones = UP/DOWN in `TouchScreenImpl1::onEvent`.

### C4. Direct frame jumps from the keyboard — S
- Put `Key::FUNCTION_F1..F5` on the alt layer in `TDeckProTapMap` (`TDeckProKeyboard.cpp:26-62`). Extend `Screen.cpp:2105-2139` so F1..F5 target home/messages/nodes/GPS/system via `framesetInfo.positions`, not raw indices.

### C5. Stop wiping drafts; keyboard-aware compose screen — S
- For `HAS_PHYSICAL_KEYBOARD`, raise FREETEXT timeout to ≥ 120 s or keep the draft on inactivate (`CannedMessageModule.cpp:49, 1224-1235`). Draw an "alt+T: To" hint and a shift/sym/alt badge in the eink freetext branch (`:1935-1946`); add a `modifierFlag` getter to `TDeckProKeyboard`.

### C6. Touch timing for a 700 ms display, plus distinct haptics — S
- `TIME_LONG_PRESS 400` (`TouchScreenBase.cpp:9`) is shorter than one refresh, so "hold until something happens" turns taps into SELECT. Add `-D TIME_LONG_PRESS=600` (t5s3 overrides to 500). Fire a distinct DRV2605 waveform on LONG_PRESS (`:170-175`) and a short pulse on swipe (`:109-127`); suppress the touch-down pulse for swipes.

### C7. Sleep: say touch is dead, verify key-wake lights the panel — S/M
- Add "Press any key" to `drawScreensaverOverlay` for `T_DECK_MAX`. Verify KB_INT wake promotes PowerFSM LS→ON and calls `handleSetOn(true)`; if #10679 reproduces, port that fix. Consider longer `screen_on_secs`/`wait_bluetooth_secs` under `USE_POWERSAVE` for this variant (`NodeDB.cpp:1155-1159`). Do NOT enable `WAKE_ON_TOUCH` first (documented reboot risk).

### C8. Expose frontlight brightness — S
- Add `defined(HAS_EINK_FRONTLIGHT)` to the `hasSupportBrightness` branch (`MenuHandler.cpp:2413-2420`); map alt+O/alt+I to `INPUT_BROKER_MSG_BRIGHTNESS_UP/DOWN`; persist to `uiconfig.screen_brightness` (read at `Screen.cpp:749-760`).

### C9. Quieter defaults for this handheld — S
- `T_DECK_MAX` block in `installDefaultModuleConfig`: vibra on, ringtone off, `nag_timeout = 0`; short default canned-message set. Pair with C2.

### C10. Destination picker: tap-to-select rows — M
- In `handleDestinationSelectionInput`, when `event->touchY != 0`, hit-test against row geometry and set `destIndex` directly; hold = select. Same for the canned list and banner options (`NotificationRenderer.cpp:636-664`).

### C11. E-ink node-list consolidation — M
- One node-list frame whose column mode is cycled by LEFT/RIGHT within the frame while UP/DOWN scroll; drops the ring by three frames (`Screen.cpp:1324-1349`).

### C12. InkHUD port — XL (roadmap)
- UC8253 `Drivers::EInk` subclass from GxEPD2 sequences; `kbchar` ingestion into InkHUD compose (no precedent); `nicheGraphics.h` for this board. Track #11354 and drafts #11524-#11526. Start from GxEPD2's exact init; never guess waveforms.

### Latent note
`kbI2cBase.cpp:317-320` maps `TCA8418KeyboardBase::BL_TOGGLE` to `INPUT_BROKER_MSG_BLUETOOTH_TOGGLE` (copy-paste). Unreachable today because `TDeckProKeyboard::released()` intercepts `BL_TOGGLE` first (`TDeckProKeyboard.cpp:153-156`).

## Suggested order
C1 + C2 + C3 make the device stop feeling broken and are all S; ship together and re-test message arrival. C4–C9 are one-file changes that fill the trackball-shaped holes. C10–C11 are the touch-native rework; C12 is the strategic path.
