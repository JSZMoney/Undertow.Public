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
        SubEngine e; Params p; p.dryDb = kMinDb; p.subMasterDb = 0; p.midMode = true;
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

    // 3) Solo isolates one band
    {
        SubEngine e; Params p; p.solo = { false, true, false };
        e.setParams (p); e.prepare (sr);
        std::vector<float> L (N);
        for (int i = 0; i < N; ++i) L[i] = (float) (0.5 * std::sin (2 * kPi * 160.0 * i / sr));   // High-band content only
        for (int i = 0; i < N; i += block) { float* ch[1] = { L.data() + i }; e.process (ch, 1, std::min (block, N - i)); }
        double pk = 0; for (int i = N / 2; i < N; ++i) pk = std::max (pk, (double) std::abs (L[i]));
        check (pk < 0.02, "soloing Mid mutes dry signal and the High band");
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
