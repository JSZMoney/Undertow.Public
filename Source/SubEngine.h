// Undertow — 3-band subharmonic synthesizer engine
// Framework-independent C++17 so it can be unit-tested without JUCE.
//
// Signal flow (per sample):
//
//   in ─► Tight Punch (resonant HPF) ─┬───────────────────────────► Original level ─┐
//                                     │                                             │
//                                     ├─► [mid or per-channel] ─► 3 × BandSynth ──► Σ band levels ─► Subharmonics master ─┤
//                                     │                                             │
//                                     └─► Edge (LowCut/HighCut ► Squeeze ► Drive) ► Edge level ──────┤
//                                                                                                     ▼
//                                                                                              Output gain ─► out
//
// BandSynth: band-pass the input around one bass region, detect positive-going
// zero crossings (with hysteresis so noise can't double-trigger), and toggle a
// ±1 flip-flop on every full cycle. Multiplying the band signal by that flip-flop
// yields a waveform whose fundamental is exactly half the input frequency, follows
// the input's amplitude envelope, and has no discontinuities (flips happen at zero).
// A 6th-order low-pass then removes the 3f/2 by-product, leaving a near-sine sub.

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
struct BandDef { double detectLo, detectHi; const char* name; };

// Input regions that get folded down one octave.
//   Low : 48–72 Hz  → subs 24–36 Hz   (dbx 120-style low band)
//   Mid : 72–112 Hz → subs 36–56 Hz   (dbx 120-style upper band)
//   High: 112–220 Hz→ subs 56–110 Hz  (the extra "high bass" band)
constexpr std::array<BandDef, 3> kBands { { { 48.0, 72.0, "Low" }, { 72.0, 112.0, "Mid" }, { 112.0, 220.0, "High" } } };

struct BandSynth
{
    Biquad hp[2], lp[2];   // detection band-pass (4th-order each side)
    Biquad post[3];        // 6th-order LP to clean the sub
    Biquad postHp;         // keeps DC / sub-audible junk out
    double env = 0, envAtk = 0, envRel = 0;
    double gate = 0, gateAtk = 0, gateRel = 0;
    double flip = 1.0, prev = 0.0;
    bool armed = false;
    bool invertPending = false;   // set by the engine to phase-lock this band's divider to its lower neighbour
    double corr = 0, pow = 0;     // running correlation with lower neighbour / own power (for that lock)

    void prepare (double sr, const BandDef& b)
    {
        for (int i = 0; i < 2; ++i)
        {
            hp[i].setHighpass (sr, b.detectLo, kBw4[i]);
            lp[i].setLowpass (sr, b.detectHi, kBw4[i]);
        }
        const double subHi = b.detectHi * 0.5 * 1.15;
        for (int i = 0; i < 3; ++i) post[i].setLowpass (sr, subHi, kBw6[i]);
        postHp.setHighpass (sr, std::max (15.0, b.detectLo * 0.5 * 0.7), 0.7071);

        envAtk  = 1.0 - std::exp (-1.0 / (sr * 0.002));
        envRel  = 1.0 - std::exp (-1.0 / (sr * 0.040));
        gateAtk = 1.0 - std::exp (-1.0 / (sr * 0.005));
        gateRel = 1.0 - std::exp (-1.0 / (sr * 0.060));
        reset();
    }

    void reset()
    {
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : post) f.reset();
        postHp.reset();
        env = gate = prev = corr = pow = 0.0; flip = 1.0; armed = false; invertPending = false;
    }

    // Returns the synthesized sub. 'bandOut' receives the detected band signal (for the activity indicator).
    inline double process (double x, double trim, double& bandOut)
    {
        double b = x * trim;
        b = hp[1].process (hp[0].process (b));
        b = lp[1].process (lp[0].process (b));
        bandOut = b;

        const double a = std::abs (b);
        env += (a > env ? envAtk : envRel) * (a - env);

        // Schmitt-style arming: must dip below -thr before the next upward zero-crossing counts.
        const double thr = env * 0.15 + 1e-7;
        if (b < -thr) armed = true;
        if (armed && prev <= 0.0 && b > 0.0)
        {
            // Skipping one toggle == inverting polarity, done exactly at a zero crossing so it's click-free.
            if (invertPending) invertPending = false;
            else flip = -flip;
            armed = false;
        }
        prev = b;

        double s = b * flip;

        const double gateTarget = env > 1e-4 ? 1.0 : 0.0;   // ≈ -80 dBFS gate
        gate += (gateTarget > gate ? gateAtk : gateRel) * (gateTarget - gate);
        s *= gate;

        for (auto& f : post) s = f.process (s);
        return postHp.process (s);
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
    float subMasterDb = -6.0f;
    float dryDb = 0.0f;
    bool  midMode = true;                       // generate subs from the mid (L+R) signal only
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
    std::atomic<float> meterOut { 0.0f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int c = 0; c < kMaxCh; ++c)
        {
            for (int b = 0; b < 3; ++b) synth[c][b].prepare (sr, kBands[b]);
            tight[c].reset();
            edge[c].reset();
            edge[c].dc.setHighpass (sr, 12.0, 0.7071);
        }
        for (int b = 0; b < 3; ++b) bandGain[b].prepare (sr, 20, dbToGain (p.levelDb[b]));
        master.prepare (sr, 20, dbToGain (p.subMasterDb));
        dry.prepare (sr, 20, dbToGain (p.dryDb));
        edgeGain.prepare (sr, 20, 0.0);
        out.prepare (sr, 20, dbToGain (p.outputDb));
        lastTight = lastLowCut = lastHighCut = -1.0f;
        corrCoef = 1.0 - std::exp (-1.0 / (sr * 0.030));
        compAtk = 1.0 - std::exp (-1.0 / (sr * 0.001));
        compRel = 1.0 - std::exp (-1.0 / (sr * 0.080));
        setParams (p);
    }

    void reset()
    {
        for (int c = 0; c < kMaxCh; ++c)
        {
            for (auto& s : synth[c]) s.reset();
            tight[c].reset();
            edge[c].reset();
        }
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
        dry.target      = soloActive ? 0.0 : dbToGain (p.dryDb);
        edgeGain.target = (p.edgeOn && ! soloActive) ? dbToGain (p.edgeLevelDb) : 0.0;
        out.target      = dbToGain (p.outputDb);

        tightOn = p.tightPunchHz > 12.0f;
        if (p.tightPunchHz != lastTight)
        {
            for (auto& t : tight) t.setHighpass (sr, p.tightPunchHz, 1.4);   // resonant bump = "punch"
            lastTight = p.tightPunchHz;
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
        double pkMain = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            double x[kMaxCh] {};
            for (int c = 0; c < numCh; ++c)
            {
                x[c] = channels[c][i];
                if (tightOn) x[c] = tight[c].process (x[c]);
            }

            std::array<double, 3> g;
            for (int b = 0; b < 3; ++b) g[b] = bandGain[b].next();
            const double gm = master.next(), gd = dry.next(), ge = edgeGain.next(), go = out.next();

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
                {
                    sub[c] = runBands (synth[c], x[c], g, gm, pkIn, pkOut) * gm;
                }
            }

            for (int c = 0; c < numCh; ++c)
            {
                double y = x[c] * gd + sub[c];
                if (ge > 1e-6) y += processEdge (edge[c], x[c] + sub[c]) * ge;
                y *= go;
                pkMain = std::max (pkMain, std::abs (y));
                channels[c][i] = (float) y;
            }
        }

        for (int b = 0; b < 3; ++b)
        {
            storeMax (meterBandIn[b], (float) pkIn[b]);
            storeMax (meterBandOut[b], (float) pkOut[b]);
        }
        storeMax (meterOut, (float) pkMain);
    }

    const Params& params() const { return p; }

private:
    // Runs the three band synths on one signal and sums them.
    // Band-pass skirts overlap, so a tone near a band edge is seen by two detectors whose
    // flip-flops aren't phase-locked — their subs could partially cancel, and the leaking band
    // also lets its 3f/2 by-product through. Each band is therefore weighted by how dominant its
    // envelope is versus its neighbours (4th-power ratio): the band that "owns" a note speaks,
    // leakage is suppressed, while genuinely separate notes in different bands all come through.
    inline double runBands (std::array<BandSynth, 3>& bands, double in, const std::array<double, 3>& g,
                            double gm, std::array<double, 3>& pkIn, std::array<double, 3>& pkOut)
    {
        std::array<double, 3> raw, e4;
        for (int b = 0; b < 3; ++b)
        {
            double bandSig;
            raw[b] = bands[b].process (in, trim[b], bandSig);
            pkIn[b] = std::max (pkIn[b], std::abs (bandSig));
            const double e2 = bands[b].env * bands[b].env;
            e4[b] = e2 * e2;
        }
        // Polarity lock: if band b's sub is anti-correlated with band b-1's (same note seen by both),
        // ask band b to skip its next toggle so the two add instead of cancelling.
        for (int b = 1; b < 3; ++b)
        {
            auto& hi = bands[b];
            hi.corr += corrCoef * (raw[b] * raw[b - 1] - hi.corr);
            hi.pow  += corrCoef * (raw[b] * raw[b] - hi.pow);
            const double lowPow = bands[b - 1].pow;
            if (! hi.invertPending && hi.corr < -0.3 * std::sqrt (hi.pow * lowPow) - 1e-12)
            {
                hi.invertPending = true;
                hi.corr = 0;
            }
        }
        bands[0].pow += corrCoef * (raw[0] * raw[0] - bands[0].pow);

        double s = 0;
        for (int b = 0; b < 3; ++b)
        {
            double denom = e4[b] + 1e-30;
            if (b > 0) denom += e4[b - 1];
            if (b < 2) denom += e4[b + 1];
            const double v = raw[b] * (e4[b] / denom) * g[b];
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
    std::array<std::array<BandSynth, 3>, kMaxCh> synth;
    std::array<Biquad, kMaxCh> tight;
    std::array<EdgeChannel, kMaxCh> edge;
    std::array<Smoothed, 3> bandGain;
    Smoothed master, dry, edgeGain, out;
    float lastTight = -1, lastLowCut = -1, lastHighCut = -1;
    double corrCoef = 0.001;
    double compAtk = 0, compRel = 0, compThreshDb = -6, compRatio = 2, compMakeup = 1, drivePre = 1, drivePost = 1;
};

} // namespace undertow
