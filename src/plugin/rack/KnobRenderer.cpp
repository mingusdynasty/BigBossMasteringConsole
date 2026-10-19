// Ported from the owner's Vocal Chain plugin (mingusdynasty/rare-luxuriousVocalChain, rebuild/engine at 7502ba1),
// so the two plugins share one rack look. Namespace renamed to kit.
#include "KnobRenderer.h"

#include <map>

namespace kit
{
    namespace
    {
        struct Vec { float x, y, z; };
        Vec normalised (Vec v) { const auto l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); return { v.x / l, v.y / l, v.z / l }; }
        float dot (Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

        float smooth (float e0, float e1, float x)
        {
            const auto t = juce::jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
            return t * t * (3.0f - 2.0f * t);
        }

        enum Material { none, skirtM, bodyM, insertM };

        // Height (in knob radii) and material at radius d (0..1) and angle a.
        float heightAt (const KnobBody& k, float d, float a, Material& m)
        {
            m = none;
            if (d > k.skirtRadius)
                return 0.0f;

            // Skirt: a low disc with a rounded rim, knurled if asked.
            auto h = k.skirtHeight * (1.0f - smooth (k.skirtRadius - 0.05f, k.skirtRadius, d));
            m = skirtM;
            if (k.skirtKnurls > 0 && d > k.bodyRadius)
                h -= k.knurlDepth * std::abs (std::sin (0.5f * (float) k.skirtKnurls * a));

            if (d <= k.bodyRadius)
            {
                // Body: rolls over at its edge, then a gentle dome.
                const auto rim = 1.0f - smooth (k.bodyRadius - 0.07f, k.bodyRadius, d);
                const auto q = d / k.bodyRadius;
                h = k.skirtHeight + k.bodyHeight * rim + k.dome * (1.0f - q * q) * rim;
                m = bodyM;
                if (k.bodyFlutes > 0 && d > k.insertRadius + 0.04f)
                    h -= k.knurlDepth * smooth (k.insertRadius, k.insertRadius + 0.1f, d)
                         * std::pow (std::abs (std::cos (0.5f * (float) k.bodyFlutes * a)), 6.0f);
                if (d <= k.insertRadius)
                {
                    h -= 0.015f;  // set slightly into the cap
                    m = insertM;
                }
            }
            return h;
        }
    }

    juce::Image KnobRenderer::body (int styleKey, const KnobBody& k, int pixelSize)
    {
        static std::map<std::pair<int, int>, juce::Image> cache;
        const auto key = std::make_pair (styleKey, pixelSize);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;

        const auto n = juce::jmax (8, pixelSize);
        juce::Image img (juce::Image::ARGB, n, n, true);
        juce::Image::BitmapData bits (img, juce::Image::BitmapData::writeOnly);

        const auto radius = 0.5f * (float) n;
        const auto light = normalised ({ -0.45f, -0.65f, 0.62f });
        const auto half = normalised ({ light.x, light.y, light.z + 1.0f });
        const auto lightAzimuth = std::atan2 (light.y, light.x);
        const auto e = 1.0f / radius;  // one pixel, in knob radii
        const auto slope = 2.2f;        // exaggerates relief a little, as the eye expects at this size

        juce::Random noise (1234);
        for (int py = 0; py < n; ++py)
            for (int px = 0; px < n; ++px)
            {
                // 2 x 2 supersampling for clean edges.
                float rr = 0, gg = 0, bb = 0, cover = 0;
                for (int s = 0; s < 4; ++s)
                {
                    const auto x = ((float) px + 0.25f + 0.5f * (float) (s & 1) - radius) / radius;
                    const auto y = ((float) py + 0.25f + 0.5f * (float) (s >> 1) - radius) / radius;
                    const auto d = std::sqrt (x * x + y * y);
                    const auto a = std::atan2 (y, x);
                    Material m;
                    const auto h = heightAt (k, d, a, m);
                    if (m == none)
                        continue;

                    Material dummy;
                    auto hAt = [&] (float xx, float yy) { return heightAt (k, std::sqrt (xx * xx + yy * yy), std::atan2 (yy, xx), dummy); };
                    const auto dx = (hAt (x + e, y) - hAt (x - e, y)) / (2.0f * e);
                    const auto dy = (hAt (x, y + e) - hAt (x, y - e)) / (2.0f * e);
                    const auto normal = normalised ({ -dx * slope, -dy * slope, 1.0f });
                    juce::ignoreUnused (h);

                    const auto albedo = m == skirtM ? k.skirt : m == bodyM ? k.body : k.insert;
                    const auto diffuse = juce::jmax (0.0f, dot (normal, light));
                    auto spec = k.gloss * std::pow (juce::jmax (0.0f, dot (normal, half)), k.shininess);

                    if (m == insertM && k.brushedInsert)
                    {
                        // Spun aluminium: the grooves run round the centre, so the
                        // highlight is a pair of bright wedges along the light's
                        // azimuth, broken up by fine concentric rings.
                        const auto along = std::abs (std::cos (a - lightAzimuth));
                        const auto rings = 0.85f + 0.15f * std::sin (d * radius * 3.1f + 7.0f * std::sin (d * 40.0f));
                        spec = 0.55f * std::pow (along, 18.0f) * rings + 0.12f * along;
                    }

                    const auto shade = 0.28f + 0.8f * diffuse;
                    rr += juce::jmin (1.0f, albedo.getFloatRed() * shade + spec);
                    gg += juce::jmin (1.0f, albedo.getFloatGreen() * shade + spec);
                    bb += juce::jmin (1.0f, albedo.getFloatBlue() * shade + spec);
                    cover += 1.0f;
                }
                if (cover <= 0.0f)
                    continue;
                const auto grain = 1.0f + 0.02f * (noise.nextFloat() - 0.5f);
                const auto alpha = cover / 4.0f;
                bits.setPixelColour (px, py, juce::Colour::fromFloatRGBA (juce::jmin (1.0f, rr / cover * grain),
                                                                          juce::jmin (1.0f, gg / cover * grain),
                                                                          juce::jmin (1.0f, bb / cover * grain), alpha));
            }

        cache[key] = img;
        return img;
    }
}
