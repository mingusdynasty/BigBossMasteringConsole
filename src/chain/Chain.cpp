#include "Chain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bb
{
std::uint32_t Chain::pack (const Order& o)
{
    std::uint32_t p = 0;
    for (int k = 0; k < kMaxSlots; ++k) p |= (std::uint32_t) (o[(size_t) k] & 15) << (4 * k);
    return p;
}

Chain::Order Chain::unpack (std::uint32_t p)
{
    Order o {};
    for (int k = 0; k < kMaxSlots; ++k) o[(size_t) k] = (int) ((p >> (4 * k)) & 15);
    return o;
}

int Chain::add (std::unique_ptr<Module> m)
{
    auto s = std::make_unique<Slot>();
    s->m = std::move (m);
    slots.push_back (std::move (s));
    Order o {};
    for (int k = 0; k < kMaxSlots; ++k) o[(size_t) k] = k < size() ? k : 0;
    active = pack (o);
    pending.store (active);
    return size() - 1;
}

int Chain::slotOf (const char* moduleId) const
{
    for (int i = 0; i < size(); ++i)
        if (std::strcmp (slots[(size_t) i]->m->id(), moduleId) == 0) return i;
    return -1;
}

bool Chain::isValidOrder (const Order& o) const
{
    std::array<bool, kMaxSlots> seen {};
    for (int k = 0; k < size(); ++k)
    {
        const int s = o[(size_t) k];
        if (s < 0 || s >= size() || seen[(size_t) s]) return false;
        seen[(size_t) s] = true;
    }
    return true;
}

bool Chain::setOrder (const Order& o)
{
    if (! isValidOrder (o)) return false;
    Order clean = o;
    for (int k = size(); k < kMaxSlots; ++k) clean[(size_t) k] = 0;
    pending.store (pack (clean));
    return true;
}

void Chain::prepare (double sampleRate, int maxBlockSize)
{
    maxBlock = std::max (1, maxBlockSize);
    totalLatency = 0;
    for (auto& s : slots)
    {
        s->m->prepare (sampleRate, maxBlock);
        s->lat = std::max (0, s->m->latency());
        totalLatency += s->lat;
        int len = 1;
        while (len < s->lat + 1) len <<= 1;
        s->mask = len - 1;
        for (int c = 0; c < 2; ++c)
        {
            s->line[(size_t) c].assign ((size_t) len, 0.0);
            s->dry[(size_t) c].assign ((size_t) maxBlock, 0.0);
        }
    }
    bypassStep = 1.0 / std::max (1.0, 0.010 * sampleRate);
    fadeStep   = 1.0 / std::max (1.0, 0.005 * sampleRate);
    reset();
}

void Chain::reset()
{
    for (auto& s : slots)
    {
        s->m->reset();
        for (auto& v : s->line) std::fill (v.begin(), v.end(), 0.0);
        s->pos = 0;
        s->mix = s->target.load();
    }
    active = pending.load();
    gain = 1.0;
    phase = Phase::Running;
}

void Chain::runSlot (Slot& s, double* l, double* r, int n)
{
    const double target = s.target.load (std::memory_order_relaxed);
    double* io[2] { l, r };

    // dry path, delayed by the module's latency
    for (int c = 0; c < 2; ++c)
    {
        auto& line = s.line[(size_t) c];
        double* d = s.dry[(size_t) c].data();
        int p = s.pos;
        for (int i = 0; i < n; ++i)
        {
            line[(size_t) p] = io[c][i];
            d[i] = line[(size_t) ((p - s.lat) & s.mask)];
            p = (p + 1) & s.mask;
        }
    }
    s.pos = (s.pos + n) & s.mask;

    s.m->process (l, r, n);

    if (s.mix == target)
    {
        if (target == 0.0)
            for (int c = 0; c < 2; ++c) std::memcpy (io[c], s.dry[(size_t) c].data(), sizeof (double) * (size_t) n);
        return;
    }
    // equal-gain crossfade: the paths are time-aligned and mostly correlated
    double m = s.mix;
    for (int i = 0; i < n; ++i)
    {
        m = target > m ? std::min (target, m + bypassStep) : std::max (target, m - bypassStep);
        for (int c = 0; c < 2; ++c) io[c][i] = s.dry[(size_t) c][(size_t) i] + m * (io[c][i] - s.dry[(size_t) c][(size_t) i]);
    }
    s.mix = m;
}

void Chain::process (double* l, double* r, int n)
{
    for (int off = 0; off < n; off += maxBlock)
    {
        const int k = std::min (maxBlock, n - off);
        double* bl = l + off;
        double* br = r + off;

        const auto want = pending.load (std::memory_order_acquire);
        if (want != active && phase == Phase::Running) phase = Phase::FadingOut;

        const Order o = unpack (active);
        for (int j = 0; j < size(); ++j) runSlot (*slots[(size_t) o[(size_t) j]], bl, br, k);

        if (phase == Phase::Running) continue;
        for (int i = 0; i < k; ++i)
        {
            switch (phase)
            {
                case Phase::FadingOut:
                    gain = std::max (0.0, gain - fadeStep);
                    if (gain == 0.0)
                    {
                        // switch at the next block boundary; until then stay silent
                        phase = Phase::Holding;
                        holdLeft = -1;
                    }
                    break;
                case Phase::Holding:
                    if (holdLeft > 0 && --holdLeft == 0) phase = Phase::FadingIn;
                    break;
                case Phase::FadingIn:
                    gain = std::min (1.0, gain + fadeStep);
                    if (gain == 1.0) phase = Phase::Running;
                    break;
                case Phase::Running: break;
            }
            bl[i] *= gain;
            br[i] *= gain;
        }
        if (phase == Phase::Holding && holdLeft < 0)
        {
            active = pending.load (std::memory_order_acquire);
            holdLeft = totalLatency + 1;   // let what was already inside the chain flush out
        }
    }
}
} // namespace bb
