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
    for (int i = 0; i < 12000; ++i)
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
        for (int i = 0; i < 24000; ++i)
        {
            const auto slot = 1 + i % 3;
            Frame observed {};
            const auto output = tapped.process(input, slot, &observed);
            check(output == reference.process(input), "analysis tap never changes audio");
            if (i >= 23997)
            {
                const auto expected = slot == 3 ? (route == 0 ? amplified : capped) : input[0];
                check(std::abs(observed[0] - expected) < 1.0e-4f, "tap reads the current strip order");
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
    tonalBalance();
    std::cout << "Short Bus DSP checks passed\n";
}
