#include "Creature.h"

#include <cstddef>
#include <cstring>

namespace dendrite
{
// The saved life has no hidden padding, so its bytes (and checksum) are the same on every compiler.
static_assert (sizeof (Creature::Life) == 5544 && sizeof (Creature::Life::Entry) == 72);

namespace
{
    // splitmix64: one well-mixed 64-bit number per call, identical on every machine
    uint64_t mix (uint64_t z) noexcept
    {
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    struct Genes
    {
        uint64_t state;
        uint64_t next() noexcept { return state = mix (state); }
        float unit() noexcept { return (float) (next() >> 40) * (1.0f / 16777216.0f); }
        // Irwin-Hall: twelve uniforms make a bell, with integer-exact inputs on every platform
        float normal() noexcept
        {
            float a = 0.0f;
            for (int i = 0; i < 12; ++i)
                a += unit();
            return a - 6.0f;
        }
    };

    int bitsSet (uint64_t x) noexcept
    {
        int n = 0;
        while (x != 0)
        {
            x &= x - 1;
            ++n;
        }
        return n;
    }

    // the three body rhythms, in sixteenths: golden-ratio spaced, so they never line up
    constexpr float rhythmPeriod[Creature::rhythms] = { 600.0f, 970.82f, 1570.82f };

    constexpr float gain = 0.9f;           // the mind's recurrent gain: stable, never locks
    constexpr float tauShort = 2.0f, tauLong = 48.0f;
    constexpr float antiHabit = 0.15f;     // how hard it leans away from what it did lately
    constexpr float instinct = 0.9f;       // how strongly the music pulls register and pace
    constexpr float tiredness = 0.6f;
    constexpr int maxHamming = 12;         // two bars with prints this close are the same loop
} // namespace

//==============================================================================
Gesture Gesture::dice (uint64_t seed, int lastSetMask) noexcept
{
    Dice d;
    d.reseed (seed);
    Gesture g;
    for (auto& v : g.u)
        v = d.unit();
    std::array<int, 5> order { 0, 1, 2, 3, 4 };
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        for (int i = 4; i > 0; --i)
            std::swap (order[(size_t) i], order[(size_t) std::min (i, (int) (d.unit() * (float) (i + 1)))]);
        if (((1 << order[0]) | (1 << order[1]) | (1 << order[2])) != lastSetMask)
            break;
    }
    g.order = { order[0], order[1], order[2] };
    g.texture = seed ^ 0x5BD1E9955BD1E995ull;
    return g;
}

//==============================================================================
uint32_t Creature::Life::sum() const noexcept
{
    const auto* p = reinterpret_cast<const unsigned char*> (this);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof (Life, checksum); ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

bool Creature::Life::valid() const noexcept
{
    if (magic != magicWord || version != currentVersion || size != sizeof (Life) || checksum != sum())
        return false;
    const auto finite = [] (const float* v, int n, float limit)
    {
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (v[i]) || std::abs (v[i]) > limit)
                return false;
        return true;
    };
    if (! finite (y, neurons, 6.5f) || ! finite (&rhythm[0][0], 2 * rhythms, 1.5f) || ! finite (outMean, Gesture::dims, 100.0f)
        || ! finite (outVar, Gesture::dims, 1.0e4f) || ! finite (tired, heads, 10.0f) || ! finite (habit, 8, 1.5f)
        || ! finite (bandMean, 6, 200.0f))
        return false;
    for (float v : outVar)
        if (v <= 0.0f)
            return false;
    for (float v : { arousal, motion, throwRate, density })
        if (! std::isfinite (v) || v < 0.0f || v > 1.0e4f)
            return false;
    if (diaryCount < 0 || diaryCount > diarySize || diaryHead < 0 || diaryHead >= diarySize)
        return false;
    for (const auto& e : diary)
        if (! finite (e.u, Gesture::dims, 1.5f))
            return false;
    return true;
}

//==============================================================================
void Creature::grow() noexcept
{
    Genes g { me.genome };
    const float w = gain / std::sqrt ((float) neurons), r = 1.0f / std::sqrt ((float) neurons);
    for (int i = 0; i < neurons; ++i)
    {
        for (int j = 0; j < neurons; ++j)
            W[i][j] = i == j ? 0.0f : g.normal() * w;
        for (int k = 0; k < senses; ++k)
            U[i][k] = 0.5f * g.normal();
        for (int k = 0; k < 2 * rhythms; ++k)
            B[i][k] = 0.4f * g.normal();
        for (int k = 0; k < kicks; ++k)
            K[i][k] = 0.8f * g.normal();
        theta[i] = 0.2f * (2.0f * g.unit() - 1.0f);
        invTau[i] = 1.0f / std::exp (std::log (tauShort) + (std::log (tauLong) - std::log (tauShort)) * g.unit());
    }
    for (int o = 0; o < Gesture::dims; ++o)
        for (int i = 0; i < neurons; ++i)
            R[o][i] = g.normal() * r;
    for (auto& plane : planes)
        for (auto& word : plane)
            word = g.next();
    for (int k = 0; k < rhythms; ++k)
    {
        turnCos[k] = std::cos (tau / rhythmPeriod[k]);
        turnSin[k] = std::sin (tau / rhythmPeriod[k]);
    }
    grownFrom = me.genome;
    grown = true;
}

void Creature::beBorn (uint64_t genome) noexcept
{
    me = Life {};
    me.genome = genome == 0 ? 1 : genome;
    grow();
    Genes g { mix (me.genome ^ 0xA5A5A5A5A5A5A5A5ull) };
    for (auto& r : me.rhythm)
    {
        const float a = tau * g.unit();
        r[0] = std::cos (a);
        r[1] = std::sin (a);
    }
    for (int i = 0; i < neurons; ++i)
        me.y[i] = 0.5f * g.normal();
    for (auto& v : me.outVar)
        v = 0.2f;
    for (auto& h : me.habit)
        h = 0.5f;
    rest();
}

void Creature::rest() noexcept
{
    if (! grown)
        beBorn (me.genome);
    kick.fill (0.0f);
    thoughts = thoughtHead = 0;
    lastFeed = lastSpace = -1.0f;
    lastBands.fill (-80.0f);
    barSteps = barOnsets = 0;
    printReady = false;
    familiar = 0.0f;
    for (int i = 0; i < neurons; ++i)
    {
        goodY[i] = me.y[i];
        fire[(size_t) i] = std::tanh (me.y[i] + theta[i]);
    }
}

bool Creature::adopt (const Life& saved) noexcept
{
    if (! saved.valid())
        return false;
    me = saved;
    if (! grown || grownFrom != me.genome)
        grow();
    rest();
    return true;
}

//==============================================================================
void Creature::step (const Senses& s) noexcept
{
    ++me.age;
    const float dt = std::clamp (s.stepSeconds, 0.02f, 2.0f);

    // the music's shape, against its own running average (level-free)
    std::array<float, senses> x {};
    if (s.sounding)
    {
        if (me.bandMeanReady == 0)
        {
            for (int b = 0; b < 6; ++b)
                me.bandMean[b] = s.bandsDb[(size_t) b];
            me.bandMeanReady = 1;
        }
        for (int b = 0; b < 6; ++b)
        {
            me.bandMean[b] += (s.bandsDb[(size_t) b] - me.bandMean[b]) / 256.0f;
            x[(size_t) b] = std::clamp ((s.bandsDb[(size_t) b] - me.bandMean[b]) / 12.0f, -2.0f, 2.0f);
        }
    }
    // a hit: some band jumped by more than 6 dB since the last sixteenth (a held pad never does)
    float flux = 0.0f;
    for (int b = 0; b < 6; ++b)
        flux = std::max (flux, s.bandsDb[(size_t) b] - lastBands[(size_t) b]);
    lastBands = s.bandsDb;
    const bool hit = s.sounding && flux > 6.0f;
    x[6] = clamp01 (s.onset);
    x[7] = s.repetition - 0.5f;

    // the hand: how fast the light moves, how often it throws; and the music's density
    if (lastFeed < 0.0f)
    {
        lastFeed = s.feed;
        lastSpace = s.space;
    }
    const float moved = std::abs (s.feed - lastFeed) + std::abs (s.space - lastSpace);
    lastFeed = s.feed;
    lastSpace = s.space;
    me.motion += (moved / dt - me.motion) * (1.0f - std::exp (-dt / 2.0f));
    me.throwRate *= std::exp (-dt / 60.0f);
    if (s.throwStarted)
        me.throwRate += 1.0f;
    me.density += ((hit ? 1.0f : 0.0f) - me.density) / 16.0f;
    const float want = clamp01 (0.25f * me.throwRate + 2.0f * me.motion + 0.4f * me.density);
    me.arousal += (want - me.arousal) * (1.0f - std::exp (-dt / (want > me.arousal ? 20.0f : 120.0f)));
    x[8] = me.arousal - 0.5f;
    x[9] = s.feed - 0.5f;
    x[10] = s.space - 0.5f;

    // a throw jolts it
    if (s.throwStarted)
    {
        Genes g { mix (me.genome ^ me.age) };
        for (int j = 0; j < kicks; ++j)
        {
            const float v = 0.5f * (g.unit() - 0.5f);
            for (int i = 0; i < neurons; ++i)
                kick[(size_t) i] += K[i][j] * v;
        }
    }

    // its body: three slow clocks of its own
    float body[2 * rhythms];
    for (int k = 0; k < rhythms; ++k)
    {
        auto& r = me.rhythm[k];
        const float c = r[0] * turnCos[k] - r[1] * turnSin[k];
        const float sn = r[0] * turnSin[k] + r[1] * turnCos[k];
        const float n = 1.0f / std::sqrt (std::max (c * c + sn * sn, 1.0e-12f));
        r[0] = c * n;
        r[1] = sn * n;
        body[2 * k] = r[0];
        body[2 * k + 1] = r[1];
    }

    // the mind: 32 leaky neurons
    float next[neurons];
    bool sane = true;
    for (int i = 0; i < neurons; ++i)
    {
        float d = 0.0f;
        for (int j = 0; j < neurons; ++j)
            d += W[i][j] * fire[(size_t) j];
        for (int k = 0; k < senses; ++k)
            d += U[i][k] * x[(size_t) k];
        for (int k = 0; k < 2 * rhythms; ++k)
            d += B[i][k] * body[k];
        next[i] = std::clamp (me.y[i] + invTau[i] * (d - me.y[i]) + kick[(size_t) i], -6.0f, 6.0f); // a kick is a jolt
        sane = sane && std::isfinite (next[i]);
    }
    kick.fill (0.0f);
    for (int i = 0; i < neurons; ++i)
        me.y[i] = sane ? next[i] : goodY[i];
    for (int i = 0; i < neurons; ++i)
        fire[(size_t) i] = std::tanh (me.y[i] + theta[i]);

    // what it is thinking, as a gesture, each number spread over its own recent range
    std::array<float, Gesture::dims> u {};
    const float c = std::max (1.0f / 16384.0f, 1.0f / ((float) me.age + 32.0f));
    for (int o = 0; o < Gesture::dims; ++o)
    {
        float z = 0.0f;
        for (int i = 0; i < neurons; ++i)
            z += R[o][i] * fire[(size_t) i];
        me.outMean[o] += c * (z - me.outMean[o]);
        me.outVar[o] = std::max (1.0e-4f, me.outVar[o] + c * ((z - me.outMean[o]) * (z - me.outMean[o]) - me.outVar[o]));
        u[(size_t) o] = 0.5f * (1.0f + std::erf ((z - me.outMean[o]) / std::sqrt (2.0f * me.outVar[o])));
    }
    patience = u[13];
    thought[(size_t) thoughtHead] = u;
    fresh[(size_t) thoughtHead] = novelty (u);
    thoughtHead = (thoughtHead + 1) % candidates;
    thoughts = std::min (thoughts + 1, candidates);

    // the bar, for its fingerprint, brightness and density
    if (s.stepsPerBar <= maxBarSteps)
    {
        const int at = std::clamp (s.stepInBar, 0, maxBarSteps - 1);
        if (at == 0)
            barSteps = barOnsets = 0;
        bar[(size_t) at] = s.bandsDb;
        ++barSteps;
        if (hit)
            ++barOnsets;
        if (at == s.stepsPerBar - 1 && barSteps >= s.stepsPerBar)
        {
            barSteps = s.stepsPerBar;
            fingerprint();
            for (int i = 0; i < neurons; ++i)
                goodY[i] = me.y[i];
        }
    }
}

void Creature::fingerprint() noexcept
{
    const int n = barSteps;
    float mean = 0.0f;
    std::array<float, 6> band {};
    for (int t = 0; t < n; ++t)
        for (int b = 0; b < 6; ++b)
        {
            mean += bar[(size_t) t][(size_t) b];
            band[(size_t) b] += bar[(size_t) t][(size_t) b];
        }
    mean /= (float) (n * 6);
    for (auto& v : band)
        v /= (float) n;

    // a random-hyperplane hash (SimHash) of the bar's shape, level removed
    uint64_t hash = 0;
    for (int p = 0; p < 64; ++p)
    {
        float acc = 0.0f;
        for (int t = 0; t < n; ++t)
            for (int b = 0; b < 6; ++b)
            {
                const int i = t * 6 + b;
                const bool plus = ((planes[p][i >> 6] >> (i & 63)) & 1u) != 0;
                acc += plus ? bar[(size_t) t][(size_t) b] - mean : mean - bar[(size_t) t][(size_t) b];
            }
        if (acc > 0.0f)
            hash |= 1ull << p;
    }
    currentPrint = hash;
    printReady = true;

    // how well it knows this loop: the nearest print it has itched on
    int nearest = 64;
    uint32_t itchesThere = 0;
    for (const auto& p : me.print)
        if (p.itches > 0)
        {
            const int h = bitsSet (p.hash ^ hash);
            if (h < nearest)
            {
                nearest = h;
                itchesThere = p.itches;
            }
        }
    familiar = nearest <= 64 && itchesThere > 0
                   ? std::exp (-(float) nearest / 8.0f) * std::min (1.0f, (float) itchesThere / 3.0f)
                   : 0.0f;

    // brightness: the centroid of the bands that are within 36 dB of the loudest
    const float top = *std::max_element (band.begin(), band.end());
    float wsum = 0.0f, centroid = 0.0f;
    for (int b = 0; b < 6; ++b)
    {
        const float w = std::max (0.0f, band[(size_t) b] - (top - 36.0f));
        wsum += w;
        centroid += w * (float) b / 5.0f;
    }
    const float c = wsum > 0.0f ? centroid / wsum : 0.3f;
    bright = clamp01 (0.5f + 2.2f * (c - 0.38f));
    dense = clamp01 ((float) barOnsets / (float) n);

    // instincts: the kind of music leans toward a kind of head
    urge.fill (0.0f);
    const float low = 0.5f * (band[0] + band[1]) - (band[3] + band[4] + band[5]) / 3.0f;
    if (low > 18.0f)
        urge[2] += 0.1f; // heavy low end: a hand on the reel
    if (dense > 0.45f && band[5] > top - 24.0f)
        urge[3] += 0.1f; // dense highs: the swarm
    if (dense < 0.2f)
    {
        urge[4] += 0.1f; // sparse stabs: the ghost and the laugh
        urge[1] += 0.1f;
    }
}

float Creature::novelty (const std::array<float, Gesture::dims>& u) const noexcept
{
    if (me.diaryCount == 0)
        return 1.0f;
    const auto distance = [&] (const Life::Entry& e)
    {
        float d = 0.0f;
        for (int k = 0; k < 8; ++k)
            d += (u[(size_t) k] - e.u[k]) * (u[(size_t) k] - e.u[k]);
        return std::sqrt (d);
    };
    // against its itches on this loop, if it knows the loop well enough; else its last 32
    float best = 10.0f;
    int found = 0;
    if (printReady)
        for (int i = 0; i < me.diaryCount; ++i)
        {
            const auto& e = me.diary[i];
            if (bitsSet (e.print ^ currentPrint) <= maxHamming)
            {
                best = std::min (best, distance (e));
                ++found;
            }
        }
    if (found < 4)
    {
        best = 10.0f;
        for (int k = 0; k < std::min (32, me.diaryCount); ++k)
            best = std::min (best, distance (me.diary[(me.diaryHead - 1 - k + diarySize) % diarySize]));
    }
    return best;
}

float Creature::threshold() const noexcept
{
    return std::clamp (0.72f - 0.04f * familiar - 0.03f * (2.0f * me.arousal - 1.0f) + 0.05f * (patience - 0.5f), 0.66f, 0.78f);
}

Gesture Creature::itch (double ppq, int frameChoice) noexcept
{
    Gesture g;

    // the freshest of its last few thoughts
    if (thoughts == 0)
    {
        thought[0].fill (0.5f);
        fresh[0] = 1.0f;
        thoughts = 1;
    }
    int best = 0;
    for (int k = 1; k < thoughts; ++k)
        if (fresh[(size_t) k] > fresh[(size_t) best])
            best = k;
    auto u = thought[(size_t) best];

    // leaning away from its habits, pulled by the music
    for (int d = 0; d < 8; ++d)
    {
        u[(size_t) d] = clamp01 (u[(size_t) d] + antiHabit * (u[(size_t) d] - me.habit[d]));
        me.habit[d] += antiHabit * (u[(size_t) d] - me.habit[d]);
    }
    // it tires of turning the same way: after two itches to one side, the third turns back
    if (me.diaryCount >= 2)
    {
        const auto& a = me.diary[(me.diaryHead - 1 + diarySize) % diarySize];
        const auto& b = me.diary[(me.diaryHead - 2 + diarySize) % diarySize];
        const bool right = u[2] >= 0.5f;
        if ((a.u[2] >= 0.5f) == right && (b.u[2] >= 0.5f) == right)
            u[2] = 1.0f - u[2];
    }
    u[0] = clamp01 (u[0] + instinct * (bright - 0.5f));
    u[1] = clamp01 (u[1] + instinct * (dense - 0.4f));

    // the three heads it wants most and is least tired of
    std::array<int, heads> rank { 0, 1, 2, 3, 4 };
    std::array<float, heads> score {};
    for (int k = 0; k < heads; ++k)
        score[(size_t) k] = u[(size_t) (8 + k)] - tiredness * me.tired[k] + urge[(size_t) k];
    std::sort (rank.begin(), rank.end(), [&] (int a, int b) { return score[(size_t) a] > score[(size_t) b]; });
    g.order = { rank[0], rank[1], rank[2] };
    if (g.setMask() == me.lastSet)
        g.order = { rank[0], rank[1], rank[3] };
    me.lastSet = g.setMask();
    for (int k = 0; k < heads; ++k)
        me.tired[k] = 0.7f * me.tired[k] + ((me.lastSet >> k) & 1 ? 0.5f : 0.0f);

    g.u = u;
    uint64_t t = mix (me.genome ^ ((uint64_t) me.itches << 20));
    for (int i = 0; i < neurons; ++i)
        t = mix (t ^ (uint64_t) (int64_t) std::lround (me.y[i] * 4096.0f));
    g.texture = t;
    g.heal = 50.0f * std::exp2 (-me.arousal);         // calm lingers, wound up heals sooner
    g.tape = 0.8f + 0.4f * me.arousal;
    g.lift = 0.1f * familiar;

    // it feels what it just did
    for (int i = 0; i < neurons; ++i)
    {
        float k = 0.0f;
        for (int j = 0; j < kicks; ++j)
            k += K[i][j] * (u[(size_t) j] - 0.5f);
        kick[(size_t) i] += k;
    }

    // the diary
    auto& e = me.diary[me.diaryHead];
    for (int d = 0; d < Gesture::dims; ++d)
        e.u[d] = u[(size_t) d];
    for (int k = 0; k < 3; ++k)
        e.order[k] = (int8_t) g.order[(size_t) k];
    e.frame = (int8_t) frameChoice;
    e.ppq = (float) ppq;
    e.print = printReady ? currentPrint : 0;
    me.diaryHead = (me.diaryHead + 1) % diarySize;
    me.diaryCount = std::min (me.diaryCount + 1, diarySize);

    // the loop it itched on
    ++me.itches;
    if (printReady)
    {
        int nearest = maxHamming + 1, at = -1, oldest = 0;
        for (int i = 0; i < printCount; ++i)
        {
            const auto& p = me.print[i];
            if (p.itches > 0)
            {
                const int h = bitsSet (p.hash ^ currentPrint);
                if (h < nearest)
                {
                    nearest = h;
                    at = i;
                }
            }
            if (p.seen < me.print[oldest].seen)
                oldest = i;
        }
        if (at < 0)
        {
            at = oldest;
            me.print[at] = { currentPrint, 0, 0 };
        }
        ++me.print[at].itches;
        me.print[at].seen = me.itches;
    }
    return g;
}
} // namespace dendrite
