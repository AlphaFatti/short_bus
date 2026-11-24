// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/Rack.h"
#include "dsp/TonalBalance.h"

#include <cstdlib>
#include <iostream>
#include <random>

using namespace shortbus::dsp;

namespace
{
void check(bool ok, const char* name)
{
    if (!ok)
    {
        std::cerr << "Failed: " << name << '\n';
        std::exit(1);
    }
}

void dynamicsCurves()
{
    DynamicsSettings settings { true, -24.0f, 2.0f, 10.0f, 100.0f, 0.0f, 9.0f, 0.0f };
    check(staticGain(-30.0f, settings, Curve::expand) == -6.0f, "downward expansion ratio");
    check(staticGain(-30.0f, settings, Curve::lift) == 3.0f, "upward compression ratio");
    check(staticGain(-12.0f, settings, Curve::compress) == -6.0f, "downward compression ratio");
    check(staticGain(-100.0f, settings, Curve::expand) == -9.0f, "expansion range");
    check(staticGain(-100.0f, settings, Curve::lift) == 9.0f, "lift range");
    check(staticGain(-30.0f, settings, Curve::compress) == 0.0f, "compression under threshold");
    settings.knee = 8.0f;
    check(staticGain(-24.0f, settings, Curve::compress) == -0.5f, "soft knee");

    Broadband broadband;
    for (auto shape : { Curve::expand, Curve::lift, Curve::compress })
    {
        broadband = {};
        broadband.configure(48000.0f, settings, shape);
        const auto input = shape == Curve::compress ? 0.5f : 0.025f;
        Frame result {};
        for (int i = 0; i < 48000; ++i)
            result = broadband.process({ input, -0.25f * input });
        check(std::abs(result[1] + 0.25f * result[0]) < 1.0e-6f, "linked stereo broadband");
        check((shape == Curve::lift) ? result[0] > input : result[0] < input, "broadband gain direction");
    }
}

float toneLevel(BandSettings settings, float frequency, int rate, bool enabled, float amplitude = 0.5f)
{
    constexpr float pi = 3.14159265358979323846f;
    Band band;
    band.configure(static_cast<float>(rate), settings, enabled);
    double in = 0.0, out = 0.0;
    for (int i = 0; i < rate; ++i)
    {
        const auto tone = amplitude * std::sin(2.0f * pi * frequency * static_cast<float>(i) / static_cast<float>(rate));
        const Frame input { tone, -tone * 0.25f };
        const auto output = band.process(input, input);
        check(std::isfinite(output[0]), "band output finite");
        check(std::abs(output[1] + 0.25f * output[0]) < 1.0e-6f, "linked stereo band");
        if (i > rate / 2)
        {
            in += static_cast<double>(input[0]) * input[0];
            out += static_cast<double>(output[0]) * output[0];
        }
    }
    return static_cast<float>(10.0 * std::log10(out / in));
}

void movableBands()
{
    auto settings = bandDefaults[2];
    settings.lowHz = 300.0f;
    settings.highHz = 3000.0f;
    settings.dynamics.threshold = -55.0f;
    settings.dynamics.ratio = 4.0f;
    settings.dynamics.range = 6.0f;
    for (auto rate : { 44100, 48000, 96000 })
    {
        check(toneLevel(settings, 950.0f, rate, false) == 0.0f, "bypassed band unity");
        const auto center = toneLevel(settings, 950.0f, rate, true);
        check(center < -4.0f && center >= -6.01f, "in-band compression");
        check(std::abs(toneLevel(settings, 40.0f, rate, true)) < 0.3f, "below-band unity");
        check(std::abs(toneLevel(settings, 12000.0f, rate, true)) < 0.6f, "above-band unity");

        auto lift = settings;
        lift.lift = true;
        lift.dynamics.threshold = -24.0f;
        const auto boosted = toneLevel(lift, 950.0f, rate, true, 0.01f);
        check(boosted > 4.0f && boosted <= 6.01f, "quiet in-band signal lifts within range");
        check(std::abs(toneLevel(lift, 950.0f, rate, true)) < 0.01f, "lift leaves above-threshold signal at unity");
        check(std::abs(toneLevel(lift, 40.0f, rate, true, 0.01f)) < 0.6f, "lift targets the selected band");
        lift.dynamics.range = 0.0f;
        check(toneLevel(lift, 950.0f, rate, true, 0.01f) == 0.0f, "zero signed range is unity");
    }

    Band moving;
    moving.configure(48000.0f, settings, true);
    float previous = 0.0f;
    for (int i = 0; i < 36000; ++i)
    {
        if (i == 18000)
        {
            settings.lowHz = 20.0f;
            settings.highHz = 20000.0f;
            moving.configure(48000.0f, settings, true);
        }
        const auto input = Frame { 0.4f, -0.2f };
        const auto output = moving.process(input, input);
        if (i > 0)
            check(std::abs(output[0] - previous) < 0.008f, "smooth edge changes");
        previous = output[0];
    }
}

void nonOverlappingBands()
{
    const auto defaults = orderedBandSpans(bandDefaults, 48000.0f);
    for (std::size_t i = 0; i < defaults.size(); ++i)
    {
        check(defaults[i].low == bandDefaults[i].lowHz, "default band start preserved");
        check(defaults[i].high == bandDefaults[i].highHz, "default band end preserved");
    }

    auto overlapping = bandDefaults;
    overlapping[1].lowHz = 101.0f;
    overlapping[1].highHz = 352.0f;
    const auto restored = orderedBandSpans(overlapping, 48000.0f);
    check(restored[0].high < restored[1].low, "band gaps allowed");
    check(restored[1].high == restored[2].low && restored[1].high == 350.0f, "old overlap trimmed at the next start");

    auto crossing = bandDefaults;
    for (std::size_t i = 0; i < crossing.size(); ++i)
    {
        crossing[i].lowHz = i % 2 == 0 ? maxHz : minHz;
        crossing[i].highHz = i % 2 == 0 ? minHz : maxHz;
    }
    for (auto rate : { 8000.0f, 44100.0f, 48000.0f, 96000.0f })
    {
        const auto spans = orderedBandSpans(crossing, rate);
        auto previousHigh = minHz;
        for (const auto span : spans)
        {
            check(span.low >= previousHigh, "host edge values cannot overlap");
            check(span.high >= span.low * edgeRatio, "minimum band width kept");
            check(span.high <= static_cast<float>(bandCeilingHz(rate)), "band limited to current sample rate");
            previousHigh = span.high;
        }
    }

    check(constrainBandFrequency(defaults, 1, BandEdge::low, 40.0f, 48000.0f) == 100.0f,
        "low edge stops at previous band");
    check(constrainBandFrequency(defaults, 1, BandEdge::high, 400.0f, 48000.0f) == 350.0f,
        "high edge stops at next band");
    check(constrainBandFrequency(defaults, 1, BandEdge::high, 40.0f, 48000.0f) == 112.0f,
        "high edge respects its own minimum width");
}

void fixedLatency()
{
    Rack rack;
    Settings settings;
    const Frame impulse { 0.8f, -0.5f };
    for (auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        for (int order = 0; order < orderCount; ++order)
        {
            rack.prepare(rate);
            settings.order = order;
            settings.bypass = order % 2 != 0;
            rack.configure(settings);
            const auto delay = static_cast<int>(std::lround(rate * 0.003));
            check(rack.latency() == delay, "reported lookahead latency");
            for (int i = 0; i < 2 * delay; ++i)
                check(rack.process(i == 0 ? impulse : Frame {}) == (i == delay ? impulse : Frame {}),
                      "sample-exact unity through order and bypass");
        }
    }
}

void peakCeiling()
{
    std::mt19937 random(935);
    std::uniform_real_distribution<float> samples(-3.0f, 3.0f);
    for (auto delay : { 144, maxDelay })
    {
        Limiter limiter;
        const auto rate = static_cast<float>(delay) / 0.003f;
        limiter.prepare(rate, delay);
        limiter.configure({ true, -1.0f, 9.0f, 100.0f });
        const auto ceiling = gainFromDb(-1.0f);
        for (int i = 0; i < delay * 4; ++i)
        {
            // A strictly descending run exercises the full deque capacity.
            const auto level = 3.0f - static_cast<float>(i) / static_cast<float>(delay * 2);
            const auto input = i < delay * 2 ? Frame { level, -level * 0.5f }
                                              : Frame { samples(random), samples(random) };
            const auto output = limiter.process(input);
            check(std::isfinite(peak(output)) && peak(output) <= ceiling + 2.0e-6f, "sample-peak ceiling");
            if (i < delay)
                check(output == Frame {}, "lookahead delay begins with silence");
        }
    }
}

void liveReorder()
{
    Rack rack;
    rack.prepare(48000.0);
    Settings settings;
    settings.compressor.enabled = true;
    settings.compressor.ratio = 1.0f;
    settings.compressor.makeup = 6.0f;
    settings.limiter = { true, -3.0f, 0.0f, 30.0f };
    rack.configure(settings);
    const Frame input { 0.62f, -0.31f };
    const auto checkMeters = [&]
    {
        rack.configure(settings);
        check(rack.meters().stages[index(Stage::lowLevel)].inputPeak == 0.0f, "stage peaks reset for each block");
        auto last = 0.0f;
        for (int i = 0; i < 256; ++i)
            last = rack.process(input)[0];
        const auto stages = rack.meters().stages;
        const auto route = orders[static_cast<std::size_t>(settings.order)];
        check(stages[index(route.front())].inputPeak == input[0], "first stage meters rack input");
        for (std::size_t i = 1; i < route.size(); ++i)
            check(stages[index(route[i - 1])].outputPeak == stages[index(route[i])].inputPeak,
                "each stage meters the previous stage's output");
        check(std::abs(stages[index(route.back())].outputPeak - last) < 0.001f, "last stage meters rack output");
        const auto comp = stages[index(Stage::compressor)];
        check(comp.outputPeak > comp.inputPeak && std::abs(comp.gainDb) < 1.0e-5f,
            "makeup affects output peak without reporting compression");
    };
    for (int i = 0; i < 48000; ++i)
        rack.process(input);
    const auto before = rack.process(input)[0];
    check(std::abs(before - gainFromDb(-3.0f)) < 0.001f, "limiter last");
    checkMeters();
    settings.order = 1;
    rack.configure(settings);
    auto previous = before;
    // Allow adaptive recovery to settle after moving the limiter ahead of makeup gain.
    for (int i = 0; i < 72000; ++i)
    {
        const auto output = rack.process(input);
        check(std::abs(output[0] - previous) < 0.002f, "continuous reorder");
        check(std::abs(output[1] + output[0] * 0.5f) < 1.0e-6f, "linked stereo during reorder");
        previous = output[0];
    }
    check(previous > 1.2f, "makeup can raise final level when limiter is earlier");
    checkMeters();

    for (int block = 0; block < 220; ++block)
    {
        settings.order = block < 100 ? block % orderCount : 0;
        rack.configure(settings);
        for (int sample = 0; sample < 64; ++sample)
            previous = rack.process(input)[0];
    }
    check(std::abs(previous - gainFromDb(-3.0f)) < 0.001f, "latest route wins after rapid changes");
    settings.bypass = true;
    rack.configure(settings);
    for (int i = 0; i < 8000; ++i)
        previous = rack.process(input)[0];
    check(std::abs(previous - input[0]) < 1.0e-6f, "latency-matched bypass");
}

void analyzerTap()
{
    Settings settings;
    settings.compressor.enabled = true;
    settings.compressor.ratio = 1.0f;
    settings.compressor.makeup = 6.0f;
    settings.limiter = { true, -3.0f, 0.0f, 30.0f };
    constexpr Frame input { 0.85f, -0.425f };
    const auto capped = gainFromDb(-3.0f);
    const auto amplified = input[0] * gainFromDb(6.0f);
    for (const auto route : { 0, 1 })
    {
        Rack tapped, reference;
        tapped.prepare(48000.0);
        reference.prepare(48000.0);
        settings.order = route;
        tapped.configure(settings);
        reference.configure(settings);
        const auto expected = route == 0 ? std::array { input[0], input[0], amplified, amplified }
                                         : std::array { input[0], input[0], input[0], capped };
        for (int i = 0; i < 24000; ++i)
        {
            const auto slot = 1 + i % 4;
            Frame observed {};
            const auto output = tapped.process(input, slot, &observed);
            check(output == reference.process(input), "analysis tap never changes audio");
            if (i >= 23996)
            {
                check(std::abs(observed[0] - expected[static_cast<std::size_t>(slot - 1)]) < 1.0e-4f,
                      "tap reads the current strip order");
                check(std::abs(observed[1] + 0.5f * observed[0]) < 1.0e-6f, "tap preserves stereo channels");
            }
        }
    }
}

void tonalBalance()
{
    constexpr int fftSize = 16384;
    constexpr float rate = 48000.0f;
    std::array<float, fftSize / 2 + 1> left {}, right {};
    left[256] = 512.0f;
    right[256] = 512.0f;
    right[2048] = 256.0f;
    const auto power = tonalBandPower(left.data(), right.data(), fftSize, rate);
    const auto curve = normalizedTone(power);
    const auto band = static_cast<std::size_t>(std::upper_bound(tonalEdges.begin(), tonalEdges.end(), 750.0f)
                                               - tonalEdges.begin() - 1);
    check(std::abs(curve[band] - 8.0f / 9.0f) < 1.0e-6f, "tonal spectrum integrates stereo band power");
    for (auto& value : left)
        value *= 2.0f;
    for (auto& value : right)
        value *= 2.0f;
    check(normalizedTone(tonalBandPower(left.data(), right.data(), fftSize, rate)) == curve,
        "tonal comparison is independent of gain");
    for (const auto& target : tonalTargets)
        check(std::abs(tonalEnergy(target) - 1.0f) < 1.0e-6f, "starter targets are normalized");
    const auto usable = tonalBandsAtRate(32000.0f);
    check(tonalBandsAtRate(rate) == tonalBandCount && usable < tonalBandCount, "analyzer respects Nyquist");
    const auto limited = normalizedTone(tonalTargets[0], usable);
    check(std::abs(tonalEnergy(limited) - 1.0f) < 1.0e-6f && limited.back() == 0.0f,
        "reference comparison uses the available bandwidth");

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = (i % 2 == 0 ? 1.0f : -1.0f) * gainFromDb(-76.0f);
        right[i] = -left[i];
    }
    const auto samples = static_cast<int>(left.size());
    const auto quiet = spectrumLevels(left.data(), right.data(), samples);
    check(std::abs(dbFromGain(quiet.rms) + 76.0f) < 0.01f && std::abs(quiet.peak - quiet.rms) < 1.0e-8f,
          "tap RMS and peak preserve opposite-polarity stereo audio");
    check(!tonalAboveFloor(quiet, defaultRtaFloorDb, true),
          "idle noise closes an active analyzer");
    check(tonalAboveFloor(quiet, -85.0f, false),
          "lowering the floor shows quiet stereo audio without phase cancellation");

    // Ideal Hann-windowed magnitudes for a bin-centred tone with amplitude 0.5.
    left.fill(0.0f);
    left[255] = left[257] = fftSize / 16.0f;
    left[256] = fftSize / 8.0f;
    right = left;
    const auto tone = tonalBandPower(left.data(), right.data(), fftSize, rate);
    check(std::abs(tone[band] * hannPowerToMeanSquare - 0.125f) < 1.0e-6f, "RTA bands report calibrated RMS power");
    for (std::size_t i = 1; i < left.size(); ++i)
        left[i] = right[i] = 256.0f / std::sqrt(static_cast<float>(i));
    const auto pink = tonalBandPower(left.data(), right.data(), fftSize, rate);
    const auto matched = levelMatchedTone(pinkTonalTarget, pink, tonalBandCount);
    check(std::abs(tonalEnergy(matched) - tonalEnergy(pink)) < 1.0e-6f, "pink reference matches in-band signal energy");
    for (std::size_t i = 0; i < pink.size(); ++i)
        check(std::abs(tonalDb(pink[i]) - tonalDb(matched[i])) < 0.2f, "pink noise is flat across equal log bands");
    check(std::abs(toneDifferenceDb(1.0e-10f, 2.0e-10f) - toneDifferenceDb(0.01f, 0.02f)) < 0.001f,
          "comparisons are level independent below the absolute display floor");

    left.fill(0.0f);
    right.fill(0.0f);
    left[341] = 4096.0f;
    right[341] = 4096.0f;
    const auto eqPower = geqBandPower(left.data(), right.data(), fftSize, rate);
    check(eqPower[17] > 0.06f && eqPower[16] < 1.0e-9f && eqPower[18] < 1.0e-9f,
          "GEQ RTA places a 1 kHz signal under the 1 kHz fader");
    left[8192] = 4096.0f;
    right[8192] = 4096.0f;
    check(geqBandPower(left.data(), right.data(), fftSize, 32000.0f).back() < 1.0e-9f,
          "GEQ RTA skips bands above Nyquist");
}

void assistedBands()
{
    struct Reading { BandLevels bands; float changeDb; };
    const auto measure = [](float rate, float frequency, float amplitude, AssistProfile profile,
                            float focus, bool enabled, int blockSize)
    {
        Multiband multiband;
        multiband.prepare(rate);
        Settings settings;
        settings.multiEnabled = enabled;
        settings.assist = { true, profile, focus };
        settings.bands[0].lift = true;
        settings.bands[0].dynamics.threshold = 0.0f;
        float inputPower = 0.0f, outputPower = 0.0f;
        const auto samples = static_cast<int>(rate);
        const auto radiansPerSample = 6.28318530718f * frequency / rate;
        for (int i = 0; i < samples; ++i)
        {
            if (i % blockSize == 0)
                multiband.configure(settings);
            const auto sample = amplitude * std::sin(radiansPerSample * static_cast<float>(i));
            const Frame input { sample, -0.5f * sample };
            const auto output = multiband.process(input);
            check(std::abs(0.5f * output[0] + output[1]) < 1.0e-6f, "Assist is stereo linked");
            if (!enabled || focus == 0.0f || amplitude < 0.001f)
                check(output == input, "disabled, zero Focus or quiet Assist is neutral");
            if (i >= samples / 2)
            {
                inputPower += input[0] * input[0];
                outputPower += output[0] * output[0];
            }
        }
        BandLevels bands {};
        for (std::size_t i = 0; i < bands.size(); ++i)
        {
            bands[i] = multiband.meter(i);
            check(bands[i] <= 0.001f && bands[i] >= -assistMaxCutDb - 0.001f, "Assist cuts are limited");
        }
        return Reading { bands, tonalDb(outputPower / inputPower) };
    };
    for (const auto rate : { 44100.0f, 48000.0f, 96000.0f })
    {
        const auto bass = measure(rate, 85.0f, 0.2f, AssistProfile::speech, 100.0f, true, 127);
        const auto musicBass = measure(rate, 85.0f, 0.2f, AssistProfile::music, 100.0f, true, 513);
        const auto lowMids = measure(rate, 190.0f, 0.2f, AssistProfile::speech, 100.0f, true, 127);
        const auto treble = measure(rate, 10000.0f, 0.2f, AssistProfile::speech, 100.0f, true, 127);
        const auto mids = measure(rate, 850.0f, 0.2f, AssistProfile::music, 100.0f, true, 127);
        check(bass.bands[0] < -1.0f && bass.changeDb < -0.5f, "Assist attenuates dominant bass");
        check(musicBass.bands[0] > bass.bands[0] + 0.2f, "Music allows more bass than Speech");
        check(lowMids.bands[1] < -0.5f && lowMids.bands[0] > -0.1f, "Assist distinguishes low mids from bass");
        check(std::abs(treble.changeDb) < 0.01f && std::abs(mids.changeDb) < 0.01f,
              "steady mids and treble retain their tone");
        const auto half = measure(rate, 85.0f, 0.2f, AssistProfile::speech, 50.0f, true, 513);
        check(std::abs(half.bands[0] - 0.5f * bass.bands[0]) < 0.002f, "Focus scales the cut across block sizes");
    }
    measure(48000.0f, 65.0f, 0.2f, AssistProfile::speech, 0.0f, true, 127);
    measure(48000.0f, 65.0f, 0.2f, AssistProfile::speech, 100.0f, false, 127);
    measure(48000.0f, 65.0f, 0.0004f, AssistProfile::speech, 100.0f, true, 127);

    Multiband transient;
    transient.prepare(48000.0f);
    Settings settings;
    settings.multiEnabled = true;
    settings.assist = { true, AssistProfile::music, 100.0f };
    transient.configure(settings);
    const auto tone = [](int sample, float hz, float amplitude)
    {
        return amplitude * std::sin(6.28318530718f * hz * static_cast<float>(sample) / 48000.0f);
    };
    float deepestCut = 0.0f;
    for (int i = 0; i < 192000; ++i)
    {
        auto sample = tone(i, 850.0f, 0.12f);
        if (i >= 48000 && i < 60000)
            sample += tone(i, 10000.0f, 0.30f);
        transient.process({ sample, -sample });
        deepestCut = std::min(deepestCut, transient.meter(4));
    }
    check(deepestCut < -0.15f && deepestCut >= -0.301f, "brief bright peaks get a small, bounded cut");
    check(std::abs(transient.meter(4)) < 0.01f, "treble cut releases after a bright peak");

    Multiband manual, withAssist;
    manual.prepare(48000.0f);
    withAssist.prepare(48000.0f);
    settings.assist.enabled = false;
    for (auto& band : settings.bands)
        band.dynamics.enabled = false;
    auto& high = settings.bands.back();
    high.lowHz = 6100.0f;
    high.dynamics.enabled = true;
    high.lift = true;
    high.dynamics.threshold = 0.0f;
    high.dynamics.range = 12.0f;
    manual.configure(settings);
    settings.assist = { true, AssistProfile::music, 0.0f, true };
    withAssist.configure(settings);
    float lift = 0.0f;
    for (int i = 0; i < 144000; ++i)
    {
        if (i == 48000)
        {
            settings.assist.focus = 100.0f;
            withAssist.configure(settings);
        }
        if (i == 96000)
        {
            settings.assist.keepManual = false;
            withAssist.configure(settings);
        }
        const auto sample = tone(i, 11000.0f, 0.02f);
        const Frame input { sample, -sample };
        const auto manualOutput = manual.process(input);
        const auto assistOutput = withAssist.process(input);
        if (i < 96000)
            check(assistOutput == manualOutput, "Keep Manual preserves custom bright voicing through Assist");
        if (i > 130000)
            check(assistOutput == input, "turning off Keep Manual returns to standalone Assist");
        if (i > 36000 && i < 48000)
            lift = std::max(lift, std::abs(manualOutput[0]));
    }
    check(lift > 0.04f, "test voicing includes substantial treble lift");
}

void graphicEq()
{
    constexpr double twoPi = 6.2831853071795864769;
    for (const auto rate : { 44100, 48000, 96000 })
    {
        for (const auto band : { std::size_t { 0 }, std::size_t { 17 }, geqBandCount - 1 })
        {
            for (const auto gain : { -12.0f, 12.0f })
            {
                GraphicEq eq;
                eq.prepare(static_cast<float>(rate));
                GraphicEqSettings settings;
                settings.enabled = true;
                settings.gains[band] = gain;
                eq.configure(settings);
                double inputPower = 0.0, outputPower = 0.0;
                for (int i = 0; i < rate * 2; ++i)
                {
                    const auto tone = static_cast<float>(0.1 * std::sin(twoPi * geqFrequencies[band] * i / rate));
                    const auto output = eq.process({ tone, -0.5f * tone });
                    check(std::isfinite(output[0]) && std::abs(output[1] + 0.5f * output[0]) < 1.0e-6f,
                          "GEQ preserves stereo balance and stays finite");
                    if (i >= rate)
                    {
                        inputPower += tone * tone;
                        outputPower += output[0] * output[0];
                    }
                }
                check(std::abs(10.0 * std::log10(outputPower / inputPower) - gain) < 0.08,
                      "GEQ reaches the selected gain at the band center");
            }
        }
    }

    GraphicEq eq;
    eq.prepare(32000.0f);
    GraphicEqSettings settings;
    settings.enabled = true;
    settings.gains.back() = 12.0f;
    eq.configure(settings);
    constexpr Frame input { 0.8f, -0.8f };
    check(eq.process(input) == input, "flat GEQ and bands above Nyquist pass audio exactly");

    eq.prepare(48000.0f);
    settings.gains.fill(0.0f);
    settings.saturation = 100.0f;
    eq.configure(settings);
    double fundamental = 0.0, harmonic = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        const auto phase = twoPi * i / 48.0;
        const auto tone = static_cast<float>(0.8 * std::sin(phase));
        const auto output = eq.process({ tone, -tone });
        fundamental += output[0] * std::sin(phase);
        harmonic += output[0] * std::sin(phase * 3.0);
        check(std::abs(output[0]) <= std::abs(tone) && output[0] == -output[1],
              "saturation rounds peaks symmetrically");
    }
    check(fundamental > 0.0 && std::abs(harmonic) > 0.04 * fundamental,
          "saturation adds a third harmonic");
    settings.saturation = 0.0f;
    eq.configure(settings);
    Frame output {};
    for (int i = 0; i < 8000; ++i)
        output = eq.process(input);
    check(output == input, "returning saturation to zero restores the flat signal exactly");
}
} // namespace

int main()
{
    dynamicsCurves();
    movableBands();
    nonOverlappingBands();
    fixedLatency();
    peakCeiling();
    liveReorder();
    analyzerTap();
    assistedBands();
    tonalBalance();
    graphicEq();
    std::cout << "Short Bus DSP checks passed\n";
}
