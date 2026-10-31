# DSP Playbook — how the principles become sound

The brief says *what* the plug-in must feel like. This file says *how*, and explains the mechanism behind each technique so decisions can be reasoned about rather than guessed. The code is JUCE-flavoured C++ sketches: check API details against current JUCE docs before using them.

---

## 1. No dead zones: parameter mapping

**Mechanism:** perception is roughly logarithmic in frequency, time and level. A linear knob spends most of its travel where the ear hears little change, which creates dead zones. When the curve matches perception, every millimetre of travel is audible.

| Quantity | Mapping (knob `x` in 0..1) |
|---|---|
| Frequency | `f = fMin * pow(fMax / fMin, x)` |
| Time | `t = tMin * pow(tMax / tMin, x)` |
| Level | linear in dB, then `juce::Decibels::decibelsToGain(dB)` |
| Mix | equal-power: `wet = sin(x·π/2)`, `dry = cos(x·π/2)` |

**Ranges are musical, not technical.** Clamp every hidden parameter to the range where it sounds good, never to what the algorithm technically allows. For example, a Resonator node shouldn't tune below about 40 Hz or above about 4 kHz.

### Macro knobs as curated paths

One knob drives many hidden parameters, and each hidden parameter has its own breakpoint curve:

```cpp
struct Breakpoint { float x, y; };

// Piecewise curve with smoothstep easing between points: no corners, so no audible "kinks".
float evalPath (const std::array<Breakpoint, N>& p, float x)
{
    for (size_t i = 1; i < N; ++i)
        if (x <= p[i].x)
        {
            float t = (x - p[i-1].x) / (p[i].x - p[i-1].x);
            t = t * t * (3.f - 2.f * t);                    // smoothstep
            return p[i-1].y + t * (p[i].y - p[i-1].y);
        }
    return p[N-1].y;
}
```

**Design each macro as regimes, not a single "amount".** The Ecology regimes are:
- **0–30%**: colour (short, low-feedback, mostly parallel nodes)
- **30–60%**: space (diffusion rises, the nodes blend into a room)
- **60–80%**: ringing (loop gain approaches 1, so resonances bloom)
- **80–100%**: life (loop gain passes 1, and saturation holds the sound as a self-sustaining texture)

The boundaries between regimes are where discovery happens. Place them at different points for different hidden parameters, so the transitions overlap and never feel like steps.

---

## 2. Smoothing: never a click, never a zipper

**Mechanism:** a parameter that jumps from one block to the next creates a discontinuity, which the ear hears as a click. Small steps repeated every block are heard as "zipper" noise.

- Every parameter that affects audio goes through `juce::SmoothedValue`.
  - Gains: linear smoothing over 20–50 ms.
  - Frequencies: multiplicative smoothing (`ValueSmoothingTypes::Multiplicative`) over 30–80 ms.
- **Delay time is special.** Smoothly changing a delay's read position shifts its pitch (Doppler). This is the tape feel, and it's a feature: use a 150–600 ms glide on purpose. For an instant jump without the pitch glide, crossfade between two read heads over about 30 ms.
- Read delays with cubic (Hermite) interpolation. Linear interpolation dulls the highs and makes modulated delays sound grainy and cheap.

---

## 3. The network: bounded feedback

**Mechanism:** in a feedback loop, energy grows when loop gain is above 1 and decays when it is below 1. Real-world energy is bounded by the physical medium. We reproduce that with a saturator inside every loop, so the network can sit above unity gain without blowing up. It sustains at the level where the saturator's average gain falls back to exactly 1. That is a self-oscillating drone that still reacts to the input.

```cpp
// Per node, per sample:
float y = delay.readHermite (readPos);           // modulated delay read
y = filter.process (y);                          // SVF: tone and damping
y = std::tanh (y * drive) / drive;               // soft clip: small-signal gain 1, bounded output
y = dcBlock.process (y);                         // y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
```

Loop gain is often above 1 with asymmetric saturation, and that slowly builds up DC. **The DC blocker inside the loop is mandatory.**

### Coupling matrix

The four node outputs are mixed back into the four node inputs:

```
M(c) = (1 - c) · I  +  c · H
```

- `I` is the identity matrix: each node feeds only itself (separate echoes).
- `H` is the 4×4 Hadamard matrix scaled by 0.5. It's orthogonal, so it mixes energy fully without changing the total.
- `c` comes from Ecology.

At `c = 0` the nodes stay apart. At `c = 1` every node hears every other one, which gives fast diffusion: the "room" regime. Loop gain is set separately (`g` per node, via an Ecology path). Keep the coupling and gain paths separate, so space and sustain can be tuned independently.

**Rule of thumb:** cross loop gain 1.0 at about 70–75% of Ecology's travel, and let the saturators hold everything above that.

---

## 4. It lives: autonomous motion

**Mechanism:** if every modulator ran at a related rate, the motion would repeat and the ear would detect the loop within seconds. Rates set at irrational ratios to each other never line up, so the pattern never repeats.

- Use smoothed random walks, not LFOs: a random step every `T` seconds, passed through a one-pole lowpass.
- Node `k` gets a rate of `baseRate * pow(1.6180339f, k)` (golden-ratio spacing).
- Motion scales depth (and, more gently, rate) of the random walks driving delay times, filter cutoffs and grain positions.
- Seed the RNG explicitly. **Reroll** changes the seed. Store the seed in the plug-in state, so a saved session sounds the same when it's reopened.

---

## 5. Memory and tape slowdown

**Mechanism:** a real-time plug-in can't play faster than the present, but it *can* fall behind it. A buffer that is always recording turns "slow down" into "read the past at less than 1× speed". The lag between write and read grows as long as the slowdown lasts, so the slowdown is limited only by the buffer's length.

- In `prepareToPlay`, preallocate a stereo circular buffer of about 30 s × sample rate. Never resize it on the audio thread.
- **Grains:**
  - Hann-windowed, 20–400 ms long
  - read position = now − (Memory × 30 s) ± jitter
  - density and length follow Memory and Motion paths
- **Tape slowdown:** read speed ramps below 1 with an exponential curve, the way a motor spins down. When the lag nears the buffer's length, crossfade back to the present over 0.5–2 s, the way a tape catches up. Pitch and time move together automatically, because the speed is literally changing.

---

## 6. Safe at the edges: the output chain

Order: network → wet auto-gain → Mix → Output trim → safety limiter → NaN guard.

- **Auto-gain.** Measure input and wet RMS with slow (~1 s) envelopes, and compensate the wet signal toward the input level (clamped to ±12 dB). **Mechanism:** without it, louder settings simply sound "better", which ruins honest exploration. It is also the difference between the plug-in feeling cheap and feeling premium.
- **Limiter.** A true-peak-safe soft limiter with a ceiling around −0.3 dBFS. The user should never hear it work in normal use, because it is a seatbelt, not an effect.
- **NaN/Inf guard.** If any sample is non-finite, zero the output for that block and reset the network's state. Never pass a NaN to the host.

---

## 7. Real-time rules for the audio thread

These are absolute: break one and the plug-in will eventually glitch in someone's session.

- No memory allocation: no `new`, `std::vector::push_back`, string building or `std::function` capture.
- No locks or mutexes, no file or network I/O, no logging. Use lock-free FIFOs to send data to the UI.
- Put `juce::ScopedNoDenormals` at the top of `processBlock`. **Mechanism:** decaying feedback tails produce denormal floats, and denormal arithmetic is very slow on some CPUs, which causes CPU spikes exactly at the quiet end of a tail.
- Read parameters through `AudioProcessorValueTreeState::getRawParameterValue` (atomics), once per block, then pass them to the smoothers.
- Support any block size and sample rate (44.1–192 kHz). Derive every time constant from the sample rate.
- Report latency through `setLatencySamples` if any look-ahead or STFT is added.

---

## 8. Host sync

- Read tempo with `getPlayHead()->getPosition()` → `getBpm()`. Handle a missing play head or missing BPM by falling back to free mode.
- In Sync mode, quantize the Echo and Memory delay times to musical divisions: 1/16, 1/8, dotted 1/8, triplets and 1/4. Let Size choose the division, and keep the Resonator node free so it can stay pitched.
- Optional (Phase 4): a key/scale selector that snaps Resonator tunings to scale degrees. Hosts don't report key, so the user sets it.

---

## 9. Discovery audit

`discovery_audit.py` loads the built VST3 with Spotify's `pedalboard` library, sweeps every parameter across its range on several test signals, renders 100 random full states, and flags:

| Flag | Meaning | Fix by |
|---|---|---|
| BLOWUP | a non-finite sample, or a peak above 0 dBFS | the output chain, saturation |
| SILENT | output below −60 dBFS on a non-silent input | ranges, gain paths |
| LEVEL_JUMP | more than 6 dB of change between neighbouring knob steps | auto-gain, smoothing of paths |
| DEAD_ZONE | a stretch of ≥25% of travel whose two ends still sound alike | the mapping curve, regime layout |
| NO_EFFECT | the whole sweep barely differs from the default | wiring, or the knob's purpose |
| INSERT_DULL | the default state barely differs from dry | default preset |

Run it before every hand-over. **Zero BLOWUP and SILENT flags is a hard requirement.** DEAD_ZONE flags need either a fix or a written reason in `DECISIONS.md`.

**How it measures "sounds alike":** it compares whole spectrograms (1/3-octave bands × ~43 ms frames), averaged only over cells where there is sound. An average-spectrum metric would be blind to delays, echoes and grains, which change *when* energy happens more than its colour. Including silent cells would dilute every difference. Dead zones are judged end-to-end, not step-to-step, so a smooth, gradual knob isn't mistaken for a dead one.

**The audit is necessary, not sufficient.** It proves that every knob position changes the sound. Only Hakan's ears can say whether those changes are *interesting*.

---

## 10. Quality checklist ("doesn't feel cheap")

- [ ] It makes an interesting sound immediately on insert, at default settings.
- [ ] No knob, at any speed of turning, produces a click or zipper.
- [ ] No level jumps when sweeping any knob (auto-gain is working).
- [ ] Bypass on and off is click-free (crossfade).
- [ ] Tails ring out naturally after transport stop, with no abrupt cut.
- [ ] CPU stays steady with no spikes on quiet tails (denormals handled).
- [ ] The state saves and recalls exactly, including the seed, Freeze and Sync.
- [ ] pluginval passes at strictness 10.
- [ ] UI: animation is smooth, every knob shows its value on hover, double-click resets a knob to default, and the visuals react to audio.
- [ ] Preset names evoke places and feelings, not specs.

---

## 11. Mono-safe space (learned building dendrite 1.0)

- **Width lives only in the Side.** Build it from the Mid (pan k·sinθ·M, a 90° allpass pair for spread) and write it into the Side; the Mid must never read the Side. Then the mono sum is the dry plus a mono echo, whatever space does.
- **Any gain computed from the Side must not touch the Mid.** Loop and output ceilings run on Mid and Side separately; one linked gain would let space change the mono sum.
- **Coherent vs. decorrelated build-up.** Undiffused, the Side recirculates in phase and grows like the Mid. Diffused, each pass adds in power, not amplitude, so on steady tones the Side drops several dB. Raise the Side output where diffusion fades in, or width dips as the knob goes up.
- **Never switch an allpass chain in.** With a small coefficient a Schroeder allpass is a pure delay, so switching it on jumps the Side in time. Run it always and crossfade.
- **Ceilings follow what was played.** Feedback and wet output are capped relative to the recent input peak (not a fixed level), so endless and throws stay as big as the playing and never lean on the safety limiter.
- **Check:** the audit's MONO_BREAK compares the echo's mono sum at space 0 and at the default after a gain match; keep it under −30 dB (1.0 measures −139 to −141 dB).

