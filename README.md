# Short Bus

Short Bus is a free, open-source VST3 mastering rack for a stereo mix bus. It puts five reorderable audio stages and a movable analyzer in one plugin for OBS via atkAudio or for a VST3 DAW. Builds target Windows x64 and Apple Silicon. [atkAudio](https://obsproject.com/forum/resources/atkaudio-plugin.2099/)

| Stage | Controls |
| --- | --- |
| Expand / Lift | Downward expansion or upward compression below threshold, with a cap on the cut or lift |
| 5-Band | Five editable bands with upward or downward compression, or adaptive cut-only Assist with Speech and Music profiles |
| Compressor | Stereo-linked Clean, Opto or FET compression with knee, range and makeup gain |
| GEQ | 31 faders from 20 Hz to 20 kHz, ±12 dB per band, and a saturation knob after the filters |
| Limiter | Stereo-linked sample-peak limiting with a smooth 3 ms lookahead ramp, drive, ceiling, and fixed or adaptive release |
| RTA | Movable analyzer with tap peak and RMS, Fine and Broad views, an adjustable sensitivity floor and a Pink Slope reference |

Select a stage card to edit its controls. Drag any of the six cards to a new position, or use the **Move** arrows. A drag previews the order and commits it on release. Click **Stage on** to engage that stage. Stage **IN** and **OUT** show stereo peaks just before and after that stage in the current order. The middle bar in Expand / Lift, Compressor, and Limiter shows the dynamics gain change. The 5-Band analyzer shows the stereo output of the whole rack, with the 5-Band input and output meters on its sides.

**Reset** opens a confirmation for the selected stage. Confirm to restore that stage's factory settings, including its enable switch. Resetting the RTA also clears its captured reference.

The blue dot on the Expand / Lift, Compressor, and Limiter curves follows the current input level. The strip below each graph shows the last four seconds with the newest readings at the right. It shows input and output in dBFS, with actual gain change on its own dB scale for the dynamics stages. In 5-Band, the gain trace follows the selected band. The RTA strip shows the level at its current tap.

Drag the threshold left or right in **Expand / Lift** and **Compressor**. Below threshold, a vertical drag adjusts **Range** in Expand / Lift or **Makeup** in Compressor. Above the compressor threshold, drag down for a higher ratio or up for a lower one. In **Limiter**, drag the ceiling up or down.

The **Mode** selector below the compressor's gain readout offers **Clean**, **Opto** and **FET**. Clean uses the original calculation. Opto adds RMS sensing and recovery memory. FET uses fast feedback compression with a variable-resistance gain model. Attack and release settings are retained separately for each mode. [Model equations, tuning choices and public references](docs/compressor-models.md)

In **Limiter**, **Auto release** lengthens recovery during sustained limiting. Turn it off for a fixed release using W1's published timing convention. The Release slider sets the base speed in both modes. Older sessions and presets receive Auto release when loaded in v0.1.12. [Limiter response and comparison notes](docs/limiter.md)

In **5-Band**, click anywhere in a band to select it. Drag the circle up or down for threshold, the horizontal bar above zero to lift quiet audio or below zero to compress peaks, and the top edge tabs to resize the selected band. The bar sets the maximum gain change in dB. At zero, the band applies no gain change. The filters have gradual skirts, so a narrow band can apply less gain change at its center. The frequency sliders and typed values obey the same limits. Bands can meet at a boundary or leave gaps. Previously overlapping spans are trimmed at the neighboring band start.

To try **Assist**, enable 5-Band, turn on **Assist**, and choose **Speech** for spoken word or **Music** for the full band. Assist uses five fixed bands, watches for an excessive share of energy in a band, and shows the cut it applies to each band. **Focus** ranges from unity at 0% to cuts of up to 3 dB per band at 100%. The effect releases during quiet gaps. Turn Assist off to return to your manual settings.

In **GEQ**, move each fader to cut or boost its frequency. Double-click a fader to center it or use **Flat EQ** to center the whole bank. **Saturation** sets how strongly the stage rounds peaks after the filters. The blue RTA bars show the signal after the GEQ and saturation, using the dBFS scale at the right. The RTA Sensitivity floor applies here too. Bands at or above Nyquist are dimmed at lower sample rates.

Select the **RTA** tile and use **Move** to check the rack input, output, or the signal between two stages. The display shows peak and RMS at the tap in dBFS. **Sensitivity** sets the RMS floor for the spectrum and capture. Raise it above your idle noise level to quiet the trace. **Fine** shows per-band RMS and a level-matched target range. Hover over a band to see its frequency span, level and dB difference from the target. **Broad** shows this difference across four wider regions.

Turn on **Pink Slope** to compare against pink noise at the same in-band energy. The reference is level across the equally spaced log-frequency bands because each covers the same fraction of an octave. With Pink Slope off, choose Balanced, Bass-forward, or Speech, or press **Capture** while playing audio to save your own target. Move the RTA to another point to compare against that capture.

The **PRESETS** license plate opens the bus door. The upper window saves and recalls the selected unit. The lower **Bus Presets** window saves the whole rack, including its order, RTA position and captured reference. Select an entry and press **Load**, or double-click it. **Safes** chooses which sections keep their current settings during Bus recall. Bypass is safe by default, and safe choices recall with the host project.

Preset files use the **.shortbus** extension in a **Short Bus Presets** folder beside the VST3. A protected install folder uses a shared user preset folder. The **Folder** button opens the location in use. Presets appear in every host using that folder. Saving the same name creates a numbered copy.

The default order is Expand / Lift → 5-Band → Compressor → GEQ → Limiter. Every stage starts off. Global **Bypass** preserves the plugin's delay so you can A/B without a timing jump. The three small roof lights glow while Short Bus is active and flare red when the final output reaches −1 dBFS. In bypass, the two large red lamps alternate with an incandescent fade. The limiter ceiling is measured at its own output in dBFS. For a ceiling on the final output, keep it at the end of the rack and leave downstream gain at unity. Its sample-peak detector measures discrete samples. The plugin reports approximately 3 ms of latency to the host in every order.

## Install a preview

Download the ZIP for Apple Silicon or Windows x64 and follow the install steps on the release page. [GitHub Releases](https://github.com/AlphaFatti/short_bus/releases)

## Install in OBS 33 on Windows 11

1. Download and extract the Windows x64 ZIP from the [release page](https://github.com/AlphaFatti/short_bus/releases).
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
