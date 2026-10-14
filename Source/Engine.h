// Doodle Synth — sound engine.
// Mirrors the browser prototype: three drawn wavetable oscillators (16 frames each) with warp,
// unison, a resonant filter with LFO/envelope, amp envelope, vibrato, three point-curve mods
// that can be routed to almost any knob, and an FX chain (drive, phaser, flanger, chorus, delay, reverb).
// Tempo-synced things follow the host (DAW) tempo and song position.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace ds
{
constexpr int N = 256;      // samples per drawn cycle
constexpr int NF = 16;      // wavetable frames
constexpr int NH = 127;     // harmonics kept from a 256-sample cycle
constexpr int TL = 1024;    // playback table length
constexpr int MIPS = 8;     // band-limited table levels (127, 64, 32 ... 1 harmonics)
constexpr int MAXV = 10;    // polyphony
constexpr int MAXU = 7;     // unison voices per oscillator
constexpr int CHUNK = 32;   // control-rate chunk in samples
constexpr float TAU = 6.28318530718f;

inline float clampf (float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

//==============================================================================
// Modulatable parameters (same names/ranges as the UI knobs)
enum OscParam { O_LEVEL, O_SEMI, O_FINE, O_SPREAD, O_POS, O_SQUIG, O_FOLD, O_BEND, O_LEFT, O_RIGHT, O_DRIVE, O_CRUSH, O_COUNT };
enum GlobalParam
{
    M_TRANS = 3 * O_COUNT, M_DRIVE, M_VOL,
    PL_AMT, PL_RATE,
    F_CUTOFF, F_RES, F_RATE, F_LAMT,
    PH_RATE, PH_DEPTH, PH_FB, PH_METAL, PH_MIX,
    FL_RATE, FL_DEPTH, FL_FB, FL_MIX,
    CH_RATE, CH_DEPTH, CH_MIX,
    DL_TIME, DL_FB, DL_TONE, DL_MIX,
    RV_DECAY, RV_SIZE, RV_TONE, RV_MIX,
    NP
};

struct Spec { float mn, mx; int curve; float step; }; // curve: 0 lin, 1 exp, 2 pow

inline Spec specOf (int id)
{
    if (id < 3 * O_COUNT)
    {
        switch (id % O_COUNT)
        {
            case O_SEMI:  return { -24, 24, 0, 1 };
            case O_FINE:  return { -100, 100, 0, 0 };
            case O_BEND: case O_LEFT: case O_RIGHT: return { -1, 1, 0, 0 };
            default:      return { 0, 1, 0, 0 };
        }
    }
    switch (id)
    {
        case M_TRANS:  return { -24, 24, 0, 1 };
        case PL_AMT:   return { 0, 1200, 2, 0 };
        case PL_RATE:  return { 0.1f, 20, 1, 0 };
        case F_CUTOFF: return { 30, 18000, 1, 0 };
        case F_RATE:   return { 0.05f, 20, 1, 0 };
        case F_LAMT:   return { 0, 5, 0, 0 };
        case FL_FB:    return { -1, 1, 0, 0 };
        case CH_RATE:  return { 0.05f, 6, 1, 0 };
        case DL_TIME:  return { 0.03f, 1.2f, 2, 0 };
        case DL_FB:    return { 0, 0.95f, 0, 0 };
        case RV_DECAY: return { 0.2f, 10, 1, 0 };
        default:       return { 0, 1, 0, 0 };
    }
}
inline float toP (const Spec& s, float v)
{
    float p = s.curve == 1 ? std::log (v / s.mn) / std::log (s.mx / s.mn)
            : s.curve == 2 ? std::sqrt (std::max (0.0f, (v - s.mn) / (s.mx - s.mn)))
                           : (v - s.mn) / (s.mx - s.mn);
    return clampf (p, 0, 1);
}
inline float fromP (const Spec& s, float p)
{
    p = clampf (p, 0, 1);
    float v = s.curve == 1 ? s.mn * std::pow (s.mx / s.mn, p)
            : s.curve == 2 ? s.mn + (s.mx - s.mn) * p * p
                           : s.mn + (s.mx - s.mn) * p;
    if (s.step > 0) v = std::round (v / s.step) * s.step;
    return clampf (v, s.mn, s.mx);
}

inline int paramIdForPath (const juce::var& path)
{
    if (! path.isArray() || path.size() < 2) return -1;
    auto a = path[0].toString();
    if (a == "osc")
    {
        int i = (int) path[1];
        if (i < 0 || i > 2 || path.size() < 3) return -1;
        auto c = path[2].toString();
        int base = i * O_COUNT;
        if (c == "warp" && path.size() >= 4)
        {
            auto k = path[3].toString();
            if (k == "squig") return base + O_SQUIG;
            if (k == "fold")  return base + O_FOLD;
            if (k == "bend")  return base + O_BEND;
            if (k == "left")  return base + O_LEFT;
            if (k == "right") return base + O_RIGHT;
            if (k == "drive") return base + O_DRIVE;
            if (k == "crush") return base + O_CRUSH;
            return -1;
        }
        if (c == "level")  return base + O_LEVEL;
        if (c == "semi")   return base + O_SEMI;
        if (c == "fine")   return base + O_FINE;
        if (c == "spread") return base + O_SPREAD;
        if (c == "pos")    return base + O_POS;
        return -1;
    }
    auto b = path[1].toString();
    if (a == "master") return b == "trans" ? M_TRANS : b == "drive" ? M_DRIVE : b == "vol" ? M_VOL : -1;
    if (a == "plfo")   return b == "amt" ? PL_AMT : b == "rate" ? PL_RATE : -1;
    if (a == "filt")   return b == "cutoff" ? F_CUTOFF : b == "res" ? F_RES : b == "rate" ? F_RATE : b == "lamt" ? F_LAMT : -1;
    if (a == "fx" && path.size() >= 3)
    {
        auto k = path[2].toString();
        if (b == "phaser")  return k == "rate" ? PH_RATE : k == "depth" ? PH_DEPTH : k == "fb" ? PH_FB : k == "metal" ? PH_METAL : k == "mix" ? PH_MIX : -1;
        if (b == "flanger") return k == "rate" ? FL_RATE : k == "depth" ? FL_DEPTH : k == "fb" ? FL_FB : k == "mix" ? FL_MIX : -1;
        if (b == "chorus")  return k == "rate" ? CH_RATE : k == "depth" ? CH_DEPTH : k == "mix" ? CH_MIX : -1;
        if (b == "delay")   return k == "time" ? DL_TIME : k == "fb" ? DL_FB : k == "tone" ? DL_TONE : k == "mix" ? DL_MIX : -1;
        if (b == "reverb")  return k == "decay" ? RV_DECAY : k == "size" ? RV_SIZE : k == "tone" ? RV_TONE : k == "mix" ? RV_MIX : -1;
    }
    return -1;
}

// Tempo divisions in quarter notes (same lists as the UI)
inline float divBeats (int i)
{
    static const float d[] = { 4, 2, 1, 2.0f / 3, 0.5f, 1.0f / 3, 0.25f, 1.0f / 6, 0.125f };
    return d[juce::jlimit (0, 8, i)];
}

//==============================================================================
// Drawing -> waveform (left to right is one cycle; newest ink at each column wins)
inline bool rasterCols (const juce::var& strokes, float* col)
{
    for (int i = 0; i < N; ++i) col[i] = std::numeric_limits<float>::quiet_NaN();
    bool any = false;
    auto put = [&] (float x, float y)
    {
        int i = juce::jlimit (0, N - 1, (int) std::lround (x * (N - 1)));
        col[i] = 0.5f - y;
        any = true;
    };
    if (! strokes.isArray()) return false;
    for (auto& st : *strokes.getArray())
    {
        if (! st.isArray()) continue;
        auto& pts = *st.getArray();
        if (pts.size() == 1) put ((float) pts[0][0], (float) pts[0][1]);
        for (int k = 1; k < pts.size(); ++k)
        {
            float x0 = pts[k - 1][0], y0 = pts[k - 1][1], x1 = pts[k][0], y1 = pts[k][1];
            int i0 = (int) std::lround (x0 * (N - 1)), i1 = (int) std::lround (x1 * (N - 1));
            int n = std::max (1, std::abs (i1 - i0));
            for (int j = 0; j <= n; ++j)
            {
                float f = (float) j / n;
                put (x0 + (x1 - x0) * f, y0 + (y1 - y0) * f);
            }
        }
    }
    if (! any) return false;
    std::vector<int> idx;
    for (int i = 0; i < N; ++i) if (! std::isnan (col[i])) idx.push_back (i);
    for (int i = 0; i < N; ++i)
    {
        if (! std::isnan (col[i])) continue;
        int a = idx.back(), b = idx.front();
        for (int j : idx) { if (j < i) a = j; else { b = j; break; } }
        int da = (i - a + N) % N, db = (b - i + N) % N;
        col[i] = (da + db) ? col[a] + (col[b] - col[a]) * (float) da / (da + db) : col[a];
    }
    return true;
}

inline void normCol (const float* col, float* raw)
{
    float mean = 0;
    for (int i = 0; i < N; ++i) mean += col[i];
    mean /= N;
    float peak = 0;
    for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (col[i] - mean));
    for (int i = 0; i < N; ++i) raw[i] = peak > 1e-4f ? (col[i] - mean) / peak : 0.0f;
}

struct Warp { float squig = 0, fold = 0, bend = 0, left = 0, right = 0, drive = 0, crush = 0; };

inline void applyWarp (const float* raw, const Warp& w, float* out)
{
    const int hold = 1 + (int) std::floor (w.crush * 12);
    for (int i = 0; i < N; ++i)
    {
        int ii = w.crush > 0 ? (i / hold) * hold : i;
        float x = (float) ii / N;
        float xw = w.bend >= 0 ? std::pow (x, 1 + w.bend * 3) : 1 - std::pow (1 - x, 1 - w.bend * 3);
        float p = xw * N;
        int i0 = (int) std::floor (p);
        float f = p - i0;
        float y = raw[i0 % N] * (1 - f) + raw[(i0 + 1) % N] * f;
        y += w.squig * 0.55f * (0.6f * std::sin (TAU * 7 * x + 1.3f) + 0.45f * std::sin (TAU * 19 * x + 0.4f)
                                + 0.3f * std::sin (TAU * 37 * x + 2.1f) + 0.18f * std::sin (TAU * 61 * x));
        float s = std::sin (TAU * x);
        y += (x < 0.5f ? w.left : w.right) * 0.9f * s * s;
        if (w.fold > 0)
        {
            float g = 1 + w.fold * 6, fy = std::sin (y * g * juce::MathConstants<float>::halfPi);
            y += (fy - y) * std::min (1.0f, w.fold * 5);
        }
        if (w.drive > 0) { float k = 1 + w.drive * 14; y = std::tanh (k * y) / std::tanh (k); }
        if (w.crush > 0) { float L = std::max (2.0f, std::round (24 - w.crush * 21)) / 2; y = std::round (y * L) / L; }
        out[i] = y;
    }
    float m = 0;
    for (int i = 0; i < N; ++i) m += out[i];
    m /= N;
    float pk = 1e-6f;
    for (int i = 0; i < N; ++i) { out[i] -= m; pk = std::max (pk, std::abs (out[i])); }
    for (int i = 0; i < N; ++i) out[i] /= pk;
}

//==============================================================================
// Everything parsed from the UI state, built on the message thread.
struct Params
{
    float v[NP] {};
    struct Osc
    {
        bool on = false, sweep = true, hasTable = false;
        int uni = 1;
        std::array<std::array<float, N>, NF> base {};
    } osc[3];
    float ampA = 0.004f, ampD = 0.35f, ampS = 0.9f, ampR = 0.2f;
    int plShape = 0;
    int fType = 0; // 0 LP 1 BP 2 HP 3 notch
    bool fEnv = false, fSync = true;
    int fShape = 0, fDiv = 4;
    float fEa = 0.005f, fEd = 0.35f, fEs = 0.25f, fEr = 0.25f, fEamt = 3;
    bool fxOn[5] {};
    bool mono = true;
    float glide = 0.05f;
    struct Mod { bool lfo = true; float curve[128] {}; float len = 0.6f; bool sync = true; int div = 2; float rate = 2; bool retrig = true; } mods[3];
    struct Route { int src, id; float depth; };
    std::vector<Route> routes;
};

inline int shapeIndex (const juce::String& s)
{
    return s == "triangle" ? 1 : s == "square" ? 2 : s == "sawtooth" ? 3 : 0;
}

inline std::unique_ptr<Params> parseState (const juce::var& S)
{
    auto P = std::make_unique<Params>();
    auto num = [] (const juce::var& o, const char* k, float def) { auto v = o.getProperty (k, juce::var()); return v.isVoid() ? def : (float) (double) v; };
    auto flag = [] (const juce::var& o, const char* k, bool def) { auto v = o.getProperty (k, juce::var()); return v.isVoid() ? def : (bool) v; };

    auto oscs = S["osc"];
    for (int i = 0; i < 3; ++i)
    {
        auto o = oscs[i];
        auto& Po = P->osc[i];
        Po.on = flag (o, "on", i == 0);
        Po.sweep = flag (o, "sweep", true);
        Po.uni = juce::jlimit (1, MAXU, (int) num (o, "uni", 1));
        float* v = P->v + i * O_COUNT;
        v[O_LEVEL] = num (o, "level", 0.8f);
        v[O_SEMI] = num (o, "semi", 0);
        v[O_FINE] = num (o, "fine", 0);
        v[O_SPREAD] = num (o, "spread", 0.25f);
        v[O_POS] = num (o, "pos", 1);
        auto w = o["warp"];
        v[O_SQUIG] = num (w, "squig", 0); v[O_FOLD] = num (w, "fold", 0); v[O_BEND] = num (w, "bend", 0);
        v[O_LEFT] = num (w, "left", 0); v[O_RIGHT] = num (w, "right", 0); v[O_DRIVE] = num (w, "drive", 0); v[O_CRUSH] = num (w, "crush", 0);

        // keyframes: frame 0 = "strokes", frames 1..15 = "fk"
        std::array<std::array<float, N>, NF> cols {};
        std::array<bool, NF> has {};
        auto fk = o["fk"];
        for (int k = 0; k < NF; ++k)
        {
            juce::var st = k == 0 ? o["strokes"] : (fk.isArray() && k < fk.size() ? fk[k] : juce::var());
            has[(size_t) k] = st.isArray() && st.size() > 0 && rasterCols (st, cols[(size_t) k].data());
        }
        std::vector<int> keys;
        for (int k = 0; k < NF; ++k) if (has[(size_t) k]) keys.push_back (k);
        Po.hasTable = ! keys.empty();
        if (Po.hasTable)
        {
            for (int k = 0; k < NF; ++k)
            {
                if (! has[(size_t) k])
                {
                    int a = -1, b = -1;
                    for (int q : keys) { if (q < k) a = q; else { b = q; break; } }
                    if (a < 0) cols[(size_t) k] = cols[(size_t) b];
                    else if (b < 0) cols[(size_t) k] = cols[(size_t) a];
                    else
                    {
                        float f = (float) (k - a) / (b - a);
                        for (int n = 0; n < N; ++n) cols[(size_t) k][(size_t) n] = cols[(size_t) a][(size_t) n] + (cols[(size_t) b][(size_t) n] - cols[(size_t) a][(size_t) n]) * f;
                    }
                }
                normCol (cols[(size_t) k].data(), Po.base[(size_t) k].data());
            }
        }
    }

    auto amp = S["amp"];
    P->ampA = num (amp, "a", 0.004f); P->ampD = num (amp, "d", 0.35f); P->ampS = num (amp, "s", 0.9f); P->ampR = num (amp, "r", 0.2f);
    auto pl = S["plfo"];
    P->v[PL_AMT] = num (pl, "amt", 0); P->v[PL_RATE] = num (pl, "rate", 5.5f); P->plShape = shapeIndex (pl["shape"].toString());
    auto f = S["filt"];
    auto ft = f["type"].toString();
    P->fType = ft == "bandpass" ? 1 : ft == "highpass" ? 2 : ft == "notch" ? 3 : 0;
    P->v[F_CUTOFF] = num (f, "cutoff", 7000); P->v[F_RES] = num (f, "res", 0.2f); P->v[F_RATE] = num (f, "rate", 2); P->v[F_LAMT] = num (f, "lamt", 0);
    P->fEnv = f["mode"].toString() == "env";
    P->fShape = shapeIndex (f["lshape"].toString());
    P->fSync = flag (f, "sync", true);
    P->fDiv = (int) num (f, "div", 4);
    P->fEa = num (f, "ea", 0.005f); P->fEd = num (f, "ed", 0.35f); P->fEs = num (f, "es", 0.25f); P->fEr = num (f, "er", 0.25f); P->fEamt = num (f, "eamt", 3);

    auto fx = S["fx"];
    auto ph = fx["phaser"], fl = fx["flanger"], ch = fx["chorus"], dl = fx["delay"], rv = fx["reverb"];
    P->fxOn[0] = flag (ph, "on", false); P->fxOn[1] = flag (fl, "on", false); P->fxOn[2] = flag (ch, "on", false);
    P->fxOn[3] = flag (dl, "on", false); P->fxOn[4] = flag (rv, "on", false);
    P->v[PH_RATE] = num (ph, "rate", 0.3f); P->v[PH_DEPTH] = num (ph, "depth", 0.6f); P->v[PH_FB] = num (ph, "fb", 0.65f); P->v[PH_METAL] = num (ph, "metal", 0.45f); P->v[PH_MIX] = num (ph, "mix", 0.5f);
    P->v[FL_RATE] = num (fl, "rate", 0.2f); P->v[FL_DEPTH] = num (fl, "depth", 0.6f); P->v[FL_FB] = num (fl, "fb", 0.75f); P->v[FL_MIX] = num (fl, "mix", 0.5f);
    P->v[CH_RATE] = num (ch, "rate", 0.8f); P->v[CH_DEPTH] = num (ch, "depth", 0.5f); P->v[CH_MIX] = num (ch, "mix", 0.4f);
    P->v[DL_TIME] = num (dl, "time", 0.33f); P->v[DL_FB] = num (dl, "fb", 0.35f); P->v[DL_TONE] = num (dl, "tone", 0.55f); P->v[DL_MIX] = num (dl, "mix", 0.22f);
    P->v[RV_DECAY] = num (rv, "decay", 1.8f); P->v[RV_SIZE] = num (rv, "size", 0.2f); P->v[RV_TONE] = num (rv, "tone", 0.6f); P->v[RV_MIX] = num (rv, "mix", 0.14f);

    auto m = S["master"];
    P->v[M_VOL] = num (m, "vol", 0.7f); P->v[M_DRIVE] = num (m, "drive", 0.12f); P->v[M_TRANS] = num (m, "trans", 0);
    P->mono = flag (m, "mono", true); P->glide = num (m, "glide", 0.05f);

    auto mods = S["mods"];
    for (int i = 0; i < 3; ++i)
    {
        auto md = mods[i];
        auto& M = P->mods[i];
        M.lfo = md["mode"].toString() != "env";
        M.len = num (md, "len", 0.6f); M.sync = flag (md, "sync", true); M.div = (int) num (md, "div", 2);
        M.rate = num (md, "rate", 2); M.retrig = flag (md, "retrig", true);
        auto c = md["curve"];
        for (int j = 0; j < 128; ++j) M.curve[j] = c.isArray() && j < c.size() ? clampf ((float) (double) c[j], 0, 1) : 1.0f - std::abs (2.0f * j / 127 - 1);
    }
    auto routes = S["routes"];
    if (routes.isArray())
        for (auto& r : *routes.getArray())
        {
            int id = paramIdForPath (r["path"]);
            int src = (int) r["src"];
            if (id >= 0 && src >= 0 && src < 3) P->routes.push_back ({ src, id, (float) (double) r["depth"] });
        }
    return P;
}

//==============================================================================
inline float lfoShape (int shape, float ph)
{
    switch (shape)
    {
        case 1: return ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;
        case 2: return ph < 0.5f ? 1.0f : -1.0f;
        case 3: return 2 * ph - 1;
        default: return std::sin (TAU * ph);
    }
}

// Small radix-2 inverse FFT (no scaling) used to build band-limited tables
struct IFFT
{
    int n, logn;
    std::vector<std::complex<float>> tw;
    explicit IFFT (int size) : n (size), logn (0)
    {
        while ((1 << logn) < n) ++logn;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar (1.0f, TAU * i / n);
    }
    void run (std::complex<float>* a) const
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    auto u = a[i + k], v = a[i + k + len / 2] * tw[(size_t) (k * step)];
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                }
        }
    }
};

// One oscillator's current playable table (band-limited mip levels)
struct OscTables
{
    float mip[MIPS][TL + 1] {};
    bool valid = false;
    // cache of what's currently built
    float lastPos = -1, lastW[7] { -9, -9, -9, -9, -9, -9, -9 };
    const Params::Osc* lastBase = nullptr;
};

inline int mipForFreq (float f, double sr)
{
    float allowed = (float) (sr * 0.5) / std::max (1.0f, f);
    if (allowed >= NH) return 0;
    int L = (int) std::ceil (std::log2 (128.0f / std::max (1.0f, allowed)));
    return juce::jlimit (0, MIPS - 1, L);
}

//==============================================================================
struct SVF
{
    float ic1 = 0, ic2 = 0;
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    int type = 0;
    void set (float fc, float Q, int t, double sr)
    {
        fc = clampf (fc, 20, (float) sr * 0.45f);
        g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
        k = 1.0f / std::max (0.3f, Q);
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        type = t;
    }
    float process (float v0)
    {
        float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        switch (type)
        {
            case 1: return v1 * k;        // bandpass (normalised peak)
            case 2: return v0 - k * v1 - v2; // highpass
            case 3: return v0 - k * v1;      // notch
            default: return v2;            // lowpass
        }
    }
    void reset() { ic1 = ic2 = 0; }
};

struct Voice
{
    bool active = false, released = false;
    int note = 0;
    float vel = 1;
    uint64_t age = 0;
    float logF = 0, logFTarget = 0;     // pitch in semitones (note number)
    // amp envelope
    float env = 0; int envStage = 0;    // 0 attack, 1 decay/sustain, 2 release
    // filter envelope
    float fenv = 0; int fenvStage = 0;
    float flfoPh = 0;
    float ph[3][MAXU] {};
    SVF filt[2];
};

//==============================================================================
struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void init (int size) { buf.assign ((size_t) size, 0.0f); w = 0; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) % (int) buf.size(); }
    float read (float delaySamples) const
    {
        int sz = (int) buf.size();
        float rp = (float) w - 1 - delaySamples;
        while (rp < 0) rp += (float) sz;
        int i0 = (int) rp;
        float f = rp - i0;
        return buf[(size_t) (i0 % sz)] * (1 - f) + buf[(size_t) ((i0 + 1) % sz)] * f;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void allpass (float f, float Q, double sr)
    {
        float w = TAU * clampf (f, 20, (float) sr * 0.45f) / (float) sr, cs = std::cos (w), al = std::sin (w) / (2 * Q), a0 = 1 + al;
        b0 = (1 - al) / a0; b1 = -2 * cs / a0; b2 = (1 + al) / a0; a1 = -2 * cs / a0; a2 = (1 - al) / a0;
    }
    float process (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void set (float f, double sr) { a = std::exp (-TAU * clampf (f, 10, (float) sr * 0.45f) / (float) sr); }
    float process (float x) { z = x * (1 - a) + z * a; return z; }
};

//==============================================================================
class Engine
{
public:
    Engine() : ifft (TL) { cur = std::make_unique<Params>(); specBuf.resize ((size_t) TL); }

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (auto& v : voices) { v = Voice(); }
        for (int c = 0; c < 2; ++c)
        {
            phFb[c].init ((int) (sr * 0.05) + 4);
            flDel[c].init ((int) (sr * 0.05) + 4);
            dlDel[c].init ((int) (sr * 2.0) + 4);
            rvPre[c].init ((int) (sr * 0.2) + 4);
            for (auto& b : phAp[c]) b.reset();
        }
        chDel.init ((int) (sr * 0.06) + 4);
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
        reverb.prepare (spec);
        reverb.reset();
        comp.prepare (spec);
        comp.setThreshold (-8.0f); comp.setRatio (12.0f); comp.setAttack (3.0f); comp.setRelease (200.0f);
        wet.setSize (2, maxBlock);
        lastIRDecay = -1;
        scopeW = 0;
    }

    // Called from the message thread with a freshly parsed state.
    void setPending (std::unique_ptr<Params> p)
    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);
        pending = std::move (p);
    }
    // Called from the message thread: frees whatever the audio thread retired.
    void collectGarbage()
    {
        const juce::SpinLock::ScopedLockType sl (pendingLock);
        retired.reset();
    }

    // Reverb tail is rebuilt off the audio thread when its decay changes.
    void updateReverbIR (float decay)
    {
        if (std::abs (decay - lastIRDecay) < 0.01f) return;
        lastIRDecay = decay;
        double rate = sr > 0 ? sr : 44100.0;
        int len = std::max (64, (int) (rate * decay));
        juce::AudioBuffer<float> ir (2, len);
        juce::Random rnd;
        for (int c = 0; c < 2; ++c)
        {
            auto* d = ir.getWritePointer (c);
            for (int i = 0; i < len; ++i) d[i] = (rnd.nextFloat() * 2 - 1) * std::pow (1.0f - (float) i / len, 3.2f);
        }
        reverb.loadImpulseResponse (std::move (ir), rate, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
    }

    void noteOn (int note, float vel)
    {
        const auto& P = *cur;
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        held.push_back (note);
        if (P.mono)
        {
            Voice* mv = nullptr;
            for (auto& v : voices) if (v.active && ! v.released) { mv = &v; break; }
            if (mv) { mv->note = note; mv->logFTarget = (float) note; return; } // legato glide
            startVoice (note, vel, true);
        }
        else
        {
            for (auto& v : voices) if (v.active && ! v.released && v.note == note) release (v, true);
            startVoice (note, vel, false);
        }
    }

    void noteOff (int note)
    {
        const auto& P = *cur;
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (P.mono)
        {
            for (auto& v : voices)
                if (v.active && ! v.released)
                {
                    if (! held.empty()) { if (v.note == note) { v.note = held.back(); v.logFTarget = (float) held.back(); } }
                    else release (v, false);
                }
        }
        else
            for (auto& v : voices) if (v.active && ! v.released && v.note == note) release (v, false);
    }

    void allNotesOff()
    {
        held.clear();
        for (auto& v : voices) if (v.active) release (v, true);
    }

    void setPitchBend (float semis) { bend = semis; }

    void setTransport (double bpm, double ppq, bool playing)
    {
        hostBpm = bpm > 1 ? bpm : 120.0;
        hostPpq = ppq;
        hostPlaying = playing;
    }

    void render (float* L, float* R, int n)
    {
        swapIn();
        for (int off = 0; off < n; off += CHUNK)
        {
            int len = std::min (CHUNK, n - off);
            control (len);
            renderChunk (L + off, R + off, len);
            hostPpq += len / sr * hostBpm / 60.0;
        }
    }

    // After render(): reverb + compressor over the whole block
    void postProcess (juce::AudioBuffer<float>& buffer, int n)
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);
        if (cur->fxOn[4] || reverbTail > 0)
        {
            for (int c = 0; c < 2; ++c) wet.copyFrom (c, 0, buffer, c, 0, n);
            // pre-delay
            float pre = eff[RV_SIZE] * 0.12f * (float) sr;
            for (int c = 0; c < 2; ++c)
            {
                auto* w = wet.getWritePointer (c);
                for (int i = 0; i < n; ++i) { rvPre[c].push (w[i]); w[i] = rvPre[c].read (pre); }
            }
            juce::dsp::AudioBlock<float> wb (wet.getArrayOfWritePointers(), 2, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (wb);
            reverb.process (ctx);
            rvTone[0].set (700 * std::pow (2.0f, eff[RV_TONE] * 5), sr);
            rvTone[1].a = rvTone[0].a;
            float g = cur->fxOn[4] ? eff[RV_MIX] * 1.3f : 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                auto* w = wet.getReadPointer (c);
                auto* o = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i) o[i] += rvTone[c].process (w[i]) * g;
            }
            reverbTail = cur->fxOn[4] ? (int) (sr * 10) : std::max (0, reverbTail - n);
        }
        // master volume then the safety compressor
        float vol = eff[M_VOL] * eff[M_VOL] * 1.6f;
        buffer.applyGain (0, n, vol);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        comp.process (ctx);
        for (int c = 0; c < 2; ++c)
        {
            auto* o = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i) o[i] = clampf (o[i], -1.0f, 1.0f);
        }
    }

    // ---- visual feedback for the UI ----
    float modPos[3] {}, modVal[3] {};
    std::atomic<bool> playingFlag { false };
    std::atomic<double> uiBpm { 120.0 };
    float scope[2048] {};
    int scopeW = 0;
    const Params& params() const { return *cur; }

private:
    double sr = 44100;
    std::unique_ptr<Params> cur, pending, retired;
    juce::SpinLock pendingLock;
    IFFT ifft;
    std::vector<std::complex<float>> specBuf;
    OscTables tabs[3];
    Voice voices[MAXV];
    uint64_t ageCounter = 0;
    std::vector<int> held;
    float eff[NP] {};
    float bend = 0;
    double hostBpm = 120, hostPpq = 0;
    bool hostPlaying = false;
    float modT[3] {}, modPh[3] {};
    float vibPh = 0;
    int tableCounter = 0;
    // fx state
    DelayLine phFb[2], flDel[2], dlDel[2], rvPre[2], chDel;
    Biquad phAp[2][8];
    float phLfo = 0, flLfo = 0, chLfo1 = 0, chLfo2 = 0;
    float phLast[2] {}, flLast[2] {};
    OnePoleLP dlLp[2], rvTone[2];
    juce::dsp::Convolution reverb;
    juce::dsp::Compressor<float> comp;
    juce::AudioBuffer<float> wet;
    float lastIRDecay = -1;
    int reverbTail = 0;

    void swapIn()
    {
        const juce::SpinLock::ScopedTryLockType sl (pendingLock);
        if (sl.isLocked() && pending)
        {
            retired = std::move (cur);
            cur = std::move (pending);
            for (auto& t : tabs) t.lastBase = nullptr; // force table rebuild
        }
    }

    void startVoice (int note, float vel, bool mono)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (! v)
        {
            Voice* oldest = &voices[0];
            for (auto& x : voices) if (x.age < oldest->age) oldest = &x;
            v = oldest;
        }
        juce::ignoreUnused (mono);
        *v = Voice();
        v->active = true; v->note = note; v->vel = vel; v->age = ++ageCounter;
        v->logF = v->logFTarget = (float) note;
        for (int i = 0; i < 3; ++i)
            for (int u = 0; u < MAXU; ++u) v->ph[i][u] = 0;
        // retrigger mods on a fresh note
        for (int i = 0; i < 3; ++i) { modT[i] = 0; if (cur->mods[i].retrig) modPh[i] = 0; }
    }

    void release (Voice& v, bool fast)
    {
        v.released = true;
        v.envStage = fast ? 3 : 2;
        v.fenvStage = 2;
    }

    float modHz (const Params::Mod& m) const { return m.sync ? (float) (hostBpm / 60.0 / divBeats (m.div)) : m.rate; }

    void control (int len)
    {
        const auto& P = *cur;
        const float dt = (float) (len / sr);
        bool anyActive = false;
        for (auto& v : voices) anyActive |= v.active;
        playingFlag = anyActive;
        uiBpm = hostBpm;

        // mods
        for (int i = 0; i < 3; ++i)
        {
            const auto& m = P.mods[i];
            float x;
            if (! m.lfo) { modT[i] += dt; x = clampf (modT[i] / std::max (0.01f, m.len), 0, 1); }
            else
            {
                if (m.sync && ! m.retrig && hostPlaying)
                    modPh[i] = (float) std::fmod (hostPpq / divBeats (m.div), 1.0);
                else
                    modPh[i] = std::fmod (modPh[i] + dt * modHz (m), 1.0f);
                x = modPh[i];
            }
            float p = x * 127, fi = std::floor (p);
            int i0 = (int) fi, i1 = std::min (127, i0 + 1);
            modPos[i] = x;
            modVal[i] = m.curve[i0] + (m.curve[i1] - m.curve[i0]) * (p - fi);
        }
        // effective parameters
        std::copy (P.v, P.v + NP, eff);
        if (! P.routes.empty())
        {
            float acc[NP]; bool touched[NP] {};
            for (auto& r : P.routes)
            {
                if (! touched[r.id]) { acc[r.id] = toP (specOf (r.id), P.v[r.id]); touched[r.id] = true; }
                acc[r.id] += r.depth * modVal[r.src];
            }
            for (int id = 0; id < NP; ++id) if (touched[id]) eff[id] = fromP (specOf (id), acc[id]);
        }
        // wavetables (every 256 samples, or immediately if stale)
        if (++tableCounter >= 256 / CHUNK || tabs[0].lastBase == nullptr || tabs[1].lastBase == nullptr || tabs[2].lastBase == nullptr)
        {
            tableCounter = 0;
            for (int i = 0; i < 3; ++i) updateTable (i);
        }
        vibPh = std::fmod (vibPh + dt * eff[PL_RATE], 1.0f);
    }

    void updateTable (int i)
    {
        const auto& Po = cur->osc[i];
        auto& T = tabs[i];
        const float* e = eff + i * O_COUNT;
        float w7[7] = { e[O_SQUIG], e[O_FOLD], e[O_BEND], e[O_LEFT], e[O_RIGHT], e[O_DRIVE], e[O_CRUSH] };
        float pos = clampf (e[O_POS], 0, 1);
        bool same = T.lastBase == &Po && std::abs (pos - T.lastPos) < 1e-4f;
        for (int k = 0; k < 7 && same; ++k) same = std::abs (w7[k] - T.lastW[k]) < 1e-4f;
        if (same) return;
        T.lastBase = &Po; T.lastPos = pos;
        std::copy (w7, w7 + 7, T.lastW);
        if (! Po.hasTable) { T.valid = false; return; }

        float kf = pos * (NF - 1);
        int a = (int) std::floor (kf), b = std::min (NF - 1, a + 1);
        float fr = kf - a;
        auto warpFor = [&] (int k)
        {
            float t = Po.sweep ? (float) k / (NF - 1) : 1.0f;
            Warp w { w7[0] * t, w7[1] * t, w7[2] * t, w7[3] * t, w7[4] * t, w7[5] * t, w7[6] * t };
            return w;
        };
        float fa[N], fb[N], t[N];
        applyWarp (Po.base[(size_t) a].data(), warpFor (a), fa);
        if (b != a && fr > 1e-4f) applyWarp (Po.base[(size_t) b].data(), warpFor (b), fb);
        else std::copy (fa, fa + N, fb);
        float pk = 1e-6f;
        for (int n = 0; n < N; ++n) { t[n] = fa[n] * (1 - fr) + fb[n] * fr; pk = std::max (pk, std::abs (t[n])); }
        for (int n = 0; n < N; ++n) t[n] /= pk;

        // harmonics of the 256-sample cycle
        static float cosT[N], sinT[N];
        static bool init = false;
        if (! init) { for (int n = 0; n < N; ++n) { cosT[n] = std::cos (TAU * n / N); sinT[n] = std::sin (TAU * n / N); } init = true; }
        float ak[NH + 1] {}, bk[NH + 1] {};
        for (int k = 1; k <= NH; ++k)
        {
            float sa = 0, sb = 0;
            for (int n = 0; n < N; ++n) { int j = (k * n) % N; sa += t[n] * cosT[j]; sb += t[n] * sinT[j]; }
            ak[k] = 2 * sa / N; bk[k] = 2 * sb / N;
        }
        auto& spec = specBuf;
        float scale = 1;
        for (int L = 0; L < MIPS; ++L)
        {
            int maxH = std::min (NH, 128 >> L);
            std::fill (spec.begin(), spec.end(), std::complex<float> (0, 0));
            for (int k = 1; k <= maxH; ++k)
            {
                std::complex<float> X (ak[k] * 0.5f, -bk[k] * 0.5f);
                spec[(size_t) k] = X;
                spec[(size_t) (TL - k)] = std::conj (X);
            }
            ifft.run (spec.data());
            if (L == 0)
            {
                float m = 1e-6f;
                for (int n = 0; n < TL; ++n) m = std::max (m, std::abs (spec[(size_t) n].real()));
                scale = 1.0f / m;
            }
            for (int n = 0; n < TL; ++n) T.mip[L][n] = spec[(size_t) n].real() * scale;
            T.mip[L][TL] = T.mip[L][0];
        }
        T.valid = true;
    }

    void renderChunk (float* L, float* R, int len)
    {
        const auto& P = *cur;
        float mixL[CHUNK] {}, mixR[CHUNK] {};
        const float vibCents = eff[PL_AMT] * lfoShape (P.plShape, vibPh);
        const float lfoHz = P.fSync ? (float) (hostBpm / 60.0 / divBeats (P.fDiv)) : eff[F_RATE];
        const float dt = (float) (len / sr);

        for (auto& v : voices)
        {
            if (! v.active) continue;
            // glide
            float gl = std::max (0.004f, P.glide);
            v.logF += (v.logFTarget - v.logF) * std::min (1.0f, dt / (gl / 3.0f));
            // filter cutoff for this chunk
            v.flfoPh = std::fmod (v.flfoPh + dt * lfoHz, 1.0f);
            float oct = P.fEnv ? v.fenv * P.fEamt : lfoShape (P.fShape, v.flfoPh) * eff[F_LAMT];
            float fc = eff[F_CUTOFF] * std::pow (2.0f, oct);
            float Q = (P.fType == 0 || P.fType == 2) ? std::pow (10.0f, eff[F_RES] * 24 / 20) : 0.5f + eff[F_RES] * 14;
            for (auto& f : v.filt) f.set (fc, Q, P.fType, sr);

            // per-oscillator unison setup
            struct U { float inc, gl, gr; const float* tab; };
            U us[3][MAXU]; int nu[3] {};
            for (int i = 0; i < 3; ++i)
            {
                const auto& Po = P.osc[i];
                if (! Po.on || ! tabs[i].valid) continue;
                const float* e = eff + i * O_COUNT;
                int n = Po.uni;
                float g = e[O_LEVEL] / std::sqrt ((float) n);
                for (int u = 0; u < n; ++u)
                {
                    float off = n > 1 ? (float) u / (n - 1) * 2 - 1 : 0;
                    float cents = e[O_SEMI] * 100 + e[O_FINE] + off * e[O_SPREAD] * 60 + vibCents + bend * 100;
                    float f = 440.0f * std::pow (2.0f, (v.logF + eff[M_TRANS] - 69) / 12.0f + cents / 1200.0f);
                    float pan = off * 0.8f;
                    float gl2 = n > 1 ? std::cos ((pan + 1) * juce::MathConstants<float>::pi / 4) * 1.41421f : 1.0f;
                    float gr2 = n > 1 ? std::sin ((pan + 1) * juce::MathConstants<float>::pi / 4) * 1.41421f : 1.0f;
                    us[i][u] = { f / (float) sr, g * gl2, g * gr2, tabs[i].mip[mipForFreq (f, sr)] };
                }
                nu[i] = n;
            }

            const float aStep = 1.0f / std::max (1.0f, P.ampA * (float) sr);
            const float dCoef = 1 - std::exp (-1.0f / std::max (1.0f, P.ampD / 3 * (float) sr));
            const float rCoef = 1 - std::exp (-1.0f / std::max (1.0f, (v.envStage == 3 ? 0.015f : P.ampR) / 4 * (float) sr));
            const float faStep = 1.0f / std::max (1.0f, P.fEa * (float) sr);
            const float fdCoef = 1 - std::exp (-1.0f / std::max (1.0f, P.fEd / 3 * (float) sr));
            const float frCoef = 1 - std::exp (-1.0f / std::max (1.0f, P.fEr / 3 * (float) sr));

            for (int s = 0; s < len; ++s)
            {
                // envelopes
                if (v.envStage == 0) { v.env += aStep * v.vel; if (v.env >= v.vel) { v.env = v.vel; v.envStage = 1; } }
                else if (v.envStage == 1) v.env += (P.ampS * v.vel - v.env) * dCoef;
                else v.env -= v.env * rCoef;
                if (v.fenvStage == 0) { v.fenv += faStep; if (v.fenv >= 1) { v.fenv = 1; v.fenvStage = 1; } }
                else if (v.fenvStage == 1) v.fenv += (P.fEs - v.fenv) * fdCoef;
                else v.fenv -= v.fenv * frCoef;

                float sl = 0, sR = 0;
                for (int i = 0; i < 3; ++i)
                    for (int u = 0; u < nu[i]; ++u)
                    {
                        auto& U0 = us[i][u];
                        float& ph = v.ph[i][u];
                        float p = ph * TL;
                        int i0 = (int) p;
                        float fr = p - i0;
                        float x = U0.tab[i0] + (U0.tab[i0 + 1] - U0.tab[i0]) * fr;
                        sl += x * U0.gl; sR += x * U0.gr;
                        ph += U0.inc;
                        if (ph >= 1) ph -= 1;
                    }
                float outL = v.filt[0].process (sl) * v.env;
                float outR = v.filt[1].process (sR) * v.env;
                mixL[s] += outL; mixR[s] += outR;
            }
            if (v.released && v.env < 1e-4f) { v.active = false; v.filt[0].reset(); v.filt[1].reset(); }
        }

        // ---- master drive (same curve as the browser version) ----
        const float k = 0.5f + eff[M_DRIVE] * 14, tk = std::tanh (k), post = 0.9f / (1 + eff[M_DRIVE] * 0.5f);
        for (int s = 0; s < len; ++s)
        {
            mixL[s] = std::tanh (k * clampf (mixL[s] * 0.4f, -1, 1)) / tk * post;
            mixR[s] = std::tanh (k * clampf (mixR[s] * 0.4f, -1, 1)) / tk * post;
            scope[scopeW] = 0.5f * (mixL[s] + mixR[s]);
            scopeW = (scopeW + 1) & 2047;
        }
        fxChunk (mixL, mixR, len);
        for (int s = 0; s < len; ++s) { L[s] = mixL[s]; R[s] = mixR[s]; }
    }

    void fxChunk (float* L, float* R, int len)
    {
        const auto& P = *cur;
        const float dt = (float) (len / sr);
        auto xfade = [] (float m, float& dry, float& wt) { dry = std::cos (m * juce::MathConstants<float>::halfPi); wt = std::sin (m * juce::MathConstants<float>::halfPi); };

        // Metal phaser: 8 allpass stages fed back through a short comb delay
        if (P.fxOn[0])
        {
            phLfo = std::fmod (phLfo + dt * (0.02f + eff[PH_RATE] * eff[PH_RATE] * 8), 1.0f);
            float lfo = lfoShape (1, phLfo);
            static const float bases[8] = { 180, 320, 560, 980, 1700, 2900, 4800, 7600 };
            float Q = 0.7f + eff[PH_METAL] * 7, mult = std::pow (2.0f, lfo * eff[PH_DEPTH] * 3000 / 1200);
            for (int c = 0; c < 2; ++c) for (int j = 0; j < 8; ++j) phAp[c][j].allpass (bases[j] * mult, Q, sr);
            float fb = eff[PH_FB] * 0.93f, dly = (0.003f + eff[PH_METAL] * 0.012f) * (float) sr, dry, wt;
            xfade (eff[PH_MIX], dry, wt);
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < len; ++s)
                {
                    float in = io[c][s] + phFb[c].read (dly) * fb;
                    float y = in;
                    for (auto& ap : phAp[c]) y = ap.process (y);
                    phFb[c].push (y);
                    io[c][s] = io[c][s] * dry + y * wt * 0.75f;
                }
        }
        // Jet flanger
        if (P.fxOn[1])
        {
            float rate = 0.02f + eff[FL_RATE] * eff[FL_RATE] * 5, dry, wt;
            xfade (eff[FL_MIX], dry, wt);
            float fb = eff[FL_FB] * 0.95f, base = 0.004f + eff[FL_DEPTH] * 0.004f, depth = eff[FL_DEPTH] * 0.0034f;
            float* io[2] = { L, R };
            for (int s = 0; s < len; ++s)
            {
                flLfo = std::fmod (flLfo + rate / (float) sr, 1.0f);
                float d = (base + depth * std::sin (TAU * flLfo)) * (float) sr;
                for (int c = 0; c < 2; ++c)
                {
                    float y = flDel[c].read (d);
                    flDel[c].push (io[c][s] + y * fb);
                    io[c][s] = io[c][s] * dry + y * wt * 0.8f;
                }
            }
        }
        // Chorus: two swept voices panned wide
        if (P.fxOn[2])
        {
            float r1 = eff[CH_RATE], r2 = eff[CH_RATE] * 1.13f, depth = eff[CH_DEPTH] * 0.006f, dry, wt;
            xfade (eff[CH_MIX], dry, wt);
            const float pl = std::cos ((-0.75f + 1) * juce::MathConstants<float>::pi / 4), pr = std::sin ((-0.75f + 1) * juce::MathConstants<float>::pi / 4);
            for (int s = 0; s < len; ++s)
            {
                chLfo1 = std::fmod (chLfo1 + r1 / (float) sr, 1.0f);
                chLfo2 = std::fmod (chLfo2 + r2 / (float) sr, 1.0f);
                chDel.push (0.5f * (L[s] + R[s]));
                float a = chDel.read ((0.018f + depth * std::sin (TAU * chLfo1)) * (float) sr);
                float b = chDel.read ((0.018f + depth * std::sin (TAU * chLfo2)) * (float) sr);
                float wl = a * pl + b * pr, wr = a * pr + b * pl;
                L[s] = L[s] * dry + wl * wt;
                R[s] = R[s] * dry + wr * wt;
            }
        }
        // Delay with filtered feedback
        if (P.fxOn[3])
        {
            float d = eff[DL_TIME] * (float) sr, fb = eff[DL_FB];
            for (auto& lp : dlLp) lp.set (600 * std::pow (2.0f, eff[DL_TONE] * 5), sr);
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < len; ++s)
                {
                    float y = dlLp[c].process (dlDel[c].read (d));
                    dlDel[c].push (io[c][s] + y * fb);
                    io[c][s] += y * eff[DL_MIX];
                }
        }
    }
};
} // namespace ds
