# Short Bus

Short Bus is a free, open-source VST3 dynamics rack for a stereo mix bus. It puts four reorderable stages in a single plugin for OBS via [atkAudio](https://obsproject.com/forum/resources/atkaudio-plugin.2099/) or for a VST3 DAW. The first build targets Windows x64 and Apple Silicon.

| Stage | Controls |
| --- | --- |
| Expand / Lift | Downward expansion or upward compression below threshold, with a cap on the cut or lift |
| 5-Band | Five independently enabled, movable compression bands with threshold, ratio, attack, release, knee and maximum cut |
| Compressor | Stereo-linked broadband compression with knee, range and makeup gain |
| Limiter | Stereo-linked sample-peak limiting with 3 ms lookahead, drive, ceiling and release |

Select a stage card to edit its controls and use the **Move** arrows to change the signal order. Click **Stage on** to engage that stage. In **5-Band**, select an individual band to drag its edges, type frequencies, or toggle the band. The analyzer shows the stereo output of the whole rack. The band meters report the requested gain change. The filters have gradual skirts, so a narrow band can apply less cut at its center and overlapping bands can combine their cuts.

The default order is Expand / Lift → 5-Band → Compressor → Limiter. Every stage starts off. Global **Bypass** preserves the plugin's delay so you can A/B without a timing jump. The limiter ceiling is measured at its own output in dBFS. For a ceiling on the final output, keep it at the end of the rack and leave downstream gain at unity. Its sample-peak detector measures discrete samples. The plugin reports approximately 3 ms of latency to the host in every order.

## Install in OBS 33 on Windows 11

1. Download the **Short-Bus-Windows-x64** artifact from a successful run of [Build Short Bus VST3](https://github.com/AlphaFatti/short_bus/actions/workflows/build.yml) and extract the plugin archive inside it.
2. Copy the entire **Short Bus.vst3** folder into `C:\Program Files\Common Files\VST3`.
3. In OBS, open the filters for the stereo source that carries your stream bus. Add **atkAudio PluginHost** or **PluginHost2**, scan VST3 plugins, and select **Short Bus**. Open its editor from the host.
4. Play a recorded service or a music-and-speech test mix, then enable and tune the stages you need. Check audio/video sync and confirm that your settings and order return after an OBS restart before going live.

## Develop on Apple Silicon

Install Git, CMake 3.22 or newer, and Apple's Command Line Tools. If the macOS SDK is missing, use `xcode-select --install`. Run these commands from the repo root. The Makefiles generator works with the standalone tools.

```sh
cmake -S . -B build-mac -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 &&
cmake --build build-mac --parallel 2 &&
ctest --test-dir build-mac --output-on-failure &&
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" &&
ditto "build-mac/ShortBus_artefacts/Release/VST3/Short Bus.vst3" \
  "$HOME/Library/Audio/Plug-Ins/VST3/Short Bus.vst3"
```

Rescan in your host. The macOS Actions build produces an Apple Silicon development bundle.

## Build on Windows

Install Git, CMake 3.22 or newer, and Visual Studio 2022 with the **Desktop development with C++** workload.

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --parallel 2
ctest --test-dir build-win -C Release --output-on-failure
```

The bundle is at `build-win/ShortBus_artefacts/Release/VST3/Short Bus.vst3`.

## Tests and license

CTest runs the DSP checks and opens the built bundle in a VST3 test host. The checks cover curves and gain caps, stereo linking, frequency targeting, fixed latency, the limiter ceiling, live route changes, and VST3 state recall. To build the DSP tests without fetching JUCE, configure with `-DSHORT_BUS_BUILD_PLUGIN=OFF`.

Short Bus is licensed under [GNU AGPL v3](LICENSE). Its build fetches [JUCE 8.0.12](https://github.com/juce-framework/JUCE/tree/8.0.12) under JUCE's AGPL option.
