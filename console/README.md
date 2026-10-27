# Undertow Rack

A small Mac app that runs up to three AU/VST3 effects (Undertow or anything else) on your Mac's audio,
in the spirit of UAD Console or SoundSource, stripped down.

- **Whole system** by default, or **Selected apps** only.
- Nothing is installed into the system: no audio driver, no admin password. It uses macOS's own
  audio-tap feature (macOS 14.2 or later), which only exists while the app is running with Power on.
  Turn Power off or quit and your audio goes back to normal.

## Install

Paste into Terminal:

```
curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-rack-mac.sh | bash
```

It downloads the latest build, puts **Undertow Rack.app** in your own `~/Applications` folder,
clears the download quarantine on it, and opens it. Run the same line again to update.

The first time you turn Power on, macOS asks to allow **Screen & System Audio Recording** for
Undertow Rack. Allow it (System Settings > Privacy & Security > Screen & System Audio Recording).
Because the builds aren't signed with a paid Apple certificate, macOS may ask again after an update.

## Use

| Control | What it does |
|---|---|
| **Power** | On: your audio runs through the rack. Off: the tap is removed and audio is untouched. |
| **Source** | *Whole system*, or *Selected apps*: tick the apps to process (apps appear once they've used audio; "playing" shows which are making sound). Unticked apps play normally. |
| **Output** | Where the processed audio plays. *System default* follows whatever you pick in the menu bar. |
| **Slots 1-3** | Click a slot to choose a plug-in (or Remove). **Edit** opens its window, **Bypass** skips it. Slots run 1 → 2 → 3. |
| **Volume** | Master output level (-60 dB = muted, up to +6 dB). Output is hard-limited at 0 dBFS. |
| **Scan for plug-ins...** | Finds your AU and VST3 plug-ins. Run it once after installing, and after adding plug-ins. |

Everything (power, source, apps, output, plug-ins and their settings, volume) is remembered for next time.
Closing the window quits the app.

## Remove

Delete **Undertow Rack.app** from `~/Applications`. Its only other file is its settings, in
`~/Library/Application Support/Undertow Rack`; to remove both in one step:

```
curl -fsSL https://raw.githubusercontent.com/JSZMoney/Undertow.Public/main/install-rack-mac.sh | bash -s -- --uninstall
```

## Notes

- Audio from Undertow Rack itself is never re-captured (no feedback).
- Plug-in latency isn't compensated, so add plug-ins with big look-ahead sparingly if you're watching video.
- If a plug-in crashes during **Scan**, it's skipped next time.

## Build

GitHub Actions (`.github/workflows/console.yml`) builds a universal app on every push that changes
`console/`, and publishes it as the `rack-latest` release plus a numbered `rack-build-N` release.
Locally: `cmake -S console -B build && cmake --build build`. JUCE 8.0.4 is downloaded automatically.

Source layout: `SystemAudio_mac.mm` (Core Audio tap + aggregate device), `SystemAudio_stub.cpp`
(non-Mac stand-in), `Rack.*` (plug-in chain), `MainComponent.*` / `Main.cpp` (UI).
