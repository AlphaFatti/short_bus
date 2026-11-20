// SPDX-License-Identifier: AGPL-3.0-only
#include "Rack.h"

namespace shortbus::dsp
{
void Delay::prepare(int delaySamples)
{
    frames.fill({});
    size = delaySamples;
    cursor = 0;
}

Frame Delay::push(Frame input)
{
    const auto slot = static_cast<std::size_t>(cursor);
    const auto output = frames[slot];
    frames[slot] = input;
    if (++cursor == size)
        cursor = 0;
    return output;
}

void Limiter::MovingAverage::prepare(int length)
{
    values.fill(0.0);
    size = length;
    cursor = 0;
    sum = 0.0;
}

double Limiter::MovingAverage::process(double input)
{
    const auto slot = static_cast<std::size_t>(cursor);
    sum += input - values[slot];
    values[slot] = input;
    if (++cursor == size)
        cursor = 0;
    return std::clamp(sum / size, 0.0, 1.0);
}

void Limiter::prepare(float rate, int samples)
{
    samples = std::clamp(samples, 1, maxDelay);
    delay.prepare(samples);
    head = tail = 0;
    frame = 0;
    delaySamples = static_cast<std::uint64_t>(samples);
    sampleRate = rate;
    firstAverage.prepare(samples / 2 + 1);
    secondAverage.prepare(samples - samples / 2 + 1);
    memoryAttack = timeCoefficient(rate, 50.0f);
    memoryRelease = timeCoefficient(rate, 250.0f);
    slew = timeCoefficient(rate, 10.0f);
    gain = 1.0f;
    reduction = activity = 0.0;
    configured = false;
}

void Limiter::configure(LimiterSettings settings)
{
    ceiling = gainFromDb(settings.ceiling);
    driveTarget = gainFromDb(settings.drive);
    // W1's published fixed release convention is three times the indicated time at RT60.
    constexpr double log1000 = 6.907755278982137;
    const auto rt60 = 3.0 * settings.release;
    release = -std::expm1(-1000.0 * log1000 / (sampleRate * rt60));
    slowRelease = timeCoefficient(sampleRate, std::max(250.0f, 4.0f * settings.release));
    adaptive = settings.adaptive;
    mixTarget = settings.enabled ? 1.0f : 0.0f;
    if (!configured)
    {
        drive = driveTarget;
        mix = mixTarget;
        configured = true;
    }
}

float Limiter::trackPeak(float amplitude)
{
    // A monotonic deque gives the maximum across the delayed sample and its lookahead.
    if (head != tail && frame - peaks[head].frame > delaySamples)
        head = next(head);
    while (head != tail && peaks[previous(tail)].value <= amplitude)
        tail = previous(tail);
    peaks[tail] = { frame++, amplitude };
    tail = next(tail);
    return peaks[head].value;
}

Frame Limiter::process(Frame input)
{
    const auto delayed = delay.push(input);
    const auto futurePeak = trackPeak(peak(input));
    drive += slew * (driveTarget - drive);
    mix = smoothMix(mix, mixTarget, slew);
    const auto wanted = std::min(1.0f, ceiling / std::max(1.0e-9f, drive * futurePeak));
    const auto required = 1.0 - wanted;
    activity += (reduction > activity ? memoryAttack : memoryRelease) * (reduction - activity);
    const auto sustained = adaptive ? std::clamp(4.0 * activity, 0.0, 1.0) : 0.0;
    const auto recovery = release + sustained * (slowRelease - release);
    reduction = std::max(required, reduction * (1.0 - recovery));

    // The finite smoothing support fits inside the peak hold. Every peak reaches
    // the output only after the complete gain ramp, without a normal-operation clamp.
    const auto smoothed = secondAverage.process(firstAverage.process(reduction));
    gain = static_cast<float>(1.0 - smoothed);
    if (mix <= 0.0f)
        return delayed;
    // Catch abrupt ceiling/drive automation and floating-point roundoff.
    gain = std::min(gain, ceiling / std::max(1.0e-9f, drive * peak(delayed)));
    const auto limited = scale(delayed, drive * gain);
    return blend(delayed, limited, mix);
}

void Rack::Chain::prepare(float rate, int delay)
{
    sampleRate = rate;
    lowLevel = {};
    compressor = {};
    bands = {};
    stageMeters = {};
    limiter.prepare(rate, delay);
    geq.prepare(rate);
}

void Rack::Chain::configure(const Settings& settings)
{
    stageMeters = {};
    lowLevel.configure(sampleRate, settings.lowLevel,
        settings.lowMode == LowMode::expand ? Curve::expand : Curve::lift);
    compressor.configure(sampleRate, settings.compressor, settings.compressorMode, settings.optoTiming, settings.fetTiming);
    const auto spans = orderedBandSpans(settings.bands, sampleRate);
    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        auto band = settings.bands[i];
        band.lowHz = spans[i].low;
        band.highHz = spans[i].high;
        bands[i].configure(sampleRate, band, settings.multiEnabled);
    }
    limiter.configure(settings.limiter);
    geq.configure(settings.geq);
}

Frame Rack::Chain::process(Frame input, int tap, Frame* tapped)
{
    auto amplitude = peak(input);
    for (auto stage : order)
    {
        auto& meter = stageMeters[index(stage)];
        meter.inputPeak = std::max(meter.inputPeak, amplitude);
        switch (stage)
        {
            case Stage::lowLevel: input = lowLevel.process(input); break;
            case Stage::multiband:
            {
                const auto detector = input;
                for (auto& band : bands)
                    input = band.process(detector, input);
                break;
            }
            case Stage::compressor: input = compressor.process(input); break;
            case Stage::limiter: input = limiter.process(input); break;
            case Stage::geq: input = geq.process(input); break;
        }
        amplitude = peak(input);
        meter.outputPeak = std::max(meter.outputPeak, amplitude);
        if (tapped != nullptr && --tap == 0)
            *tapped = input;
    }
    return input;
}

Metering Rack::Chain::meters() const
{
    Metering result { stageMeters, {} };
    result.stages[index(Stage::lowLevel)].gainDb = lowLevel.meter();
    result.stages[index(Stage::compressor)].gainDb = compressor.meter();
    result.stages[index(Stage::limiter)].gainDb = limiter.meter();
    auto& strongest = result.stages[index(Stage::multiband)].gainDb;
    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        result.bands[i] = bands[i].meter();
        if (std::abs(result.bands[i]) > std::abs(strongest))
            strongest = result.bands[i];
    }
    return result;
}

void Rack::prepare(double rate)
{
    const auto sampleRate = static_cast<float>(rate);
    delaySamples = std::clamp(static_cast<int>(std::lround(rate * lookaheadMs / 1000.0)), 1, maxDelay);
    fadeLength = std::max(1, static_cast<int>(std::lround(rate * 0.024)));
    slew = timeCoefficient(sampleRate, 8.0f);
    dryDelay.prepare(delaySamples);
    for (auto& chain : chains)
        chain.prepare(sampleRate, delaySamples);
    active = activeOrder = pendingOrder = warmup = fadeLeft = 0;
    configured = false;
}

void Rack::configure(const Settings& settings)
{
    const auto order = std::clamp(settings.order, 0, orderCount - 1);
    const auto orderIndex = static_cast<std::size_t>(order);
    auto& current = chains[static_cast<std::size_t>(active)];
    auto& pending = chains[static_cast<std::size_t>(1 - active)];
    dryTarget = settings.bypass ? 1.0f : 0.0f;

    if (!configured)
    {
        current.order = orders[orderIndex];
        activeOrder = order;
        dryMix = dryTarget;
        configured = true;
    }
    current.configure(settings);

    if (warmup == 0 && fadeLeft == 0 && order != activeOrder)
    {
        // The fixed-size DSP state can be copied without allocating on the audio thread.
        pending = current;
        pending.order = orders[orderIndex];
        pendingOrder = order;
        warmup = delaySamples;
        fadeLeft = fadeLength;
    }
    if (fadeLeft > 0)
        pending.configure(settings);
}

Frame Rack::process(Frame input, int tap, Frame* tapped)
{
    const auto dry = dryDelay.push(input);
    auto wet = chains[static_cast<std::size_t>(active)].process(input, tap, tapped);
    if (fadeLeft > 0)
    {
        const auto upcoming = chains[static_cast<std::size_t>(1 - active)].process(input);
        if (warmup > 0)
        {
            --warmup;
        }
        else
        {
            --fadeLeft;
            const auto mix = 1.0f - static_cast<float>(fadeLeft) / static_cast<float>(fadeLength);
            wet = blend(wet, upcoming, mix);
            if (fadeLeft == 0)
            {
                active = 1 - active;
                activeOrder = pendingOrder;
            }
        }
    }
    dryMix = smoothMix(dryMix, dryTarget, slew);
    return blend(wet, dry, dryMix);
}

Metering Rack::meters() const
{
    return chains[static_cast<std::size_t>(active)].meters();
}

Metering Rack::takeMeters()
{
    const auto result = meters();
    for (auto& chain : chains)
        chain.stageMeters = {};
    return result;
}

} // namespace shortbus::dsp
