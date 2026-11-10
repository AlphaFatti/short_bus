# Short Bus

Short Bus is a free, open-source VST3 dynamics rack for a stereo mix bus. It puts four reorderable stages in a single plugin for OBS via [atkAudio](https://obsproject.com/forum/resources/atkaudio-plugin.2099/) or for a VST3 DAW. The first build targets Windows x64 and Apple Silicon.

| Stage | Controls |
| --- | --- |
| Expand / Lift | Downward expansion or upward compression below threshold, with a cap on the cut or lift |
| 5-Band | Five independent bands with adjustable edges, threshold, ratio, attack, release, knee, and upward or downward compression |
| Compressor | Stereo-linked broadband compression with knee, range and makeup gain |
| Limiter | Stereo-linked sample-peak limiting with 3 ms lookahead, drive, ceiling and release |

Select a stage card to edit its controls and use the **Move** arrows to change the signal order. Click **Stage on** to engage that stage. Stage **IN** and **OUT** show stereo-linked peaks just before and after that stage in the current order. The middle bar in Expand / Lift, Compressor, and Limiter shows the dynamics gain change. The 5-Band analyzer shows the stereo output of the whole rack, with the 5-Band input and output meters on its sides.

Drag the threshold left or right in **Expand / Lift** and **Compressor**. Below threshold, a vertical drag adjusts **Range** in Expand / Lift or **Makeup** in Compressor. Above the compressor threshold, drag down for a higher ratio or up for a lower one. In **Limiter**, drag the ceiling up or down.

In **5-Band**, click anywhere in a band to select it. Drag the circle up or down for threshold, the horizontal bar above zero to lift quiet audio or below zero to compress peaks, and the top edge tabs to resize the selected band. The bar sets the maximum gain change in dB. At zero, the band applies no gain change. The filters have gradual skirts, so a narrow band can apply less gain change at its center. The frequency sliders and typed values obey the same limits. Bands can meet at a boundary or leave gaps. Previously overlapping spans are trimmed at the neighboring band start.

The default order is Expand / Lift → 5-Band → Compressor → Limiter. Every stage starts off. Global **Bypass** preserves the plugin's delay so you can A/B without a timing jump. The three small roof lights glow while Short Bus is active and flare red when the final output reaches −1 dBFS. In bypass, the two large red lamps alternate with an incandescent fade. The limiter ceiling is measured at its own output in dBFS. For a ceiling on the final output, keep it at the end of the rack and leave downstream gain at unity. Its sample-peak detector measures discrete samples. The plugin reports approximately 3 ms of latency to the host in every order.

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

## Typeface

Short Bus uses a Clearview or ClearviewHwy family when it finds one among your installed fonts. Install your licensed font in Font Book on macOS or in Windows Fonts, then restart your VST3 host. The built-in fallback is [Overpass](https://github.com/RedHatOfficial/Overpass/tree/v3.0.5), distributed under the [SIL Open Font License](assets/Overpass-License.md).

## Tests and license

CTest runs the DSP checks and opens the built bundle in a VST3 test host. The checks cover curves and gain caps, stereo linking, frequency targeting, fixed latency, the limiter ceiling, live route changes, and VST3 state recall. To build the DSP tests without fetching JUCE, configure with `-DSHORT_BUS_BUILD_PLUGIN=OFF`.

Short Bus is licensed under [GNU AGPL v3](LICENSE). Its build fetches [JUCE 8.0.12](https://github.com/juce-framework/JUCE/tree/8.0.12) under JUCE's AGPL option.
