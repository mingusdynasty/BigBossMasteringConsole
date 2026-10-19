#include "Spectrum.h"

#include <cmath>

namespace bbui
{
namespace
{
constexpr float kLoHz = 20.0f, kHiHz = 20000.0f, kTopDb = 6.0f, kBottomDb = -90.0f;
constexpr float kSlope = 4.5f;   // dB per octave, pivoting at 1 kHz: pink noise reads flat
}

Spectrum::Spectrum()
{
    ringL.assign (kSize, 0.0f);
    ringR.assign (kSize, 0.0f);
    window.resize (kSize);
    for (int i = 0; i < kSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kSize);
    work.assign (2 * kSize, 0.0f);
    mid.assign (kSize / 2 + 1, -200.0f);
    side.assign (kSize / 2 + 1, -200.0f);
    peak.assign (kSize / 2 + 1, -200.0f);
    raw.assign (kSize / 2 + 1, -200.0f);
}

void Spectrum::clearPeak()
{
    std::fill (peak.begin(), peak.end(), -200.0f);
    repaint();
}

void Spectrum::setSource (bb::MeterSuite* m, double sampleRate)
{
    src = m;
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    readPos = m != nullptr ? m->samples.head() : 0;
    std::fill (peak.begin(), peak.end(), -200.0f);
}

void Spectrum::tick()
{
    if (src == nullptr) return;
    bb::StereoSample buf[2048];
    for (int got; (got = src->samples.read (readPos, buf, 2048)) > 0;)
        for (int i = 0; i < got; ++i)
        {
            ringL[(size_t) ringPos] = buf[i].l;
            ringR[(size_t) ringPos] = buf[i].r;
            ringPos = (ringPos + 1) & (kSize - 1);
        }

    // coherent gain of the Hann window is 0.5: a full-scale sine reads 0 dB
    const float norm = 2.0f / (0.5f * (float) kSize);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < kSize; ++i)
        {
            const size_t j = (size_t) ((ringPos + i) & (kSize - 1));
            const float v = pass == 0 ? 0.5f * (ringL[j] + ringR[j]) : 0.5f * (ringL[j] - ringR[j]);
            work[(size_t) i] = v * window[(size_t) i];
        }
        std::fill (work.begin() + kSize, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        auto& dst = pass == 0 ? mid : side;
        for (size_t b = 0; b < dst.size(); ++b)
        {
            const float hz = (float) b * (float) sr / (float) kSize;
            const float tilt = hz > 1.0f ? kSlope * std::log2 (hz / 1000.0f) : 0.0f;
            const float db = juce::Decibels::gainToDecibels (work[b] * norm, -200.0f) + tilt;
            // fast rise, ~30 dB/s fall at a 30 Hz tick
            dst[b] = db > dst[b] ? db : juce::jmax (db, dst[b] - 1.0f);
            if (pass == 0)
            {
                raw[b] = db;
                peak[b] = juce::jmax (db, peak[b] - 0.15f);
            }
        }
    }
    repaint();
}

float Spectrum::xOfHz (float hz, juce::Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz);
}

void Spectrum::buildCurve (const std::vector<float>& db, juce::Rectangle<float> r, juce::Path& out, bool closed) const
{
    auto yOf = [&] (float v) { return r.getY() + (1.0f - norm (v, kBottomDb, kTopDb)) * r.getHeight(); };
    const float binHz = (float) sr / (float) kSize;
    bool started = false;
    for (int x = 0; x <= (int) r.getWidth(); x += 2)
    {
        // the bins this pixel column covers; take the loudest (low end: interpolate between two)
        const float f0 = kLoHz * std::pow (kHiHz / kLoHz, (float) x / r.getWidth());
        const float f1 = kLoHz * std::pow (kHiHz / kLoHz, (float) (x + 2) / r.getWidth());
        const float b0 = f0 / binHz, b1 = f1 / binHz;
        float v;
        if (b1 - b0 < 1.0f)
        {
            const int i = (int) b0;
            const float t = b0 - (float) i;
            if (i + 1 >= (int) db.size()) break;
            v = db[(size_t) i] + t * (db[(size_t) i + 1] - db[(size_t) i]);
        }
        else
        {
            v = -200.0f;
            for (int i = (int) b0; i <= (int) b1 && i < (int) db.size(); ++i) v = juce::jmax (v, db[(size_t) i]);
        }
        const float px = r.getX() + (float) x, py = yOf (v);
        if (! started) { out.startNewSubPath (px, closed ? r.getBottom() : py); if (closed) out.lineTo (px, py); started = true; }
        else out.lineTo (px, py);
    }
    if (closed && started) { out.lineTo (r.getRight(), r.getBottom()); out.closeSubPath(); }
}

void Spectrum::paint (juce::Graphics& g)
{
    auto rb = getLocalBounds();
    g.setColour (col::panel);
    g.fillRoundedRectangle (rb.toFloat(), 6.0f);
    rb.reduce (10, 8);
    auto title = rb.removeFromTop (16);
    g.setColour (col::dim);
    g.setFont (font (10.5f, true));
    g.drawText ("SPECTRUM", title, juce::Justification::left);
    g.setFont (font (10.0f));
    g.drawText ("mid   side   " + juce::String (kSlope, 1) + " dB/oct", title, juce::Justification::right);

    auto r = rb.toFloat().withTrimmedLeft (26.0f).withTrimmedBottom (14.0f);
    g.setColour (col::bg);
    g.fillRect (r);

    g.setFont (font (10.0f));
    for (float db = 0.0f; db >= -84.0f; db -= 12.0f)
    {
        const float y = r.getY() + (1.0f - norm (db, kBottomDb, kTopDb)) * r.getHeight();
        g.setColour (col::faint.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (col::faint);
        g.drawText (juce::String ((int) db), (int) rb.getX(), (int) y - 6, 24, 12, juce::Justification::right);
    }
    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = xOfHz (hz, r);
        g.setColour (col::faint.withAlpha (0.45f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (col::faint);
        const juce::String s = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (s, (int) x - 15, (int) r.getBottom() + 1, 30, 12, juce::Justification::centred);
    }

    juce::Path fill, line, sideLine, peakLine;
    buildCurve (mid, r, fill, true);
    buildCurve (mid, r, line, false);
    buildCurve (side, r, sideLine, false);
    buildCurve (peak, r, peakLine, false);
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setGradientFill (juce::ColourGradient (col::amber.withAlpha (0.35f), 0, r.getY(), col::amber.withAlpha (0.04f), 0, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (col::blue.withAlpha (0.7f));
    g.strokePath (sideLine, juce::PathStrokeType (1.0f));
    g.setColour (col::legend.withAlpha (0.35f));
    g.strokePath (peakLine, juce::PathStrokeType (1.0f));
    g.setColour (col::amber);
    g.strokePath (line, juce::PathStrokeType (1.5f));
    g.restoreState();
}

Spectrogram::Spectrogram()
{
    // black, through blue and violet, to the analyser's amber and a pale top
    const juce::Colour stops[] = { juce::Colour (0xff0b0c0e), juce::Colour (0xff122a4a), juce::Colour (0xff4a2a78),
                                   juce::Colour (0xffc0502a), col::amber, juce::Colour (0xfffff0c8) };
    constexpr int nStops = (int) (sizeof (stops) / sizeof (stops[0]));
    for (int i = 0; i < 256; ++i)
    {
        const float t = (float) i / 255.0f * (float) (nStops - 1);
        const int a = juce::jmin ((int) t, nStops - 2);
        palette[(size_t) i] = stops[a].interpolatedWith (stops[a + 1], t - (float) a);
    }
}

juce::Rectangle<int> Spectrogram::plot() const
{
    auto rb = getLocalBounds().reduced (10, 8);
    rb.removeFromTop (16);
    return rb.withTrimmedLeft (26).withTrimmedBottom (14);
}

void Spectrogram::resized()
{
    const auto r = plot();
    if (r.isEmpty()) return;
    if (image.isNull() || image.getWidth() != r.getWidth() || image.getHeight() != r.getHeight())
    {
        image = juce::Image (juce::Image::RGB, r.getWidth(), r.getHeight(), false);
        clear();
    }
}

void Spectrogram::clear()
{
    if (image.isValid()) image.clear (image.getBounds(), palette[0]);
    repaint();
}

void Spectrogram::push (const std::vector<float>& db, double sampleRate)
{
    if (image.isNull() || db.empty()) return;
    const int w = image.getWidth(), h = image.getHeight();
    image.moveImageSection (0, 0, 1, 0, w - 1, h);

    // each pixel row takes the loudest bin it covers (interpolating where bins are wider than a row)
    const float binHz = (float) sampleRate / (float) Spectrum::kSize;
    juce::Image::BitmapData px (image, w - 1, 0, 1, h, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        const float f0 = kLoHz * std::pow (kHiHz / kLoHz, (float) (h - 1 - y) / (float) h);
        const float f1 = kLoHz * std::pow (kHiHz / kLoHz, (float) (h - y) / (float) h);
        const float b0 = f0 / binHz, b1 = f1 / binHz;
        float v = -200.0f;
        if (b1 - b0 < 1.0f)
        {
            const int i = (int) b0;
            if (i + 1 < (int) db.size()) v = db[(size_t) i] + (b0 - (float) i) * (db[(size_t) i + 1] - db[(size_t) i]);
        }
        else
            for (int i = (int) b0; i <= (int) b1 && i < (int) db.size(); ++i) v = juce::jmax (v, db[(size_t) i]);
        // the spectrum's lower 72 dB, so the noise floor stays dark
        const int c = juce::jlimit (0, 255, (int) (255.0f * (v + 78.0f) / 72.0f));
        px.setPixelColour (0, y, palette[(size_t) c]);
    }
    repaint (plot());
}

void Spectrogram::paint (juce::Graphics& g)
{
    auto rb = getLocalBounds();
    g.setColour (col::panel);
    g.fillRoundedRectangle (rb.toFloat(), 6.0f);
    rb.reduce (10, 8);
    auto title = rb.removeFromTop (16);
    g.setColour (col::dim);
    g.setFont (font (10.5f, true));
    g.drawText ("SPECTROGRAM", title, juce::Justification::left);
    g.setFont (font (10.0f));
    g.drawText ("mid   last " + juce::String (juce::roundToInt ((float) plot().getWidth() / kFramesPerSecond)) + " s", title,
                juce::Justification::right);

    const auto r = plot();
    if (image.isValid()) g.drawImageAt (image, r.getX(), r.getY());
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float y = (float) r.getBottom() - (float) r.getHeight() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz);
        g.setColour (col::faint.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, (float) r.getX(), (float) r.getRight());
        g.setColour (col::faint);
        const juce::String s = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (s, rb.getX(), (int) y - 6, 24, 12, juce::Justification::right);
    }
    // seconds ago, along the bottom
    for (int sec = 0; (float) sec * kFramesPerSecond < (float) r.getWidth(); sec += 2)
    {
        const int x = r.getRight() - (int) ((float) sec * kFramesPerSecond);
        g.setColour (col::faint);
        g.drawText (sec == 0 ? juce::String ("now") : "-" + juce::String (sec), x - 15, r.getBottom() + 1, 30, 12, juce::Justification::centred);
    }
}

void Goniometer::tick()
{
    if (src == nullptr) return;
    bb::StereoSample buf[4096];
    pts.clear();
    for (int got; (got = src->samples.read (readPos, buf, 4096)) > 0;)
        pts.insert (pts.end(), buf, buf + got);
    if (pts.size() > 4096) pts.erase (pts.begin(), pts.end() - 4096);
    float pk = 0.0f;
    for (const auto& p : pts) pk = juce::jmax (pk, std::abs (p.l), std::abs (p.r));
    // auto-gain: quiet material still fills the scope; rises fast, relaxes slowly
    const float want = pk > 1e-4f ? 0.9f / pk : 1.0f;
    scale = want < scale ? want : scale + (juce::jmin (want, 64.0f) - scale) * 0.05f;
    repaint();
}

void Goniometer::paint (juce::Graphics& g)
{
    auto rb = getLocalBounds();
    g.setColour (col::panel);
    g.fillRoundedRectangle (rb.toFloat(), 6.0f);
    rb.reduce (10, 8);
    g.setColour (col::dim);
    g.setFont (font (10.5f, true));
    g.drawText ("STEREO FIELD", rb.removeFromTop (16), juce::Justification::left);

    const float side = (float) juce::jmin (rb.getWidth(), rb.getHeight());
    const auto sq = juce::Rectangle<float> (side, side).withCentre (rb.toFloat().getCentre());
    g.setColour (col::bg);
    g.fillEllipse (sq);
    const auto c = sq.getCentre();
    const float rad = side * 0.5f;
    g.setColour (col::faint.withAlpha (0.6f));
    g.drawLine (c.x, sq.getY(), c.x, sq.getBottom(), 1.0f);                                  // mono (M)
    g.drawLine (c.x - rad * 0.707f, c.y - rad * 0.707f, c.x + rad * 0.707f, c.y + rad * 0.707f, 1.0f);   // L
    g.drawLine (c.x + rad * 0.707f, c.y - rad * 0.707f, c.x - rad * 0.707f, c.y + rad * 0.707f, 1.0f);   // R
    g.setFont (font (10.0f));
    g.drawText ("L", (int) (c.x - rad * 0.75f) - 10, (int) (c.y - rad * 0.75f) - 10, 12, 12, juce::Justification::centred);
    g.drawText ("R", (int) (c.x + rad * 0.75f), (int) (c.y - rad * 0.75f) - 10, 12, 12, juce::Justification::centred);

    g.setColour (col::amber.withAlpha (0.35f));
    for (const auto& p : pts)
    {
        // rotated 45 degrees: mono is vertical, side is horizontal
        const float x = (p.r - p.l) * 0.7071f * scale, y = (p.l + p.r) * 0.7071f * scale;
        const float px = c.x + juce::jlimit (-1.0f, 1.0f, x) * rad, py = c.y - juce::jlimit (-1.0f, 1.0f, y) * rad;
        g.fillRect (px, py, 1.5f, 1.5f);
    }
}
} // namespace bbui
