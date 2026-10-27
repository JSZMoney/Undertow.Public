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

// Input regions (what the detector listens to, each with its own Trim) and the
// output bands their subs land in, dbx 120-style:
//   Low : notes 48–72 Hz  → sub 24–36 Hz
//   Mid : notes 72–112 Hz → sub 36–56 Hz
//   High: notes 112–220 Hz→ sub 56–110 Hz
constexpr std::array<BandDef, 3> kBands { { { 48.0, 72.0 }, { 72.0, 112.0 }, { 112.0, 220.0 } } };
constexpr double kXoverLow = 36.0, kXoverHigh = 56.0;

// Linkwitz-Riley 4th-order low/high-pass (two cascaded Butterworth 2nd-order sections)
struct LR4
{
    Biquad a, b;
    void setLowpass (double sr, double f)  { a.setLowpass (sr, f, 0.70710678);  b.setLowpass (sr, f, 0.70710678); }
    void setHighpass (double sr, double f) { a.setHighpass (sr, f, 0.70710678); b.setHighpass (sr, f, 0.70710678); }
    void reset() { a.reset(); b.reset(); }
    inline double process (double x) { return b.process (a.process (x)); }
};

// One subharmonic generator (one per channel, or one for the mid signal).
//
// 1. The three input regions are band-passed, trimmed and summed.
// 2. A single detector follows the FUNDAMENTAL of that signal: zero crossings are
//    taken from a low-pass-tilted copy so harmonics can't cause extra triggers, with
//    hysteresis and a minimum-period lockout against noise.
// 3. Every input cycle flips the polarity of the sub and starts a new half-sine arc
//    lasting one input period, giving a clean sine at exactly half the fundamental.
//    (A sine at f0/2 can never cancel the original note or its harmonics.)
// 4. Its loudness follows the input's envelope; a gate silences it below -74 dBFS.
// 5. Low-passed, then split by Linkwitz-Riley crossovers into the 3 output bands
//    (which sum back flat when all band levels are equal).
struct SubGenerator
{
    LR4 inHp48, inLp220, rLowLp, rRestHp, rMidLp, rHighHp, apInLp, apInHp;   // input-region crossovers
    Biquad tilt;
    Biquad post[3];
    Biquad postHp;
    LR4 xLowLp, xLowHp, xHighLp, xHighHp, apLp, apHp;

    double env = 0, envAtk = 0, envRel = 0;
    double ampEnv = 0, gate = 0, gateAtk = 0, gateRel = 0;
    double flip = 1.0, prevTilt = 0.0;
    double period = 400.0, ampSmooth = 0.0, ampCoef = 0.0;
    bool armed = false;
    int sinceFlip = 0, minPeriod = 0, maxPeriod = 0;

    static constexpr double kMakeup = 1.3;   // brings the sub up to roughly the level of the note it comes from

    void prepare (double sr)
    {
        inHp48.setHighpass (sr, kBands[0].lo);  inLp220.setLowpass (sr, kBands[2].hi);
        rLowLp.setLowpass (sr, kBands[0].hi);   rRestHp.setHighpass (sr, kBands[0].hi);
        rMidLp.setLowpass (sr, kBands[1].hi);   rHighHp.setHighpass (sr, kBands[1].hi);
        apInLp.setLowpass (sr, kBands[1].hi);   apInHp.setHighpass (sr, kBands[1].hi);
        tilt.setLowpass (sr, 70.0, 0.6);
        for (int i = 0; i < 3; ++i) post[i].setLowpass (sr, 120.0, kBw6[i]);
        postHp.setHighpass (sr, 18.0, 0.70710678);
        xLowLp.setLowpass (sr, kXoverLow);   xLowHp.setHighpass (sr, kXoverLow);
        xHighLp.setLowpass (sr, kXoverHigh); xHighHp.setHighpass (sr, kXoverHigh);
        apLp.setLowpass (sr, kXoverHigh);    apHp.setHighpass (sr, kXoverHigh);

        envAtk  = 1.0 - std::exp (-1.0 / (sr * 0.002));
        envRel  = 1.0 - std::exp (-1.0 / (sr * 0.050));
        gateAtk = 1.0 - std::exp (-1.0 / (sr * 0.004));
        gateRel = 1.0 - std::exp (-1.0 / (sr * 0.080));
        minPeriod = (int) (sr / 240.0);
        maxPeriod = (int) (sr / 40.0);
        ampCoef = 1.0 - std::exp (-1.0 / (sr * 0.006));   // smooth the loudness so it can't add grit
        reset();
    }

    void reset()
    {
        for (auto* l : { &inHp48, &inLp220, &rLowLp, &rRestHp, &rMidLp, &rHighHp, &apInLp, &apInHp }) l->reset();
        tilt.reset(); for (auto& f : post) f.reset(); postHp.reset();
        for (auto* l : { &xLowLp, &xLowHp, &xHighLp, &xHighHp, &apLp, &apHp }) l->reset();
        env = ampEnv = gate = prevTilt = ampSmooth = 0.0;
        flip = 1.0; armed = false; sinceFlip = 0; period = minPeriod > 0 ? minPeriod * 4.0 : 400.0;
    }

    // Returns the three output-band subs; regionLevel receives each trimmed input region (for the LEDs).
    inline std::array<double, 3> process (double x, const std::array<double, 3>& trim, std::array<double, 3>& regionLevel)
    {
        // Split 48–220 Hz into the three regions with Linkwitz-Riley crossovers, so that with
        // equal trims they sum back to a flat band (no cancellation where regions meet).
        const double bandIn = inLp220.process (inHp48.process (x));
        double lowR = rLowLp.process (bandIn);
        const double restR = rRestHp.process (bandIn);
        lowR = apInLp.process (lowR) + apInHp.process (lowR);    // phase-match the 112 Hz split
        regionLevel = { lowR * trim[0], rMidLp.process (restR) * trim[1], rHighHp.process (restR) * trim[2] };
        const double sig = regionLevel[0] + regionLevel[1] + regionLevel[2];

        // Fundamental tracker: one trigger per input cycle, from a low-pass-tilted copy so
        // harmonics can't cause extra triggers; hysteresis + lockout reject noise.
        const double t = tilt.process (sig);
        env += (std::abs (t) > env ? envAtk : envRel) * (std::abs (t) - env);
        const double thr = env * 0.2 + 1e-7;
        ++sinceFlip;
        if (t < -thr) armed = true;
        if (armed && prevTilt <= 0.0 && t > 0.0 && sinceFlip >= minPeriod)
        {
            // Each input cycle = half a cycle of the sub. Remember how long it took.
            period += 0.5 * (std::clamp ((double) sinceFlip, (double) minPeriod, (double) maxPeriod) - period);
            flip = -flip;
            armed = false;
            sinceFlip = 0;
        }
        prevTilt = t;

        // Sine-shaped sub: each half-cycle is a smooth arc lasting one input period, so the
        // result is a clean sine at f0/2 rather than a buzzy square-ish wave.
        const double ph = std::min (1.0, sinceFlip / period);
        const double arc = std::sin (kPi * ph) * flip;

        // Loudness follows the input; gate keeps the noise floor from producing rumble.
        ampEnv += (std::abs (sig) > ampEnv ? envAtk : envRel) * (std::abs (sig) - ampEnv);
        const double gateTarget = ampEnv > 2e-4 ? 1.0 : 0.0;
        gate += (gateTarget > gate ? gateAtk : gateRel) * (gateTarget - gate);
        ampSmooth += ampCoef * (ampEnv - ampSmooth);

        double s = arc * ampSmooth * gate * kMakeup;
        for (auto& f : post) s = f.process (s);
        s = postHp.process (s);

        // Split into output bands; the low band goes through a matching all-pass so the sum stays flat
        double low  = xLowLp.process (s);
        const double rest = xLowHp.process (s);
        low = apLp.process (low) + apHp.process (low);
        return { low, xHighLp.process (rest), xHighHp.process (rest) };
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
};

//==============================================================================
class SubEngine
{
public:
    static constexpr int kMaxCh = 2;

    // Meters (written by audio thread as block peaks, read & cleared by the UI)
    std::array<std::atomic<float>, 3> meterBandIn {}, meterBandOut {};
    std::array<std::atomic<float>, 2> meterIn {}, meterOut {};

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
        scopeEvery = std::max (1, (int) (sr / 6000.0));   // ~85 ms of signal across the scope
        compAtk = 1.0 - std::exp (-1.0 / (sr * 0.001));
        compRel = 1.0 - std::exp (-1.0 / (sr * 0.080));
        setParams (p);
        // Start every smoothed control exactly at its current value (no fade-in on playback start)
        for (auto& g : bandGain) g.cur = g.target;
        for (auto* sm : { &master, &dry, &edgeGain, &out, &gainIn, &mixS, &widthS }) sm->cur = sm->target;
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

        tightOn = p.tightPunchHz > 12.0f;
        if (p.tightPunchHz != lastTight)
        {
            for (auto& t : tight) t.setHighpass (sr, p.tightPunchHz, 1.4);   // resonant bump = "punch"
            lastTight = p.tightPunchHz;
        }
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
        double pkInL[kMaxCh] {}, pkOutL[kMaxCh] {};

        for (int i = 0; i < numSamples; ++i)
        {
            std::array<double, 3> g;
            for (int b = 0; b < 3; ++b) g[b] = bandGain[b].next();
            const double gm = master.next(), gd = dry.next(), ge = edgeGain.next(), go = out.next();
            const double gi = gainIn.next(), mx = mixS.next(), wd = widthS.next();

            double in[kMaxCh] {}, x[kMaxCh] {}, dryPath[kMaxCh] {};
            for (int c = 0; c < numCh; ++c)
            {
                in[c] = channels[c][i] * gi;
                pkInL[c] = std::max (pkInL[c], std::abs (in[c]));
                x[c] = tightOn ? tight[c].process (in[c]) : in[c];
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

            for (int c = 0; c < numCh; ++c)
            {
                y[c] *= go;
                pkOutL[c] = std::max (pkOutL[c], std::abs (y[c]));
                channels[c][i] = (float) y[c];
            }
        }

        for (int b = 0; b < 3; ++b)
        {
            storeMax (meterBandIn[b], (float) pkIn[b]);
            storeMax (meterBandOut[b], (float) pkOut[b]);
        }
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
    std::array<double, 3> trim { 1, 1, 1 };
    std::array<SubGenerator, kMaxCh> synth;
    std::array<Biquad, kMaxCh> tight, lowEnd;
    LR4 monoHp;
    Smoothed gainIn, mixS, widthS;
    bool lowEndOn = false, monoOn = false;
    int scopeDecim = 0, scopeEvery = 8;
    float lastMono = -1000, lastLowEnd = -1000;
    std::array<EdgeChannel, kMaxCh> edge;
    std::array<Smoothed, 3> bandGain;
    Smoothed master, dry, edgeGain, out;
    float lastTight = -1, lastLowCut = -1, lastHighCut = -1;
    double compAtk = 0, compRel = 0, compThreshDb = -6, compRatio = 2, compMakeup = 1, drivePre = 1, drivePost = 1;
};

} // namespace undertow
