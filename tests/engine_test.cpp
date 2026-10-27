// Headless check of the subharmonic engine: g++ -O2 -std=c++17 -I../Source engine_test.cpp && ./a.out
#include "SubEngine.h"
#include <cstdio>
#include <vector>
#include <random>

using namespace undertow;

static double goertzelDb (const std::vector<float>& x, double f, double sr, size_t start)
{
    const double w = 2.0 * kPi * f / sr, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    const size_t n = x.size() - start;
    for (size_t i = start; i < x.size(); ++i) { double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    const double mag = std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / (double) n;
    return 20.0 * std::log10 (mag + 1e-12);
}

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

int main()
{
    const double sr = 48000.0;
    const int N = (int) sr * 2, block = 256;

    // 1) Sine in each band → sub-only output should be dominated by f/2
    for (double f : { 60.0, 90.0, 160.0 })
    {
        SubEngine e; Params p; p.solo = { true, true, true }; p.subMasterDb = 0; p.midMode = true;
        e.setParams (p); e.prepare (sr);
        std::vector<float> L (N), R (N);
        for (int i = 0; i < N; ++i) L[i] = R[i] = (float) (0.5 * std::sin (2 * kPi * f * i / sr));
        for (int i = 0; i < N; i += block) { float* ch[2] = { L.data() + i, R.data() + i }; e.process (ch, 2, std::min (block, N - i)); }

        const size_t st = (size_t) sr / 2;  // skip settling
        const double sub = goertzelDb (L, f / 2, sr, st), fund = goertzelDb (L, f, sr, st), h3 = goertzelDb (L, 1.5 * f, sr, st);
        std::printf ("  in %5.1f Hz @ -6 dBFS → sub %5.1f Hz: %6.1f dB | at f: %6.1f dB | at 3f/2: %6.1f dB\n", f, f / 2, sub, fund, h3);
        char msg[128]; std::snprintf (msg, sizeof msg, "%.0f Hz produces strong %.0f Hz sub (> -15 dB, > 15 dB above by-products)", f, f / 2);
        check (sub > -15 && sub - std::max (fund, h3) > 15, msg);
        check (std::abs (L[N - 1] - R[N - 1]) < 1e-6, "mid mode keeps L/R subs identical");
    }

    // 2) Silence in → silence out (gate), and no NaNs with white noise + all features on
    {
        SubEngine e; Params p; p.edgeOn = true; p.edgeMode = 1; p.drive = 1; p.squeeze = 1; p.tightPunchHz = 80; p.midMode = false;
        p.trimDb = { 18, 18, 18 }; p.levelDb = { 12, 12, 12 }; p.subMasterDb = 12;
        e.setParams (p); e.prepare (sr);
        std::vector<float> L (N, 0.0f), R (N, 0.0f);
        std::mt19937 rng (1); std::uniform_real_distribution<float> u (-1, 1);
        for (int i = N / 2; i < N; ++i) { L[i] = u (rng); R[i] = u (rng); }
        for (int i = 0; i < N; i += block) { float* ch[2] = { L.data() + i, R.data() + i }; e.process (ch, 2, std::min (block, N - i)); }
        double silentPeak = 0; bool finite = true; double peak = 0;
        for (int i = 0; i < N / 2; ++i) silentPeak = std::max (silentPeak, (double) std::abs (L[i]));
        for (int i = 0; i < N; ++i) { finite &= std::isfinite (L[i]) && std::isfinite (R[i]); peak = std::max (peak, (double) std::abs (L[i])); }
        check (silentPeak < 1e-6, "silence stays silent");
        check (finite, "no NaN/Inf under extreme settings with noise");
        std::printf ("  extreme-settings noise peak: %.2f (%.1f dBFS)\n", peak, 20 * std::log10 (peak));
    }

    // 3) Solo isolates one band: a 160 Hz note's sub lives in the High band, so soloing Mid
    //    (which also mutes the dry signal) must be far quieter than soloing High.
    {
        auto render = [&] (int soloBand)
        {
            SubEngine e; Params p; p.solo = { false, false, false }; p.solo[(size_t) soloBand] = true;
            e.setParams (p); e.prepare (sr);
            std::vector<float> L (N);
            for (int i = 0; i < N; ++i) L[i] = (float) (0.5 * std::sin (2 * kPi * 160.0 * i / sr));
            for (int i = 0; i < N; i += block) { float* ch[1] = { L.data() + i }; e.process (ch, 1, std::min (block, N - i)); }
            double pk = 0; for (int i = N / 2; i < N; ++i) pk = std::max (pk, (double) std::abs (L[i]));
            return 20.0 * std::log10 (pk + 1e-12);
        };
        const double hi = render (2), mid = render (1);
        std::printf ("  160 Hz note: solo High %.1f dB, solo Mid %.1f dB\n", hi, mid);
        check (hi - mid > 12.0, "solo isolates bands (dry muted, other bands > 12 dB down)");
    }

    // 4) Global section: transparency, Mix, Gain In, Width, Mono Maker
    {
        auto run = [&] (const Params& p, std::vector<float>& L, std::vector<float>& R)
        {
            SubEngine e; e.setParams (p); e.prepare (sr);
            for (int i = 0; i < N; i += block) { float* ch[2] = { L.data() + i, R.data() + i }; e.process (ch, 2, std::min (block, N - i)); }
        };
        // Stereo test signal: 50 Hz only on the side, 2 kHz only on the side, 120 Hz in the mid
        auto make = [&] (std::vector<float>& L, std::vector<float>& R)
        {
            L.resize (N); R.resize (N);
            for (int i = 0; i < N; ++i)
            {
                const double m = 0.3 * std::sin (2 * kPi * 120 * i / sr);
                const double s = 0.2 * std::sin (2 * kPi * 50 * i / sr) + 0.2 * std::sin (2 * kPi * 2000 * i / sr);
                L[i] = (float) (m + s); R[i] = (float) (m - s);
            }
        };
        const size_t st = (size_t) sr / 2;
        auto maxDiff = [&] (const std::vector<float>& a, const std::vector<float>& b)
        { double d = 0; for (size_t i = st; i < a.size(); ++i) d = std::max (d, (double) std::abs (a[i] - b[i])); return d; };

        std::vector<float> L0, R0, L, R; make (L0, R0);

        Params neutral; neutral.levelDb = { kMinDb, kMinDb, kMinDb };
        L = L0; R = R0; run (neutral, L, R);
        check (maxDiff (L, L0) < 1e-4 && maxDiff (R, R0) < 1e-4, "neutral settings (subs off) are transparent");

        Params dryMix; dryMix.mix = 0.0f;
        L = L0; R = R0; run (dryMix, L, R);
        check (maxDiff (L, L0) < 1e-4, "Mix 0% returns the untouched input");

        Params gain = neutral; gain.gainInDb = 6.0f; gain.outputDb = -6.0f;
        L = L0; R = R0; run (gain, L, R);
        check (maxDiff (L, L0) < 1e-3, "Gain In +6 dB and Gain Out -6 dB cancel");

        Params mono = neutral; mono.width = 0.0f;
        L = L0; R = R0; run (mono, L, R);
        check (maxDiff (L, R) < 1e-4, "Stereo Width 0% gives mono");

        Params mm = neutral; mm.monoMakerHz = 200.0f;
        L = L0; R = R0; run (mm, L, R);
        std::vector<float> S (N), S0 (N);
        for (int i = 0; i < N; ++i) { S[i] = 0.5f * (L[i] - R[i]); S0[i] = 0.5f * (L0[i] - R0[i]); }
        const double s50 = goertzelDb (S, 50, sr, st) - goertzelDb (S0, 50, sr, st);
        const double s2k = goertzelDb (S, 2000, sr, st) - goertzelDb (S0, 2000, sr, st);
        std::printf ("  Mono Maker 200 Hz: side @50 Hz %.1f dB, side @2 kHz %.2f dB\n", s50, s2k);
        check (s50 < -20.0 && std::abs (s2k) < 0.5, "Mono Maker removes low side content, keeps highs");
    }

    // 5) Robustness: buffer sizes, sample rates, fast automation
    {
        // Bass + kick-ish test signal
        auto makeBass = [] (double rate, int n)
        {
            std::vector<float> v ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / rate, tb = std::fmod (t, 0.5);
                v[(size_t) i] = (float) (0.35 * std::sin (2 * kPi * 65.4 * t) + 0.12 * std::sin (2 * kPi * 130.8 * t)
                                         + 0.4 * std::sin (2 * kPi * (50 + 90 * std::exp (-tb * 30)) * tb) * std::exp (-tb * 8));
            }
            return v;
        };
        auto runBlocks = [] (SubEngine& e, std::vector<float>& L, std::vector<float>& R, int bs)
        {
            const int n = (int) L.size();
            for (int i = 0; i < n; i += bs) { float* ch[2] = { L.data() + i, R.data() + i }; e.process (ch, 2, std::min (bs, n - i)); }
        };

        // Same output regardless of host buffer size
        {
            const auto src = makeBass (sr, N);
            std::vector<float> refL = src, refR = src;
            { SubEngine e; Params p; p.edgeOn = true; e.setParams (p); e.prepare (sr); runBlocks (e, refL, refR, 4096); }
            double worst = 0;
            for (int bs : { 1, 7, 64, 333, 1024 })
            {
                std::vector<float> L = src, R = src;
                SubEngine e; Params p; p.edgeOn = true; e.setParams (p); e.prepare (sr); runBlocks (e, L, R, bs);
                for (int i = 0; i < N; ++i) worst = std::max (worst, (double) std::abs (L[(size_t) i] - refL[(size_t) i]));
            }
            check (worst < 1e-5, "identical output at buffer sizes 1, 7, 64, 333, 1024, 4096");
        }

        // Works the same at 44.1 and 96 kHz
        for (double rate : { 44100.0, 96000.0 })
        {
            const int n = (int) rate * 2;
            std::vector<float> L ((size_t) n), R;
            for (int i = 0; i < n; ++i) L[(size_t) i] = (float) (0.5 * std::sin (2 * kPi * 90.0 * i / rate));
            R = L;
            SubEngine e; Params p; p.solo = { true, true, true }; e.setParams (p); e.prepare (rate); runBlocks (e, L, R, 512);
            const double sub = goertzelDb (L, 45.0, rate, (size_t) n / 2), junk = goertzelDb (L, 135.0, rate, (size_t) n / 2);
            char msg[96]; std::snprintf (msg, sizeof msg, "%.0f kHz: 90 Hz -> 45 Hz sub %.1f dB, by-product %.1f dB", rate / 1000, sub, junk);
            check (sub > -12.0 && sub - junk > 30.0, msg);
        }

        // Fast automation of every filter-type control must not click.
        // Input is pure low end (< 300 Hz), so anything above 2 kHz in the output is a click/zipper artefact.
        {
            std::vector<float> L ((size_t) N), R;
            for (int i = 0; i < N; ++i) L[(size_t) i] = (float) (0.4 * std::sin (2 * kPi * 80.0 * i / sr) + 0.2 * std::sin (2 * kPi * 160.0 * i / sr));
            R = L;
            std::vector<float> inCopy = L;
            SubEngine e; Params p; e.setParams (p); e.prepare (sr);
            const int bs = 64;
            for (int i = 0, blk = 0; i < N; i += bs, ++blk)
            {
                const double ph = 0.5 + 0.5 * std::sin (blk * 0.05);       // fast sweep, ~4 per second
                p.tightPunchHz  = (float) (10 + 240 * ph);
                p.lowEndDb      = (float) (-12 + 24 * ph);
                p.monoMakerHz   = (float) (20 + 480 * ph);
                p.edgeLowCutHz  = (float) (20 + 600 * ph);
                p.edgeHighCutHz = (float) (300 + 15000 * ph);
                p.mix = (float) ph; p.width = (float) (2 * ph); p.subMasterDb = (float) (-30 + 30 * ph);
                p.levelDb = { (float) (-20 * ph), (float) (-20 * (1 - ph)), 0.0f };
                e.setParams (p);
                float* ch[2] = { L.data() + i, R.data() + i };
                e.process (ch, 2, std::min (bs, N - i));
            }
            auto hfDb = [&] (const std::vector<float>& x)
            {
                Biquad h[4]; for (int k = 0; k < 4; ++k) h[k].setHighpass (sr, 2000.0, kBw4[k % 2]);
                double hf = 0, tot = 0;
                // run the measuring filter from the very start, so its own start-up transient is never counted
                for (size_t i = 0; i < x.size(); ++i)
                {
                    double v = x[i]; const double raw = v;
                    for (auto& b : h) v = b.process (v);
                    if (i >= (size_t) sr / 4) { tot += raw * raw; hf += v * v; }
                }
                return 10 * std::log10 (hf / tot + 1e-20);
            };
            const double hfOut = hfDb (L);
            char msg[96]; std::snprintf (msg, sizeof msg, "fast automation of all filters/levels: HF artefacts %.1f dB", hfOut);
            check (hfOut < -60.0, msg);
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
