// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Knob bodies rendered from height maps with diffuse and specular lighting,
// the method of the owner's chocolatefudgeverb UI applied to rack hardware.
//
// Each body is a function of radius (and angle, for knurls and flutes) giving
// height and material. Normals come from the height field; the light sits
// up and to the left of the screen and does not turn with the knob, so the
// body is rotationally symmetric and is rendered once per style and pixel
// size, then cached. The pointer, which does turn, is drawn on top.
namespace kit
{
    struct KnobBody
    {
        // Radii as fractions of the knob's radius.
        float skirtRadius = 0.95f, bodyRadius = 0.72f, insertRadius = 0.0f;
        float skirtHeight = 0.12f, bodyHeight = 0.30f, dome = 0.05f;
        int skirtKnurls = 0, bodyFlutes = 0;
        float knurlDepth = 0.03f;
        juce::Colour skirt, body, insert;
        float gloss = 0.4f, shininess = 30.0f;  // plastic
        bool brushedInsert = true;              // spun-aluminium insert with an anisotropic highlight
    };

    class KnobRenderer
    {
    public:
        // Rendered at `pixelSize` (the knob's diameter in physical pixels).
        static juce::Image body (int styleKey, const KnobBody&, int pixelSize);
    };
}
