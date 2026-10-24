// The assistant's working side, owned by the processor so it outlives the editor: it captures the
// input, plans on its own thread, holds the plan, and applies and undoes it. Closing the plugin
// window doesn't drop a plan or wait for one; the assistant panel is only a view of this.
#pragma once

#include "Presets.h"
#include "assist/Assistant.h"
#include "chain/Chain.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class BigBossProcessor;

class AssistantHost
{
public:
    enum class State { Idle, Listening, Thinking, Ready, Applied, Failed };

    explicit AssistantHost (BigBossProcessor& p) : proc (p) {}
    ~AssistantHost();

    // message thread
    void setGoal (bb::assist::Goal g) { goal.store ((int) g); }
    bb::assist::Goal getGoal() const { return (bb::assist::Goal) goal.load(); }
    void listen();        // starts listening; while listening, stops
    bool apply();         // false if there is nothing to apply
    bool undo();          // false if there is nothing to undo

    State getState() const { return (State) state.load (std::memory_order_acquire); }
    bool busy() const { const auto s = getState(); return s == State::Listening || s == State::Thinking; }
    bool hasNews() const { return busy() || getState() == State::Ready; }
    bool canApply() const { return getState() == State::Ready; }
    bool canUndo() const { return ! before.empty(); }
    double thinkingSeconds() const;
    // the latest plan (null until one has been made); safe to keep while a new one is made
    std::shared_ptr<const bb::assist::Plan> plan() const;

private:
    void stopWorker();
    void work();   // the worker: wait for the capture, then plan

    BigBossProcessor& proc;
    std::atomic<int> goal { (int) bb::assist::Goal::Streaming };
    std::atomic<int> state { (int) State::Idle };
    std::atomic<bool> cancel { false };
    std::atomic<double> thinkingSince { 0.0 };
    std::thread worker;
    mutable std::mutex planLock;
    std::shared_ptr<const bb::assist::Plan> latest;

    // for UNDO (message thread only)
    presets::Values before;
    juce::String beforePreset;
    bb::Chain::Order beforeOrder {};
};
