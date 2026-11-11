# Compressor modes

Short Bus offers Clean, Opto and FET behavioral models. The Opto and FET modes use original implementations of published compression principles and separately adjustable timing. The models have been checked mathematically and with synthetic signals. Hardware and listening comparisons remain open validation work.

## Controls and recall

Threshold, ratio, knee, range and makeup apply in all three modes. Each mode retains its own attack and release values. Clean uses the existing parameter IDs and calculations. A mode change blends between continuously running models with an 8 ms smoothing time constant. The plugin reports the same fixed lookahead latency in every mode.

| Mode | Detector and gain control | Attack range | Release control |
| --- | --- | --- | --- |
| Clean | Original peak detector and feedforward dB gain curve | 0.5–250 ms | Original 10–2500 ms recovery |
| Opto | Feedback RMS detector and two optical recovery states | 1–100 ms, default 10 ms | Fast component half-release of 20–500 ms, default 60 ms, plus a memory tail |
| FET | Feedback peak detector and normalized variable-resistance divider | 0.02–0.8 ms, default 0.2 ms | 50–1100 ms, default 150 ms, lengthening with sustained compression |

The mode and timing values save in unit presets, Bus presets and host sessions. Older presets and sessions select Clean. A Compressor recall safe protects the mode and all its timing values.

## Published basis

Giannoulis, Massberg and Reiss describe soft-knee gain computation, exponential smoothing, RMS detection and feedback control. This implementation uses the prior gain applied to the current input as its feedback estimate. The source is their 2012 tutorial, especially sections 2.2, 2.3 and 3.1. [Digital Dynamic Range Compressor Design—A Tutorial and Analysis](https://joshreiss.github.io/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf)

Universal Audio describes optical recovery as dependent on prior compression, with a fast component followed by a longer tail. Its published typical attack and early recovery informed the Opto defaults. The following reservoir weights and memory constants are Short Bus design choices. [LA-2A history and operating behavior](https://help.uaudio.com/hc/en-us/articles/215779663-A-History-of-the-Teletronix-LA-2A-Leveling-Amplifier)

Universal Audio documents the FET attenuator as a voltage-controlled resistance in a feedback circuit. Its published attack and release ranges informed the FET controls. The normalized conductance, detector decay and exposure constants below are Short Bus design choices. [2-1176 technical manual](https://media.uaudio.com/assetlibrary/2/-/2-1176_manual.pdf) and [FET timing and recovery](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual)

## Implemented equations

The common one-pole update uses a time constant in seconds and sample rate in Hz.

$$
\alpha = 1-e^{-1/(f_s\tau)}, \qquad z_n=z_{n-1}+\alpha(u_n-z_{n-1})
$$

The shared feedback gain computer preserves the requested steady-state compression curve. Let $T$ be threshold, $R$ ratio, $K$ knee width, and $y$ the feedback level in dB. Outside the knee, requested positive attenuation is $(R-1)(y-T)$, bounded between zero and Range. Inside the knee, Short Bus algebraically inverts the input/output curve.

$$
s=1-1/R,\quad d=y-T+K/2,\quad
t=\frac{2d}{1+\sqrt{1-2sd/K}},\quad
u=\frac{st^2}{2K}
$$

The local feedback slope reduces the control update near steep ratios, which keeps short FET attacks stable at low sample rates.

### Opto

The detector smooths each channel's squared feedback signal over 5 ms and uses the larger channel power. Two positive attenuation states contribute equally to the output. Both charge using Attack. The fast state decays with the selected half-release time. The slow state's time constant varies from 8 to 40 times that control, according to squared exposure memory.

Exposure follows normalized gain reduction with 400 ms charge and 1500 ms recovery constants. Its square interpolates the slow recovery coefficient between the two endpoints. At the defaults, the effective slow time constant spans approximately 0.48–2.4 seconds. Longer or deeper compression increases the tail.

### FET

The gain element uses a normalized shunt conductance.

$$
G=\frac{1}{1+v},\qquad v_\text{target}=10^{u/20}-1
$$

A full-wave feedback peak detector has a 1 ms decay. The conductance follows the selected attack or release coefficient, with a local loop-slope correction. Exposure memory charges over 150 ms and recovers over 700 ms. It interpolates the recovery coefficient between the selected release constant and one 2.5 times longer.

Both character models sense before makeup, apply a common gain to both channels, and slew back to unity when the stage is switched off.
