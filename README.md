# SidControl

Editor and librarian for the **MIDIbox SID V2** (PIC18F core). It runs as a standalone Mac app, or as an AU or VST3 plugin.

It talks to the box with the official V2 SysEx protocol (`F0 00 00 7E 4B <device> ...`). The spec is
`apps/synthesizers/midibox_sid_v2/doc/mbsidv2_sysex_implementation.txt` in
[midibox/mios8](https://github.com/midibox/mios8).

## Build

Requires Xcode (or the command line tools) and CMake (`brew install cmake`).

```bash
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Output is in `build/SidControl_artefacts/Release/`, in the `Standalone/`, `AU/` and `VST3/` folders.
To use the plugins, copy them to `~/Library/Audio/Plug-Ins/Components` (AU) or `~/Library/Audio/Plug-Ins/VST3`.

## Using it

1. Pick the **MIDI In** and **MIDI Out** your MIDIbox SID is connected to, then press **Test**.
   The status bar shows which SID cores answered.
2. **Live** (on by default): every edit is sent to the box's edit buffer as you make it.
3. **Get Edit Buffer** reads the patch that is currently playing. **Send Edit Buffer** sends the whole patch in the editor.
4. Librarian (left panel):
   - Choose the BankStick bank (A–H).
   - **Fetch Bank** reads all 128 patches from the box.
   - Double-click a patch to open it (and send it to the box).
   - **Store Here** writes the editor patch into the selected slot on the box.
   - **Write Bank** writes the whole list to the box.
   - **Open .syx** and **Save Bank** read and write bank files. The official preset banks open directly.
   - Right-click a patch for copy, paste, init and clear.
5. **Save Patch .syx** saves the current patch as an edit-buffer dump, so sending it to a box never overwrites a stored slot.

The plugin opens the MIDI ports itself, because many DAWs filter out SysEx. Everything is saved with
the DAW project, or in the app settings when you run it standalone.

## Status

| Engine   | Librarian / transfer | Editor |
|----------|----------------------|--------|
| Lead     | yes | full: voices, filters, LFOs, envelopes, mod matrix, trigger matrix, wavetables, knobs |
| Bassline | yes | planned |
| Drum     | yes | planned |
| Multi    | yes | planned |
| Ensemble | –   | planned |

## Layout

- `Source/sid/`: plain C++ with no JUCE. Contains the patch model, SysEx codec, parameter tables and `.syx` files.
  The tests in `Tests/` cover this code.
- `Source/device/SidDevice`: MIDI ports, a request queue with timeouts, and rate-limited live parameter writes.
- `Source/model/Session`: the edit buffer, the bank, transfers, files and saved state.
- `Source/ui/`: the editor UI.

Developer aid: setting `SIDCONTROL_SNAPSHOT_DIR=/some/dir` makes the app render every editor page to PNG and quit.
You can also set `SIDCONTROL_SNAPSHOT_SYX` and `SIDCONTROL_SNAPSHOT_SLOT` to load a patch first.
