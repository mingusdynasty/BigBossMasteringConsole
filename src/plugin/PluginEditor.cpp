#include "PluginEditor.h"

using namespace bbui;

BigBossEditor::BigBossEditor (BigBossProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (int s = 0; s < BigBossProcessor::kNumSlots; ++s)
        chain.slots.push_back ({ BigBossProcessor::kSlots[s].name,
                                 proc.slotIsBuilt (s) ? juce::String ("in") : juce::String (BigBossProcessor::kSlots[s].comingIn),
                                 proc.slotIsBuilt (s) });
    chain.getOrder = [this]
    {
        const auto o = proc.getOrder();
        return std::vector<int> (o.begin(), o.begin() + BigBossProcessor::kNumSlots);
    };
    chain.setOrder = [this] (const std::vector<int>& v)
    {
        bb::Chain::Order o {};
        for (size_t k = 0; k < v.size(); ++k) o[k] = v[k];
        proc.setOrder (o);
        proc.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    };
    chain.isBypassed = [this] (int s) { return proc.apvts.getRawParameterValue (BigBossProcessor::bypassId (s))->load() > 0.5f; };
    chain.toggleBypass = [this] (int s)
    {
        if (auto* prm = proc.apvts.getParameter (BigBossProcessor::bypassId (s)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
            prm->endChangeGesture();
        }
    };
    addAndMakeVisible (chain);

    setupKnob (inTrim, BigBossProcessor::kInTrimId, "INPUT");
    setupKnob (tapeDrive, BigBossProcessor::kTapeAmountId, "TAPE DRIVE");
    setupKnob (outTrim, BigBossProcessor::kOutTrimId, "OUTPUT");

    for (auto* b : { &inButton, &outButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    inButton.onClick  = [this] { useSource (false); };
    outButton.onClick = [this] { useSource (true); };
    resetButton.onClick = [this] { proc.inMeters.requestReset(); proc.outMeters.requestReset(); };
    resetButton.setTooltip ("Restart integrated loudness, LRA and the peak holds");
    addAndMakeVisible (resetButton);

    // reference line for the loudness bars and history; it changes nothing in the audio
    const std::pair<const char*, float> targets[] = { { "-9 LUFS", -9 }, { "-11 LUFS", -11 }, { "-14 LUFS (streaming)", -14 },
                                                      { "-16 LUFS", -16 }, { "-23 LUFS (EBU R128)", -23 } };
    int id = 1;
    for (auto& t : targets) target.addItem (t.first, id++);
    target.setSelectedId (3, juce::dontSendNotification);
    target.onChange = [this, targets]
    {
        const int i = target.getSelectedId() - 1;
        if (i >= 0 && i < 5) meters.targetLufs = targets[i].second;
    };
    addAndMakeVisible (target);

    addAndMakeVisible (meters);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (gonio);
    useSource (true);

    // last, so every child (the knobs' text boxes too) is rebuilt with the console's look
    setLookAndFeel (&look);
    setSize (1180, 720);
    startTimerHz (30);
}

BigBossEditor::~BigBossEditor()
{
    setLookAndFeel (nullptr);
}

void BigBossEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (paramId)->convertFrom0to1 (proc.apvts.getParameter (paramId)->getDefaultValue()));
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setFont (font (11.0f, true));
    k.label.setColour (juce::Label::textColourId, col::dim);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void BigBossEditor::useSource (bool output)
{
    showingOutput = output;
    auto* m = output ? &proc.outMeters : &proc.inMeters;
    shownRate = proc.sampleRateHz.load();
    meters.setSource (m);
    spectrum.setSource (m, shownRate);
    gonio.setSource (m);
    inButton.setToggleState (! output, juce::dontSendNotification);
    outButton.setToggleState (output, juce::dontSendNotification);
}

void BigBossEditor::timerCallback()
{
    if (std::abs (proc.sampleRateHz.load() - shownRate) > 0.5) useSource (showingOutput);
    meters.tick();
    spectrum.tick();
    gonio.tick();
    chain.repaint();
}

void BigBossEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto header = getLocalBounds().removeFromTop (48).reduced (16, 0);
    g.setColour (col::legend);
    g.setFont (font (22.0f, true));
    g.drawText ("BIG BOSS", header.removeFromLeft (124), juce::Justification::centredLeft);
    g.setColour (col::amber);
    g.setFont (font (13.0f));
    g.drawText ("MASTERING CONSOLE", header.removeFromLeft (180), juce::Justification::centredLeft);
    g.setColour (col::panel);
    g.fillRoundedRectangle (knobArea.toFloat(), 6.0f);
}

void BigBossEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (48).reduced (16, 10);
    resetButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (8);
    target.setBounds (header.removeFromRight (170));
    header.removeFromRight (8);
    outButton.setBounds (header.removeFromRight (48));
    header.removeFromRight (4);
    inButton.setBounds (header.removeFromRight (48));

    r.reduce (16, 0);
    chain.setBounds (r.removeFromTop (60));
    r.removeFromTop (12);
    r.removeFromBottom (16);

    auto left = r.removeFromLeft (520);
    meters.setBounds (left);
    r.removeFromLeft (12);
    auto lower = r.removeFromBottom (230);
    spectrum.setBounds (r.withTrimmedBottom (12));
    gonio.setBounds (lower.removeFromLeft (230));
    lower.removeFromLeft (12);
    knobArea = lower;
    const int kw = lower.getWidth() / 3;
    for (auto* k : { &inTrim, &tapeDrive, &outTrim })
    {
        auto a = lower.removeFromLeft (kw).reduced (6, 10);
        k->label.setBounds (a.removeFromTop (16));
        k->slider.setBounds (a);
    }
}
