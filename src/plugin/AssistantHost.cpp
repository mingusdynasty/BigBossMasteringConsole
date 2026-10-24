#include "AssistantHost.h"

#include "PluginProcessor.h"

AssistantHost::~AssistantHost()
{
    stopWorker();
}

void AssistantHost::stopWorker()
{
    cancel.store (true);
    if (worker.joinable()) worker.join();
    cancel.store (false);
}

double AssistantHost::thinkingSeconds() const
{
    return getState() == State::Thinking ? 0.001 * (juce::Time::getMillisecondCounterHiRes() - thinkingSince.load()) : 0.0;
}

std::shared_ptr<const bb::assist::Plan> AssistantHost::plan() const
{
    const std::lock_guard<std::mutex> guard (planLock);
    return latest;
}

void AssistantHost::listen()
{
    const auto s = getState();
    if (s == State::Listening)   // pressed again: stop
    {
        stopWorker();
        proc.cancelCapture();
        state.store ((int) State::Idle);
        return;
    }
    if (s == State::Thinking) return;
    stopWorker();
    {
        const std::lock_guard<std::mutex> guard (planLock);
        latest.reset();
    }
    state.store ((int) State::Listening);
    proc.startCapture();
    worker = std::thread ([this] { work(); });
}

void AssistantHost::work()
{
    std::vector<float> l, r;
    double rate = 0;
    // wait for the capture to fill; a capture that stops without filling (LISTEN pressed again, or
    // the host re-prepared) ends the wait
    for (;;)
    {
        if (cancel.load()) return;
        if (proc.takeCapture (l, r, rate)) break;
        if (! proc.capturePending())
        {
            if (proc.takeCapture (l, r, rate)) break;   // it filled just now
            state.store ((int) State::Idle);
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    thinkingSince.store (juce::Time::getMillisecondCounterHiRes());
    state.store ((int) State::Thinking, std::memory_order_release);
    // MATCH REF plans against the loaded reference; holding it here keeps it alive while planning
    const auto ref = getGoal() == bb::assist::Goal::Reference ? proc.reference() : nullptr;
    auto p = std::make_shared<bb::assist::Plan> (bb::assist::plan (l, r, rate, getGoal(), proc.assistOrder(), &cancel,
                                                                   ref != nullptr ? &ref->profile : nullptr));
    if (cancel.load()) { state.store ((int) State::Idle); return; }
    const bool ok = p->outLufs > -100.0;
    {
        const std::lock_guard<std::mutex> guard (planLock);
        latest = std::move (p);
    }
    state.store ((int) (ok ? State::Ready : State::Failed), std::memory_order_release);
}

bool AssistantHost::apply()
{
    const auto p = plan();
    if (p == nullptr || getState() != State::Ready) return false;
    before = proc.currentValues();
    beforePreset = proc.getPresetName();
    beforeOrder = proc.getOrder();
    proc.applyPlan (*p);
    state.store ((int) State::Applied);
    return true;
}

bool AssistantHost::undo()
{
    if (before.empty()) return false;
    proc.applyValues (before, false);
    proc.setOrder (beforeOrder);
    proc.setPresetName (beforePreset);
    before.clear();
    state.store ((int) (plan() != nullptr ? State::Ready : State::Idle));
    return true;
}
