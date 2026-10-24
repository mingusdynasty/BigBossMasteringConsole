#include "PluginEditor.h"

#include "Params.h"

BigBossEditor::BigBossEditor (BigBossProcessor& p)
    : AudioProcessorEditor (p), proc (p), master (p), output (p), analyser (p), assistant (p)
{
    setLookAndFeel (&lookAndFeel);

    for (int s = 0; s < kModules; ++s)
    {
        const juce::String id (BigBossProcessor::kSlots[s].id);
        std::unique_ptr<kit::Panel> u;
        if (id == "eq")         u = std::make_unique<kit::EqPanel> (p.apvts);
        else if (id == "comp")  u = std::make_unique<kit::CompPanel> (p.apvts, p.compressor());
        else if (id == "tape")  u = std::make_unique<kit::TapePanel> (p.apvts, p.tapeMachine());
        else if (id == "deess") u = std::make_unique<kit::DeEssPanel> (p.apvts, p.deesser(), [&p] { return p.deessSettings().freq; },
                                                                     [&p] { return p.sampleRateHz.load(); });
        else                    u = std::make_unique<kit::LimitPanel> (p.apvts, p.limiter(), [&p] { return p.limitSettings(); });
        u->onDrag = [this, s] (int dy) { dragModule (s, dy); };
        u->onDrop = [this, s] { dropModule (s); };
        content.addAndMakeVisible (*u);
        modules[(size_t) s] = std::move (u);
    }
    content.addAndMakeVisible (master);
    master.onOrderChanged = [this] { layoutModules(); };
    content.addAndMakeVisible (output);
    content.addChildComponent (analyser);
    output.analyserKey.onClick = [this] { showAnalyser (output.analyserKey.getToggleState()); };
    master.analyserKey.onClick = [this] { showAnalyser (master.analyserKey.getToggleState()); };
    master.assistantKey.onClick = [this] { showAssistant (master.assistantKey.getToggleState()); };
    master.assistantNews = [this] { return assistant.hasNews() && ! assistant.isVisible(); };
    content.addChildComponent (assistant);
    assistant.onOrderChanged = [this] { layoutModules(); };
    assistant.onClose = [this] { showAssistant (false); };

    content.setSize (designWidth, designHeight);
    addAndMakeVisible (content);
    layoutModules();
    showAnalyser (proc.editorAnalysis);

    // resizable at a fixed aspect ratio, from half to twice the design size; the host's window
    // follows (and can drag) the editor's size
    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth * 3 / 4, designHeight * 3 / 4);   // fits a 1080p screen at 125% display scaling
    tickAll (1.0f / 30.0f);
    startTimerHz (30);
}

BigBossEditor::~BigBossEditor()
{
    setLookAndFeel (nullptr);
}

void BigBossEditor::showAnalyser (bool on)
{
    proc.editorAnalysis = on;
    output.analyserKey.setToggleState (on, juce::dontSendNotification);
    master.analyserKey.setToggleState (on, juce::dontSendNotification);
    for (auto& m : modules) m->setVisible (! on);
    analyser.setVisible (on);
    repaint();
}

void BigBossEditor::showAssistant (bool on)
{
    master.assistantKey.setToggleState (on, juce::dontSendNotification);
    assistant.setVisible (on);
    if (on) assistant.toFront (false);
}

void BigBossEditor::tickAll (float seconds)
{
    assistant.tick (seconds);   // always: it keeps listening and working while folded away
    analyser.tick (seconds);   // always, so the loudness history has no gap
    master.tick (seconds);
    output.tick (seconds);
    // the host can move the modules too (a session restored, a host undo, a program change)
    if (draggingSlot < 0 && proc.getOrder() != laidOut) layoutModules();
    // every module, shown or not, so meters and the limiter's history have no gap
    for (auto& m : modules) m->tick (seconds);
}

void BigBossEditor::layoutModules()
{
    const auto order = proc.getOrder();
    laidOut = order;
    for (int k = 0; k < kModules; ++k)
        modules[(size_t) order[(size_t) k]]->setBounds (kit::earWidth, modulesTop + k * kit::unitHeight, kit::panelWidth, kit::unitHeight);
    master.setBounds (kit::earWidth, 0, kit::panelWidth, kit::masterHeight);
    assistant.setBounds (kit::earWidth, modulesTop, kit::panelWidth, kit::unitHeight + kit::AssistantPanel::shadow);
    output.setBounds (kit::earWidth, modulesTop + kModules * kit::unitHeight, kit::panelWidth, kit::unitHeight);
    analyser.setBounds (kit::earWidth, modulesTop, kit::panelWidth, kModules * kit::unitHeight);
}

void BigBossEditor::dragModule (int slot, int dy)
{
    const auto order = proc.getOrder();
    int from = 0;
    while (from < kModules && order[(size_t) from] != slot) ++from;
    const int y = juce::jlimit (0, (kModules - 1) * kit::unitHeight, from * kit::unitHeight + dy);
    dragRow = juce::jlimit (0, kModules - 1, (y + kit::unitHeight / 2) / kit::unitHeight);
    draggingSlot = slot;

    // the others close up around where it would land
    std::vector<int> rest;
    for (int k = 0; k < kModules; ++k)
        if (order[(size_t) k] != slot) rest.push_back (order[(size_t) k]);
    rest.insert (rest.begin() + dragRow, slot);
    for (int k = 0; k < kModules; ++k)
        if (rest[(size_t) k] != slot)
            modules[(size_t) rest[(size_t) k]]->setTopLeftPosition (kit::earWidth, modulesTop + k * kit::unitHeight);
    modules[(size_t) slot]->setTopLeftPosition (kit::earWidth, modulesTop + y);
    modules[(size_t) slot]->toFront (false);
    if (assistant.isVisible()) assistant.toFront (false);
}

void BigBossEditor::dropModule (int slot)
{
    if (draggingSlot == slot && dragRow >= 0)
    {
        auto order = proc.getOrder();
        std::vector<int> rest;
        for (int k = 0; k < kModules; ++k)
            if (order[(size_t) k] != slot) rest.push_back (order[(size_t) k]);
        rest.insert (rest.begin() + dragRow, slot);
        for (int k = 0; k < kModules; ++k) order[(size_t) k] = rest[(size_t) k];
        proc.setOrder (order);
    }
    draggingSlot = dragRow = -1;
    layoutModules();
}

juce::AffineTransform BigBossEditor::designToWindow() const
{
    // the whole rack always fits: if the host gives the window another shape than the rack's, the
    // rack is centred in it rather than cropped
    const auto scale = juce::jmin ((float) getWidth() / (float) designWidth, (float) getHeight() / (float) designHeight);
    return juce::AffineTransform::scale (scale).translated (0.5f * ((float) getWidth() - scale * (float) designWidth),
                                                            0.5f * ((float) getHeight() - scale * (float) designHeight));
}

void BigBossEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a0a0a));
    g.addTransform (designToWindow());

    // rack rails, with square cage-nut holes
    g.setColour (juce::Colour (0xff141414));
    g.fillRect (0, 0, designWidth, designHeight);
    for (auto x : { 0, designWidth - kit::earWidth })
    {
        const auto ear = juce::Rectangle<float> ((float) x, 0.0f, (float) kit::earWidth, (float) designHeight);
        const bool right = x > 0;
        g.setGradientFill (juce::ColourGradient (juce::Colour (right ? 0xff1e1e20 : 0xff3a3a3c), ear.getX(), 0.0f,
                                                 juce::Colour (right ? 0xff3a3a3c : 0xff1e1e20), ear.getRight(), 0.0f, false));
        g.fillRect (ear);
        g.setColour (juce::Colour (0xff0a0a0a));
        for (int i = 0; i < 3 * (kModules + 2); ++i)
        {
            if (i % 3 == 1) continue;
            const auto cy = (float) kit::unitHeight * ((float) i + 0.5f) / 3.0f - (float) (kit::unitHeight - kit::masterHeight);
            if (cy < 0.0f || cy > (float) designHeight) continue;
            g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ ear.getCentreX(), cy }));
        }
    }

    // each unit's ears, screwed to the rails (the analyser is one 5U unit)
    auto ears = [&] (float top, float height)
    {
        for (auto x : { 0, designWidth - kit::earWidth })
        {
            const auto plate = juce::Rectangle<float> ((float) x + (x == 0 ? 3.0f : 0.0f), top + 2.0f, (float) kit::earWidth - 3.0f, height - 4.0f);
            g.setColour (juce::Colour (0xff2c2c2e));
            g.fillRoundedRectangle (plate, 2.0f);
            for (auto fy : { 0.2f, 0.8f })
                kit::drawScrew (g, { plate.getCentreX(), top + (height > (float) kit::unitHeight ? (fy < 0.5f ? 24.0f : height - 24.0f)
                                                                                                  : fy * height) }, 7.0f);
        }
    };
    ears (0.0f, (float) kit::masterHeight);
    if (analyser.isVisible()) ears ((float) modulesTop, (float) (kModules * kit::unitHeight));
    else
        for (int u = 0; u < kModules; ++u) ears ((float) (modulesTop + u * kit::unitHeight), (float) kit::unitHeight);
    ears ((float) (modulesTop + kModules * kit::unitHeight), (float) kit::unitHeight);
}

void BigBossEditor::resized()
{
    content.setBounds (0, 0, designWidth, designHeight);
    content.setTransform (designToWindow());
    repaint();
}
