// SPDX-License-Identifier: AGPL-3.0-only
#include "dsp/Rack.h"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace shortbus::dsp;

namespace
{
constexpr double twoPi = 6.2831853071795864769;

void check(bool condition, const char* name)
{
    if (!condition)
    {
        std::cerr << "Failed: " << name << '\n';
        std::exit(1);
    }
}

void transientRamp()
{
    for (const auto rate : { 8000, 44100, 48000, 96000, 192000 })
    {
        const auto delay = static_cast<int>(std::lround(rate * 0.003));
        Limiter limiter;
        limiter.prepare(static_cast<float>(rate), delay);
        limiter.configure({ true, -6.0f, 0.0f, 100.0f, false });
        const auto pulse = rate / 10;
        auto previous = 1.0f;
        for (int i = 0; i <= pulse + delay; ++i)
        {
            const auto output = limiter.process({ 0.01f, i == pulse ? 1.0f : 0.0f });
            const auto gain = output[0] / 0.01f;
            if (i == pulse)
                check(gain > 1.0f - 2.1f / static_cast<float>(delay * delay),
                      "attack begins gently when an isolated peak enters lookahead");
            if (i >= pulse && i <= pulse + delay)
                check(std::abs(gain - previous) < 1.1f / static_cast<float>(delay),
                      "gain ramp avoids an abrupt attack step");
            if (i == pulse + delay)
                check(std::abs(output[1] - gainFromDb(-6.0f)) < 2.0e-6f,
                      "the complete gain ramp reaches the ceiling at the delayed peak");
            previous = gain;
        }
    }
}

float recovery(bool adaptive, float burstSeconds)
{
    constexpr int rate = 48000, delay = 144;
    Limiter limiter;
    limiter.prepare(rate, delay);
    limiter.configure({ true, -6.0f, 0.0f, 50.0f, adaptive });
    const auto end = rate / 10 + static_cast<int>(rate * burstSeconds);
    Frame output {};
    for (int i = 0; i < end + delay * 2 + rate * 15 / 100; ++i)
        output = limiter.process({ 0.01f, i >= rate / 10 && i < end ? 1.0f : 0.0f });
    return 1.0f - output[0] / 0.01f;
}

void releaseTiming()
{
    const auto fixedShort = recovery(false, 0.005f);
    const auto fixedLong = recovery(false, 0.8f);
    check(std::abs(fixedLong - fixedShort) < 1.0e-5f && fixedLong > 0.0003f && fixedLong < 0.0006f,
          "fixed release reaches 60 dB recovery near three times the indicated 50 ms");
    const auto autoShort = recovery(true, 0.005f);
    const auto autoLong = recovery(true, 0.8f);
    check(autoLong > autoShort * 1.7f && autoShort > fixedShort,
          "adaptive recovery holds sustained limiting longer than isolated transients");
}

double toneDistortion(int frequency, bool adaptive)
{
    constexpr int rate = 48000;
    Limiter limiter;
    limiter.prepare(rate, 144);
    limiter.configure({ true, -8.0f, 0.0f, 100.0f, adaptive });
    double power = 0.0, sine = 0.0, cosine = 0.0;
    for (int i = 0; i < rate * 3; ++i)
    {
        const auto phase = twoPi * frequency * i / rate;
        const auto input = static_cast<float>(0.8 * std::sin(phase));
        const auto output = limiter.process({ input, -0.5f * input });
        check(std::abs(output[1] + 0.5f * output[0]) < 1.0e-6f,
              "stereo linking preserves polarity and balance through limiting");
        if (i >= rate * 2)
        {
            power += output[0] * output[0];
            sine += output[0] * std::sin(phase);
            cosine += output[0] * std::cos(phase);
        }
    }
    const auto fundamental = 2.0 * (sine * sine + cosine * cosine) / rate;
    return std::sqrt(std::max(0.0, power - fundamental) / fundamental);
}

void adaptiveBass()
{
    for (const auto frequency : { 30, 50, 100 })
    {
        const auto fixed = toneDistortion(frequency, false);
        const auto adaptive = toneDistortion(frequency, true);
        check(adaptive < fixed * 0.65 && adaptive < 0.012,
              "adaptive release reduces bass-cycle gain modulation");
    }
}

void automationAndReset()
{
    std::mt19937 random(120);
    std::uniform_real_distribution<float> sample(-2.0f, 2.0f);
    for (const auto rate : { 8000, 44100, 48000, 96000, 192000 })
    {
        const auto delay = static_cast<int>(std::lround(rate * 0.003));
        Limiter limiter;
        limiter.prepare(static_cast<float>(rate), delay);
        LimiterSettings settings { true, -1.0f, 0.0f, 20.0f, false };
        for (int i = 0; i < rate / 2; ++i)
        {
            if (i % 37 == 0)
            {
                settings.ceiling = i % 74 == 0 ? -18.0f : -1.0f;
                settings.drive = i % 111 == 0 ? 18.0f : -12.0f;
                settings.release = i % 148 == 0 ? 20.0f : 1000.0f;
                settings.adaptive = !settings.adaptive;
                limiter.configure(settings);
            }
            const auto output = limiter.process({ sample(random), sample(random) });
            check(std::isfinite(peak(output)) && peak(output) <= gainFromDb(settings.ceiling) + 2.0e-6f,
                  "automated controls respect the sample ceiling");
        }
        limiter.prepare(static_cast<float>(rate), delay);
        limiter.configure({ true, -1.0f, 0.0f, 100.0f });
        Frame output {};
        for (int i = 0; i < rate; ++i)
            output = limiter.process({ 0.1f, -0.05f });
        check(output == Frame { 0.1f, -0.05f }, "prepare clears the release and smoothing histories");
    }
}
}

int main()
{
    transientRamp();
    releaseTiming();
    adaptiveBass();
    automationAndReset();
    std::cout << "Limiter ramps, fixed and adaptive recovery, bass distortion, and automation passed\n";
}
