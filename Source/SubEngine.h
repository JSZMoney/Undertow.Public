// Undertow — 3-band subharmonic synthesizer engine
// Framework-independent C++17 so it can be unit-tested without JUCE.
//
// Signal flow (per sample):
//
//   in ─► Gain In ─┬─► Tight Punch (resonant HPF) ─┬─► Low End shelf ───────────────────────────────┐
//                  │                               ├─► [mid or L/R] ─► SubGenerator ─► 3 bands ─► Σ ► Subharmonics ─┤ wet
//                  │                               └─► Edge (cuts ► Squeeze ► Drive) ► Edge level ──────────────────┤
//                  └──────────────────────────────── dry ───────────────────────────────────► Mix ◄─────────────────┘
//                                                                                              ▼
//                                                        M/S: Mono Maker + Stereo Width ─► Gain Out ─► out
//
// See SubGenerator below for how the subharmonic is made.

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace undertow
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kMinDb = -60.0f;

inline float dbToGain (float db) { return db <= kMinDb ? 0.0f : std::pow (10.0f, db / 20.0f); }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void setLowpass (double sr, double f, double q)  { design (sr, f, q, false); }
    void setHighpass (double sr, double f, double q) { design (sr, f, q, true); }

    // RBJ low shelf (slope 1)
    void setLowShelf (double sr, double f, double gainDb)
    {
        f = std::clamp (f, 5.0, sr * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * kPi * f / sr;
        const double c = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0), sA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) + (A - 1) * c + sA;
        b0 = A * ((A + 1) - (A - 1) * c + sA) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * c) / a0;
        b2 = A * ((A + 1) - (A - 1) * c - sA) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * c) / a0;
        a2 = ((A + 1) + (A - 1) * c - sA) / a0;
    }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double sr, double f, double q, bool highpass)
    {
        f = std::clamp (f, 5.0, sr * 0.45);
        const double w0 = 2.0 * kPi * f / sr;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        if (highpass) { b0 = (1.0 + c) / 2.0; b1 = -(1.0 + c); b2 = b0; }
        else          { b0 = (1.0 - c) / 2.0; b1 = 1.0 - c;    b2 = b0; }
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 = (-2.0 * c) / a0;
        a2 = (1.0 - alpha) / a0;
    }
};

// Topology-preserving state-variable high-pass (Simper/Cytomic). Unlike a biquad it stays
// clean while its frequency is being swept, so it's used for the Tight Punch control.
struct SvfHighpass
{
    double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1;
    void reset() { ic1 = ic2 = 0; }
    void set (double sr, double f, double q)
    {
        f = std::clamp (f, 5.0, sr * 0.45);
        const double g = std::tan (kPi * f / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline double process (double x)
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return x - k * v1 - v2;
    }
};

// Butterworth section Qs
constexpr double kBw4[2] = { 0.54119610, 1.30656296 };
constexpr double kBw6[3] = { 0.51763809, 0.70710678, 1.93185165 };

//==============================================================================
struct Smoothed
{
    double cur = 0, target = 0, coef = 0.001;
    void prepare (double sr, double ms, double initial) { coef = 1.0 - std::exp (-1.0 / (sr * ms * 0.001)); cur = target = initial; }
    inline double next() { cur += coef * (target - cur); return cur; }
};

//==============================================================================
struct BandDef { double lo, hi; };

// Input regions and the output bands their subs land in. These follow the dbx 120XP
// (notes 48–72 Hz → sub 24–36 Hz, notes 72–112 Hz → sub 36–56 Hz) plus bx_subsynth's
// extra upper band (notes 112–160 Hz → sub 56–80 Hz).
constexpr std::array<BandDef, 3> kBands { { { 48.0, 72.0 }, { 72.0, 112.0 }, { 112.0, 160.0 } } };

// Linkwitz-Riley 4th-order low/high-pass (two cascaded Butterworth 2nd-order sections)
struct LR4
{
    Biquad a, b;
    void setLowpass (double sr, double f)  { a.setLowpass (sr, f, 0.70710678);  b.setLowpass (sr, f, 0.70710678); }
    void setHighpass (double sr, double f) { a.setHighpass (sr, f, 0.70710678); b.setHighpass (sr, f, 0.70710678); }
    void reset() { a.reset(); b.reset(); }
    inline double process (double x) { return b.process (a.process (x)); }
};

// One band's octave divider. Like the dbx, each band listens only to its own slice of the
// spectrum and makes its own sub, so every band is a steady layer you can solo, and a loud
// vocal in one region can't drive or swell the sub of another.
//  - Pitch: zero crossings of the band signal, with hysteresis. Crossings closer together than
//    the band's highest note mean the sound is above the band, so the voice goes quiet rather
//    than locking onto every other cycle.
//  - Sound: a phase-locked sine steered so each of its half-cycles lasts exactly one input
//    cycle: a clean, continuous sine at exactly half the note's frequency.
//  - Level: follows that band's own envelope.
struct Voice
{
    Biquad post[2], rumble;
    LR4 detHp, detLp;   // extra-steep edges for pitch detection only (not in the sound path)
    double detEnv = 0;
    double env = 0, prev = 0, phase = 0, inc = 0, period = 400.0, pol = 1.0;
    double ampSmooth = 0, gate = 0, harm = 1.0, pitchGain = 1.0;
    double corr = 0, pow = 0;   // running correlation with the voice below (for polarity alignment)
    bool armed = false, locked = false, polFlipPending = false;
    int since = 0, sinceCross = 0, minP = 1, maxP = 1, flipHold = 0, lastSince = 0, sinceGood = 1 << 30, holdGood = 7200;

    void prepare (double sr, const BandDef& b, bool withPost)
    {
        minP = std::max (1, (int) (sr / (b.hi * 1.05)));
        maxP = std::max (minP + 1, (int) (sr / (b.lo * 0.8)));
        if (withPost)
            for (int i = 0; i < 2; ++i) post[i].setLowpass (sr, b.hi * 0.5 * 1.5, kBw4[i]);
        rumble.setHighpass (sr, 18.0, 0.70710678);
        holdGood = (int) (sr * 0.150);
        detHp.setHighpass (sr, b.lo);
        detLp.setLowpass (sr, b.hi);
        harmAtk = 1.0 - std::exp (-1.0 / (sr * 0.060));
        harmRel = 1.0 - std::exp (-1.0 / (sr * 0.400));
        reset();
    }

    void reset()
    {
        for (auto& f : post) f.reset();
        rumble.reset();
        detHp.reset(); detLp.reset();
        detEnv = 0;
        env = prev = phase = ampSmooth = gate = corr = pow = 0.0;
        harm = pitchGain = 1.0; pol = 1.0;
        armed = locked = polFlipPending = false;
        since = sinceCross = flipHold = lastSince = 0; sinceGood = 1 << 30; period = 0.5 * (minP + maxP); inc = kPi / period;
    }

    // Pitch detection on the band signal. Called every sample.
    inline void detect (double regionSignal, double envAtk, double envRel)
    {
        const double a = std::abs (regionSignal);          // level: the band as you hear it
        env += (a > env ? envAtk : envRel) * (a - env);
        const double b = detLp.process (detHp.process (regionSignal));   // pitch: steeper view
        detEnv += (std::abs (b) > detEnv ? envAtk : envRel) * (std::abs (b) - detEnv);
        const double thr = detEnv * 0.2 + 1e-7;
        ++since; ++sinceCross;
        if (b < -thr) armed = true;
        if (armed && prev <= 0.0 && b > 0.0)
        {
            armed = false;
            const bool tooHigh = sinceCross < minP;
            sinceCross = 0;
            if (tooHigh)
            {
                lastSince = 0;
                since = 0;
            }
            else if (since >= minP)
            {
                // Pitch only follows steady readings (two cycles in a row within 12%); a jumpy
                // reading from a muddle of overtones is ignored and the voice keeps its last good
                // pitch. Loudness keeps following the band, so nothing drops out.
                const bool inRange = since <= maxP;   // longer = a lower note leaking in: ignore
                const bool steady = inRange && lastSince > 0 && std::abs ((double) since / lastSince - 1.0) < 0.12;
                lastSince = inRange ? since : 0;
                if (steady)
                {
                    period += 0.5 * ((double) since - period);
                    sinceGood = 0;
                    // Steer the oscillator to reach its next zero crossing (a multiple of pi)
                    // exactly one input period from now.
                    double target = (std::floor (phase / kPi + 0.5) + 1.0) * kPi;
                    if (target - phase < 0.5 * kPi) target += kPi;
                    inc = (target - phase) / period;
                    if (phase > 512.0 * kPi) phase -= 512.0 * kPi;
                }
                since = 0;
            }
        }
        // Speaking = had a steady pitch within the last 150 ms.
        locked = ++sinceGood < holdGood;
        if (since > 2 * maxP) lastSince = 0;   // note ended or noise
        prev = b;
    }

    // Generates this voice's sub (before band level). Called every sample after detect().
    double harmAtk = 0.01, harmRel = 0.001;
    inline double render (double ampCoef, double gateAtk, double gateRel, double harmTarget, double sr)
    {
        const double before = phase;
        phase += inc;
        // Polarity changes happen exactly at the sub's zero crossings, so they can't click.
        if (polFlipPending && std::floor (before / kPi) != std::floor (phase / kPi))
        {
            pol = -pol;
            polFlipPending = false;
        }

        ampSmooth += ampCoef * (env - ampSmooth);
        const double gateTarget = (locked && env > 2e-4) ? 1.0 : 0.0;
        gate += (gateTarget > gate ? gateAtk : gateRel) * (gateTarget - gate);
        // Ease down when the band is only hearing overtones, come back slowly, so a wobbly
        // reading can't make it flicker.
        harm += (harmTarget > harm ? harmRel : harmAtk) * (harmTarget - harm);

        // A sub under ~22 Hz is inaudible and only eats headroom: fade it out.
        const double pitchTarget = std::clamp ((sr / period - 40.0) / 8.0, 0.0, 1.0);
        pitchGain += (pitchTarget > pitchGain ? gateAtk : gateRel) * (pitchTarget - pitchGain);

        double s = pol * std::sin (phase) * ampSmooth * gate * harm * pitchGain;
        for (auto& f : post) s = f.process (s);
        return rumble.process (s);
    }
};

// The subharmonic generator (one per channel, or one for the mid signal): three independent
// band voices, plus two safeguards for real-world material:
//  - Overtone control: if a band is only hearing the 2nd–4th harmonic of a lower note, its
//    "sub" lands on that note itself (restoring the fundamental, as Epicenter does), so that
//    band is turned down 12 dB rather than adding a strong tone at the bass's own pitch.
//    A detector on 25–48 Hz does the same for very low notes.
//  - Polarity alignment: a note right at a band edge is heard by two voices; if their subs
//    come out in opposite polarity they'd cancel, so the upper one flips (at a zero crossing).
struct SubGenerator
{
    LR4 inHp, inLp, r1Lp, r1Hp, r2Lp, r2Hp, ap2Lp, ap2Hp, deepHp, deepLp;
    std::array<Voice, 3> voices;
    Voice deep;   // detection only

    double envAtk = 0, envRel = 0, gateAtk = 0, gateRel = 0, ampCoef = 0, corrCoef = 0, srate = 48000.0;
    int holdSamples = 2400;

    static constexpr double kMakeup = 2.0;   // sub level relative to the note it comes from (+6 dB)

    void prepare (double sr)
    {
        srate = sr;
        inHp.setHighpass (sr, kBands[0].lo);  inLp.setLowpass (sr, kBands[2].hi);
        r1Lp.setLowpass (sr, kBands[0].hi);   r1Hp.setHighpass (sr, kBands[0].hi);
        r2Lp.setLowpass (sr, kBands[1].hi);   r2Hp.setHighpass (sr, kBands[1].hi);
        ap2Lp.setLowpass (sr, kBands[1].hi);  ap2Hp.setHighpass (sr, kBands[1].hi);
        deepHp.setHighpass (sr, 25.0);        deepLp.setLowpass (sr, kBands[0].lo);
        for (size_t b = 0; b < 3; ++b) voices[b].prepare (sr, kBands[b], true);
        deep.prepare (sr, { 25.0, 48.0 }, false);

        envAtk   = 1.0 - std::exp (-1.0 / (sr * 0.002));
        envRel   = 1.0 - std::exp (-1.0 / (sr * 0.050));
        gateAtk  = 1.0 - std::exp (-1.0 / (sr * 0.004));
        gateRel  = 1.0 - std::exp (-1.0 / (sr * 0.080));
        ampCoef  = 1.0 - std::exp (-1.0 / (sr * 0.006));
        corrCoef = 1.0 - std::exp (-1.0 / (sr * 0.030));
        holdSamples = (int) (sr * 0.05);
        reset();
    }

    void reset()
    {
        for (auto* l : { &inHp, &inLp, &r1Lp, &r1Hp, &r2Lp, &r2Hp, &ap2Lp, &ap2Hp, &deepHp, &deepLp }) l->reset();
        for (auto& v : voices) v.reset();
        deep.reset();
    }

    static bool isHarmonicOf (const Voice& upper, const Voice& lower)
    {
        if (! lower.locked || ! upper.locked || lower.env < 0.5 * upper.env) return false;
        // Pitch readings on a band full of overtones wobble a few %, so allow ±10%.
        const double ratio = lower.period / upper.period;
        for (double k : { 2.0, 3.0, 4.0 })
            if (std::abs (ratio / k - 1.0) < 0.10) return true;
        return false;
    }

    // Returns the three band subs; regionLevel receives each trimmed input region (for the LEDs).
    inline std::array<double, 3> process (double x, const std::array<double, 3>& trim, std::array<double, 3>& regionLevel)
    {
        // Split 48–160 Hz into the three regions with Linkwitz-Riley crossovers
        const double bandIn = inLp.process (inHp.process (x));
        double r1 = r1Lp.process (bandIn);
        const double rest = r1Hp.process (bandIn);
        r1 = ap2Lp.process (r1) + ap2Hp.process (r1);   // phase-match the 112 Hz split
        regionLevel = { r1 * trim[0], r2Lp.process (rest) * trim[1], r2Hp.process (rest) * trim[2] };

        deep.detect (deepLp.process (deepHp.process (x)), envAtk, envRel);
        for (size_t b = 0; b < 3; ++b) voices[b].detect (regionLevel[b], envAtk, envRel);

        std::array<double, 3> out {};
        for (size_t b = 0; b < 3; ++b)
        {
            bool harmonic = isHarmonicOf (voices[b], deep);
            for (size_t j = 0; j < b; ++j) harmonic = harmonic || isHarmonicOf (voices[b], voices[j]);
            // Overtones of a lower note are turned down (-12 dB), not muted: like Epicenter, some of
            // that harmonic-derived bass reinforces the fundamental, and muting would make it chop.
            out[b] = voices[b].render (ampCoef, gateAtk, gateRel, harmonic ? 0.25 : 1.0, srate);
        }

        // Polarity alignment: only between two neighbours that are both genuinely playing the
        // same note (otherwise near-silent ringing could trigger endless flips).
        voices[0].pow += corrCoef * (out[0] * out[0] - voices[0].pow);
        for (size_t b = 1; b < 3; ++b)
        {
            auto& v = voices[b];
            const auto& lo = voices[b - 1];
            v.corr += corrCoef * (out[b] * out[b - 1] - v.corr);
            v.pow  += corrCoef * (out[b] * out[b] - v.pow);
            if (v.flipHold > 0) --v.flipHold;
            const bool bothPlaying = v.locked && lo.locked && v.gate > 0.5 && lo.gate > 0.5
                                     && lo.pow > 0.05 * v.pow && v.pow > 0.05 * lo.pow
                                     && std::abs (lo.period / v.period - 1.0) < 0.1;
            if (bothPlaying && v.flipHold == 0 && ! v.polFlipPending && v.corr < -0.3 * std::sqrt (v.pow * lo.pow))
            {
                v.polFlipPending = true;
                v.corr = 0.0;
                v.flipHold = holdSamples;
            }
        }

        for (auto& o : out) o *= kMakeup;
        return out;
    }
};

//==============================================================================
struct EdgeChannel
{
    Biquad lowCut[2], highCut[2], dc;
    double compEnv = 0;
    void reset() { for (auto& f : lowCut) f.reset(); for (auto& f : highCut) f.reset(); dc.reset(); compEnv = 0; }
};

//==============================================================================
struct Params
{
    float tightPunchHz = 10.0f;                 // ≤ 12 Hz = bypassed
    std::array<float, 3> trimDb { 0, 0, 0 };    // 0..+18 dB input boost per band
    std::array<float, 3> levelDb { 0, 0, 0 };   // per-band sub level
    std::array<bool, 3>  solo { false, false, false };
    float subMasterDb = 0.0f;
    float lowEndDb = 0.0f;                      // low shelf (100 Hz) on the original signal
    bool  midMode = true;                       // generate subs from the mid (L+R) signal only
    float gainInDb = 0.0f;
    float mix = 1.0f;                           // 0 = untouched input, 1 = fully processed
    float monoMakerHz = 20.0f;                  // ≤ 20 Hz = off; below this the output is made mono
    float width = 1.0f;                         // stereo width 0..2 (1 = unchanged)
    bool  edgeOn = false;
    int   edgeMode = 0;                         // 0 = Smooth, 1 = Harsh
    float squeeze = 0.3f;                       // 0..1
    float drive = 0.3f;                         // 0..1
    float edgeLowCutHz = 30.0f;
    float edgeHighCutHz = 5000.0f;
    float edgeLevelDb = -12.0f;
    float outputDb = 0.0f;
    bool  limit = true;                         // safety peak limiter on the output (ceiling -0.3 dBFS)
};

//==============================================================================
class SubEngine
{
public:
    static constexpr int kMaxCh = 2;

    // Meters (written by audio thread as block peaks, read & cleared by the UI)
    std::array<std::atomic<float>, 3> meterBandIn {}, meterBandOut {};
    std::array<std::atomic<float>, 2> meterIn {}, meterOut {};
    std::atomic<float> meterLimitGR { 0.0f };   // 1 - lowest limiter gain in the block (0 = not limiting)

    // Scope of the generated sub (decimated ring buffer; written by audio thread, drawn by the UI)
    static constexpr int kScopeSize = 512;
    std::array<std::atomic<float>, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c)
        {
            synth[c].prepare (sr);
            tight[c].reset();
            lowEnd[c].reset();
            edge[c].reset();
            edge[c].dc.setHighpass (sr, 12.0, 0.7071);
        }
        for (int b = 0; b < 3; ++b) bandGain[b].prepare (sr, 20, dbToGain (p.levelDb[b]));
        master.prepare (sr, 20, dbToGain (p.subMasterDb));
        dry.prepare (sr, 20, 1.0);
        gainIn.prepare (sr, 20, dbToGain (p.gainInDb));
        mixS.prepare (sr, 20, p.mix);
        widthS.prepare (sr, 20, p.width);
        monoHp.setHighpass (sr, std::max (20.0f, p.monoMakerHz));
        monoHp.reset();
        edgeGain.prepare (sr, 20, 0.0);
        out.prepare (sr, 20, dbToGain (p.outputDb));
        lastTight = lastLowCut = lastHighCut = lastMono = lastLowEnd = -1000.0f;
        tightAmt.prepare (sr, 15, 0.0);
        tightFreq.prepare (sr, 20, 12.5);
        tightFreq2.prepare (sr, 20, 12.5);
        scopeEvery = std::max (1, (int) (sr / 6000.0));   // ~85 ms of signal across the scope
        la = std::clamp ((int) std::lround (sr * 0.002), 1, kMaxLookahead - 1);
        limRel = 1.0 - std::exp (-1.0 / (sr * 0.120));
        for (auto& d : delayLine) d.fill (0.0);
        reqRing.fill (1.0); boxRing.fill (1.0);
        boxSum = (double) la; gState = 1.0;
        laPos = boxPos = dPos = 0;
        compAtk = 1.0 - std::exp (-1.0 / (sr * 0.001));
        compRel = 1.0 - std::exp (-1.0 / (sr * 0.080));
        setParams (p);
        // Start every smoothed control exactly at its current value (no fade-in on playback start)
        for (auto& g : bandGain) g.cur = g.target;
        for (auto* sm : { &master, &dry, &edgeGain, &out, &gainIn, &mixS, &widthS, &tightAmt, &tightFreq }) sm->cur = sm->target;
        tightFreq2.cur = tightFreq2.target = tightFreq.cur;
        for (auto& t : tight) t.set (sr, tightFreq2.cur, 1.4);
        tightTuned = tightFreq2.cur;
    }

    void reset()
    {
        for (int c = 0; c < kMaxCh; ++c)
        {
            synth[c].reset();
            tight[c].reset();
            lowEnd[c].reset();
            edge[c].reset();
        }
        monoHp.reset();
    }

    void setParams (const Params& np)
    {
        p = np;
        soloActive = p.solo[0] || p.solo[1] || p.solo[2];

        for (int b = 0; b < 3; ++b)
        {
            trim[b] = dbToGain (p.trimDb[b]);
            const bool audible = ! soloActive || p.solo[b];
            bandGain[b].target = audible ? dbToGain (p.levelDb[b]) : 0.0;
        }
        master.target   = dbToGain (p.subMasterDb);
        dry.target      = soloActive ? 0.0 : 1.0;
        gainIn.target   = dbToGain (p.gainInDb);
        mixS.target     = soloActive ? 1.0 : p.mix;      // soloing always lets you hear the band
        widthS.target   = p.width;
        edgeGain.target = (p.edgeOn && ! soloActive) ? dbToGain (p.edgeLevelDb) : 0.0;
        out.target      = dbToGain (p.outputDb);

        // Tight Punch glides to its new frequency and fades in/out (see process), so sweeping it can't click
        tightOn = p.tightPunchHz > 12.0f;
        tightAmt.target = tightOn ? 1.0 : 0.0;
        tightFreq.target = std::max (12.5, (double) p.tightPunchHz);
        lowEndOn = std::abs (p.lowEndDb) > 0.05f;
        if (p.lowEndDb != lastLowEnd)
        {
            for (auto& f : lowEnd) f.setLowShelf (sr, 100.0, p.lowEndDb);
            lastLowEnd = p.lowEndDb;
        }
        monoOn = p.monoMakerHz > 20.5f;
        if (p.monoMakerHz != lastMono)
        {
            monoHp.setHighpass (sr, std::max (20.0f, p.monoMakerHz));
            lastMono = p.monoMakerHz;
        }
        if (p.edgeLowCutHz != lastLowCut || p.edgeHighCutHz != lastHighCut)
        {
            for (auto& e : edge)
                for (int i = 0; i < 2; ++i)
                {
                    e.lowCut[i].setHighpass (sr, p.edgeLowCutHz, kBw4[i]);
                    e.highCut[i].setLowpass (sr, p.edgeHighCutHz, kBw4[i]);
                }
            lastLowCut = p.edgeLowCutHz; lastHighCut = p.edgeHighCutHz;
        }

        // Edge dynamics/saturation constants
        compThreshDb = -6.0 - p.squeeze * 30.0;
        compRatio    = 1.5 + p.squeeze * 8.5;
        compMakeup   = std::pow (10.0, (p.squeeze * 14.0) / 20.0);
        drivePre     = std::pow (10.0, (p.drive * 30.0) / 20.0);
        drivePost    = 1.0 / std::sqrt (drivePre);
    }

    void process (float* const* channels, int numCh, int numSamples)
    {
        numCh = std::min (numCh, kMaxCh);
        if (numCh <= 0) return;

        std::array<double, 3> pkIn {}, pkOut {};
        double minGain = 1.0;
        double pkInL[kMaxCh] {}, pkOutL[kMaxCh] {};

        for (int i = 0; i < numSamples; ++i)
        {
            std::array<double, 3> g;
            for (int b = 0; b < 3; ++b) g[b] = bandGain[b].next();
            const double gm = master.next(), gd = dry.next(), ge = edgeGain.next(), go = out.next();
            const double gi = gainIn.next(), mx = mixS.next(), wd = widthS.next();
            const double tightAmtNow = tightAmt.next();
            tightFreq2.target = tightFreq.next();   // two smoothing stages: block-wise automation becomes a smooth curve
            tightFreq2.next();
            // Re-tune the resonant HPF on every sample while it glides. (Re-tuning in coarser steps
            // leaves a faint buzz at the step rate; per-sample keeps the sweep clean.)
            {
                if (tightFreq2.cur != tightTuned)
                {
                    for (auto& t : tight) t.set (sr, tightFreq2.cur, 1.4);   // resonant bump = "punch"
                    tightTuned = tightFreq2.cur;
                }
            }

            double in[kMaxCh] {}, x[kMaxCh] {}, dryPath[kMaxCh] {};
            for (int c = 0; c < numCh; ++c)
            {
                in[c] = channels[c][i] * gi;
                pkInL[c] = std::max (pkInL[c], std::abs (in[c]));
                // The filter always runs (so it never restarts with stale state); Off just fades it out.
                x[c] = in[c] + tightAmtNow * (tight[c].process (in[c]) - in[c]);
                dryPath[c] = lowEndOn ? lowEnd[c].process (x[c]) : x[c];
            }

            double sub[kMaxCh] {};
            if (p.midMode || numCh == 1)
            {
                const double m = numCh == 2 ? 0.5 * (x[0] + x[1]) : x[0];
                const double s = runBands (synth[0], m, g, gm, pkIn, pkOut);
                for (int c = 0; c < numCh; ++c) sub[c] = s * gm;
            }
            else
            {
                for (int c = 0; c < numCh; ++c)
                    sub[c] = runBands (synth[c], x[c], g, gm, pkIn, pkOut) * gm;
            }

            double y[kMaxCh] {};
            for (int c = 0; c < numCh; ++c)
            {
                double wet = dryPath[c] * gd + sub[c];
                if (ge > 1e-6) wet += processEdge (edge[c], x[c] + sub[c]) * ge;
                y[c] = in[c] * (1.0 - mx) + wet * mx;
            }

            // M/S stage: Mono Maker (side removed below its frequency) and Stereo Width
            if (numCh == 2)
            {
                const double mid = 0.5 * (y[0] + y[1]);
                double side = 0.5 * (y[0] - y[1]);
                if (monoOn) side = monoHp.process (side);
                side *= wd;
                y[0] = mid + side;
                y[1] = mid - side;
            }

            if (++scopeDecim >= scopeEvery)
            {
                scopeDecim = 0;
                const int w = scopeWrite.load (std::memory_order_relaxed);
                scope[(size_t) w].store ((float) (sub[0] * mx * go), std::memory_order_relaxed);
                scopeWrite.store ((w + 1) % kScopeSize, std::memory_order_release);
            }

            // Safety limiter with 2 ms look-ahead: it sees a peak coming and eases the gain down
            // over those 2 ms instead of slamming it (which is what makes limiters sound crunchy),
            // so nothing passes the ceiling. 120 ms release; linked across channels. The audio is
            // always delayed by the look-ahead (reported to the host) so latency never changes.
            double pk = 0.0;
            for (int c = 0; c < numCh; ++c) { y[c] *= go; pk = std::max (pk, std::abs (y[c])); }
            const double req = (p.limit && pk > kCeiling) ? kCeiling / pk : 1.0;
            reqRing[(size_t) laPos] = req;
            double target = 1.0;
            for (int k = 0; k <= la; ++k) target = std::min (target, reqRing[(size_t) k]);
            gState = target < gState ? target : gState + limRel * (target - gState);
            boxSum += gState - boxRing[(size_t) boxPos];
            boxRing[(size_t) boxPos] = gState;
            boxPos = (boxPos + 1) % la;
            if (boxPos == 0) { boxSum = 0.0; for (int k = 0; k < la; ++k) boxSum += boxRing[(size_t) k]; }   // no drift
            const double lg = std::min (1.0, boxSum / la);
            laPos = (laPos + 1) % (la + 1);
            minGain = std::min (minGain, lg);

            for (int c = 0; c < numCh; ++c)
            {
                const double delayed = delayLine[(size_t) c][(size_t) dPos];
                delayLine[(size_t) c][(size_t) dPos] = y[c];
                y[c] = delayed * lg;
                pkOutL[c] = std::max (pkOutL[c], std::abs (y[c]));
                channels[c][i] = (float) y[c];
            }
            dPos = (dPos + 1) % la;
        }

        for (int b = 0; b < 3; ++b)
        {
            storeMax (meterBandIn[b], (float) pkIn[b]);
            storeMax (meterBandOut[b], (float) pkOut[b]);
        }
        storeMax (meterLimitGR, (float) (1.0 - minGain));
        for (int c = 0; c < 2; ++c)
        {
            const int src = std::min (c, numCh - 1);   // mono shows on both meters
            storeMax (meterIn[c], (float) pkInL[src]);
            storeMax (meterOut[c], (float) pkOutL[src]);
        }
    }

    const Params& params() const { return p; }

private:
    inline double runBands (SubGenerator& gen, double in, const std::array<double, 3>& g,
                            double gm, std::array<double, 3>& pkIn, std::array<double, 3>& pkOut)
    {
        std::array<double, 3> region;
        const auto bands = gen.process (in, trim, region);
        double s = 0;
        for (int b = 0; b < 3; ++b)
        {
            pkIn[b] = std::max (pkIn[b], std::abs (region[b]));
            const double v = bands[b] * g[b];
            s += v;
            pkOut[b] = std::max (pkOut[b], std::abs (v * gm));
        }
        return s;
    }

    inline double processEdge (EdgeChannel& e, double s)
    {
        for (auto& f : e.lowCut) s = f.process (s);
        for (auto& f : e.highCut) s = f.process (s);

        // Squeeze: fast peak compressor
        const double a = std::abs (s);
        e.compEnv += (a > e.compEnv ? compAtk : compRel) * (a - e.compEnv);
        const double envDb = 20.0 * std::log10 (e.compEnv + 1e-9);
        const double over = envDb - compThreshDb;
        if (over > 0) s *= std::pow (10.0, -over * (1.0 - 1.0 / compRatio) / 20.0);
        s *= compMakeup;

        // Drive: saturation
        const double d = s * drivePre;
        double y;
        if (p.edgeMode == 0)
            y = std::tanh (d);                                         // Smooth: symmetric, odd harmonics
        else
        {
            const double biased = d + 0.35;                            // Harsh: asymmetric hard clip → even + odd
            y = std::clamp (biased, -0.8, 1.0) - 0.35;
            y += 0.15 * std::sin (3.0 * y);                            // a little grit
        }
        return e.dc.process (y * drivePost);
    }

    static void storeMax (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    double sr = 44100.0;
    Params p;
    bool soloActive = false, tightOn = false;
    Smoothed tightAmt, tightFreq, tightFreq2;
    double tightTuned = -1.0;
    std::array<double, 3> trim { 1, 1, 1 };
    std::array<SubGenerator, kMaxCh> synth;
    std::array<SvfHighpass, kMaxCh> tight;
    std::array<Biquad, kMaxCh> lowEnd;
    LR4 monoHp;
    Smoothed gainIn, mixS, widthS;
    bool lowEndOn = false, monoOn = false;
    static constexpr double kCeiling = 0.9660509;   // -0.3 dBFS
    static constexpr int kMaxLookahead = 512;
    std::array<std::array<double, kMaxLookahead>, kMaxCh> delayLine {};
    std::array<double, kMaxLookahead> reqRing {}, boxRing {};
    double boxSum = 1.0, gState = 1.0, limRel = 0.001;
    int la = 96, laPos = 0, boxPos = 0, dPos = 0;

public:
    int latencySamples() const { return la; }
private:
    int scopeDecim = 0, scopeEvery = 8;
    float lastMono = -1000, lastLowEnd = -1000;
    std::array<EdgeChannel, kMaxCh> edge;
    std::array<Smoothed, 3> bandGain;
    Smoothed master, dry, edgeGain, out;
    float lastTight = -1, lastLowCut = -1, lastHighCut = -1;
    double compAtk = 0, compRel = 0, compThreshDb = -6, compRatio = 2, compMakeup = 1, drivePre = 1, drivePost = 1;
};

} // namespace undertow
