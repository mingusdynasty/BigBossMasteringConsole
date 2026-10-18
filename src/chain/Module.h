// One processor in the mastering chain. Stereo, double precision, in place.
#pragma once

namespace bb
{
class Module
{
public:
    virtual ~Module() = default;

    virtual const char* id() const = 0;      // stable, used in saved state and parameter ids
    virtual const char* name() const = 0;    // shown on the console

    virtual void prepare (double sampleRate, int maxBlock) = 0;   // may allocate
    virtual void reset() = 0;
    // fixed for a given sample rate: the chain relies on it not changing between prepare() calls
    virtual int latency() const { return 0; }
    virtual void process (double* l, double* r, int n) = 0;
};

// Holds a slot open for a module that is not built yet. Passes audio straight through.
class EmptySlot final : public Module
{
public:
    EmptySlot (const char* slotId, const char* slotName) : sid (slotId), sname (slotName) {}
    const char* id() const override { return sid; }
    const char* name() const override { return sname; }
    void prepare (double, int) override {}
    void reset() override {}
    void process (double*, double*, int) override {}

private:
    const char* sid;
    const char* sname;
};
} // namespace bb
