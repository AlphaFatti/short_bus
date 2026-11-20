# Limiter response

Version 0.1.12 uses a finite lookahead gain ramp and adaptive recovery. The target is smoother peak control for a mix bus. Listening against W1 on the same material remains part of evaluating the sound.

## Controls

**Drive** sets the input gain and **Ceiling** sets the maximum sample level at the limiter output. **Release** sets the base recovery speed. **Auto release** lengthens recovery during sustained gain reduction. It is on by default. Turning it off selects fixed recovery.

The fixed release uses the timing convention described by George Yohng for W1. The attenuation envelope decays to one thousandth of its initial value in three times the indicated release time. A 50 ms setting therefore corresponds to approximately 150 ms at RT60. The coefficient for the unsmoothed envelope is `1 − exp(−ln(1000) / (sampleRate × 3 × releaseSeconds))`. [W1 description](https://www.yohng.com/software/w1limit.html)

The adaptive detector and its tuning are original Short Bus choices. It follows the attenuation envelope with a 50 ms charge and 250 ms recovery memory. Sustained attenuation blends the base recovery coefficient toward a slower time constant of `max(250 ms, 4 × Release)`. Isolated peaks recover faster than prolonged limiting.

## Attack smoothing

For each incoming sample, a moving peak detector finds the required attenuation across the lookahead window. A release envelope follows it, then two moving averages smooth that envelope. Their combined support spans the same number of samples as the audio delay. By the time a peak reaches the output, the whole smoothed control window contains its required attenuation. The normal gain ramp reaches the ceiling without a late attack correction.

This uses published peak-hold and finite-smoothing principles. The implementation is independent. A final bound handles abrupt ceiling or drive automation and rounding. [Signalsmith limiter design](https://signalsmith-audio.co.uk/writing/2022/limiter/)

## Recall and validation

Existing parameter IDs and the 3 ms reported latency are retained. Older sessions and presets receive Auto release when loaded by this version, so their enabled limiter sound changes with the new algorithm. The public v0.1.11 release remains available for comparison. Both fixed and automatic modes save in unit presets, Bus presets, and host state.

Checks cover isolated transients, fixed release calibration, duration-dependent recovery, stereo balance, reset, automation, and the sample ceiling from 8 to 192 kHz. In a 48 kHz synthetic comparison at an −8 dBFS ceiling, 100 ms release, and 0.8 peak input, 30 Hz distortion measured 0.85% with Auto release versus 1.32% in v0.1.11. The largest gain step entering an isolated 6 dB peak fell from 0.027 to 0.0068. These measurements compare Short Bus versions. Direct W1 renders and human listening have not been measured here.
