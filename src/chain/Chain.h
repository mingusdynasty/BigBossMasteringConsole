// The mastering chain: modules in a user-chosen order, each with its own bypass.
//
// - Latency is the sum of every module's latency and never depends on bypass or order, so the host's
//   delay compensation stays put. A bypassed module's dry path is delayed by that module's latency.
// - Bypass crossfades over 10 ms between the module and its delayed dry path. Bypassed modules keep
//   running, so switching back in is seamless (their filters and envelopes are already settled).
// - A new order is applied behind a short mute: fade out (10 ms, raised cosine), switch, hold until the
//   audio already in flight has cleared the chain's latency and the modules have settled on their new
//   inputs (10 ms more), fade in (10 ms).
// - Bypass and order can be set from any thread; they take effect at the next block.
#pragma once

#include "Module.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace bb
{
class Chain
{
public:
    static constexpr int kMaxSlots = 8;
    using Order = std::array<int, kMaxSlots>;

    // setup, before prepare(); returns the slot index
    int add (std::unique_ptr<Module> m);
    int size() const { return (int) slots.size(); }
    Module& module (int slot) { return *slots[(size_t) slot]->m; }
    int slotOf (const char* moduleId) const;   // -1 if none

    void prepare (double sampleRate, int maxBlock);   // allocates
    void reset();
    int latency() const { return totalLatency; }
    void process (double* l, double* r, int n);

    void setBypassed (int slot, bool bypassed) { slots[(size_t) slot]->target.store (bypassed ? 0.0f : 1.0f); }
    bool isBypassed (int slot) const { return slots[(size_t) slot]->target.load() < 0.5f; }

    // order[k] is the slot that runs k-th; it must be a permutation of 0 .. size()-1, otherwise it is ignored
    bool setOrder (const Order& order);
    Order order() const { return unpack (pending.load()); }   // the requested order (applies shortly)
    bool isValidOrder (const Order& order) const;

    static std::uint32_t pack (const Order& o);
    static Order unpack (std::uint32_t p);

private:
    struct Slot
    {
        std::unique_ptr<Module> m;
        std::atomic<float> target { 1.0f };   // 1 = in, 0 = bypassed
        double mix = 1.0;
        int lat = 0, pos = 0, mask = 0;
        std::array<std::vector<double>, 2> line, dry;
    };
    void runSlot (Slot& s, double* l, double* r, int n);

    std::vector<std::unique_ptr<Slot>> slots;
    std::atomic<std::uint32_t> pending { 0 };
    std::uint32_t active = 0;
    int totalLatency = 0, maxBlock = 512;
    double fadeStep = 0.01, bypassStep = 0.01, gain = 1.0;
    enum class Phase { Running, FadingOut, Holding, FadingIn } phase = Phase::Running;
    int holdLeft = 0, settleSamples = 480;
};
} // namespace bb
