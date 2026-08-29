#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

namespace nodo::theme
{
/** The single source of truth for how the suite looks.

    Every colour, radius and font size in every plugin resolves to a token in
    here. Change the accent once and Nodo EQ, Comp, Delay and Verb all change
    together; that is the whole reason the suite reads as one product rather than
    four plugins that happen to share a logo.

    The accent is SonidoenRed's purple, the same one used on the downloadable
    guides, so the plugins and the ebooks are visibly from the same place.
*/
namespace colours
{
    inline const juce::Colour background   { 0xff14131a };
    inline const juce::Colour panel        { 0xff1d1b25 };
    inline const juce::Colour panelRaised  { 0xff262331 };
    inline const juce::Colour grid         { 0xff2c2937 };
    inline const juce::Colour gridStrong   { 0xff3a3648 };

    inline const juce::Colour text         { 0xffe8e4ee };
    inline const juce::Colour textDim      { 0xff8b8695 };
    inline const juce::Colour textFaint    { 0xff5d5869 };

    /** Each plugin in the suite gets its own accent, and every widget reads it
        from here rather than from a constant. That is why these are functions:
        the value is set once when a plugin is constructed, and the shared
        header, knobs, meters and curves all follow without knowing which plugin
        they are inside.

        The family is built by rotating the hue of SonidoenRed's purple and
        keeping the saturation fixed, with the brightness of each hue chosen so
        that none of them reads heavier or fainter than the others against the
        background. Equal HSV value would have made the teal and the green shout
        over the purple; equal contrast would have turned them to mud. These sit
        halfway between the two, which is where they stop competing and start
        looking like one set.
    */
    struct AccentPalette
    {
        juce::Colour base, bright, dim;
    };

    inline AccentPalette& accentPalette()
    {
        // Defaults to the brand purple: a plugin that forgets to set its accent
        // still looks like part of the suite.
        static AccentPalette palette { juce::Colour (0xff6c4ac7),
                                       juce::Colour (0xff9e7ef7),
                                       juce::Colour (0xff301f60) };
        return palette;
    }

    /** Called once per plugin, before any editor exists. */
    inline void setAccent (juce::Colour base, juce::Colour bright, juce::Colour dim)
    {
        accentPalette() = { base, bright, dim };
    }

    inline void setAccent (const AccentPalette& palette) { accentPalette() = palette; }

    inline juce::Colour accent()       { return accentPalette().base; }
    inline juce::Colour accentBright() { return accentPalette().bright; }
    inline juce::Colour accentDim()    { return accentPalette().dim; }

    /** The accents of the whole suite, including the ones whose plugins are not
        written yet, so the family is decided in one place rather than drifting
        as each new plugin picks a colour it likes.
    */
    namespace suite
    {
        inline const AccentPalette eq    { juce::Colour (0xff6c4ac7), juce::Colour (0xff9e7ef7), juce::Colour (0xff301f60) };
        inline const AccentPalette comp  { juce::Colour (0xffa66e3e), juce::Colour (0xffcd9869), juce::Colour (0xff50331a) };
        inline const AccentPalette limit { juce::Colour (0xffb74457), juce::Colour (0xffe37486), juce::Colour (0xff581c26) };
        inline const AccentPalette ess   { juce::Colour (0xff399a86), juce::Colour (0xff61bfac), juce::Colour (0xff184a40) };
        inline const AccentPalette gate  { juce::Colour (0xff609a39), juce::Colour (0xff87bf61), juce::Colour (0xff2c4a18) };
        inline const AccentPalette delay { juce::Colour (0xff4076ad), juce::Colour (0xff6da2d6), juce::Colour (0xff1b3753) };
        inline const AccentPalette verb  { juce::Colour (0xffb142b1), juce::Colour (0xffdb70db), juce::Colour (0xff551b55) };
    }

    /** The wordmark keeps the brand purple whatever the plugin's accent is: the
        logo is SonidoenRed's, the accent is the plugin's.
    */
    inline const juce::Colour wordmark { 0xff6c4ac7 };

    inline const juce::Colour warning      { 0xffe0a04c };
    inline const juce::Colour danger       { 0xffe05c6e };

    inline const juce::Colour analyserFill { 0xff2f2b3d };
    inline const juce::Colour curve        { 0xffe8e4ee };

    /** The response curve is filled towards the zero line in the plugin's
        accent. A thin white line on near-black reads as an oscilloscope; the
        fill is what makes an EQ curve look like a decision rather than a
        measurement.
    */
    inline juce::Colour curveFill() { return accent(); }

    /** Metering. Green is deliberately absent: the bar is the brand accent until
        the signal is close to the ceiling, so a normal level does not look like
        an alarm state.
    */
    inline juce::Colour meterNormal() { return accentBright(); }
    inline const juce::Colour meterHot     { 0xffe0a04c };
    inline const juce::Colour meterOver    { 0xffe05c6e };
    inline const juce::Colour meterTrack   { 0xff221f2c };

    /** Per-band identity colours. Six hues that stay distinguishable on a dark
        background and never collide with the accent used for selection.
    */
    inline const std::array<juce::Colour, 6> band {
        juce::Colour (0xffe05c6e),   // 1 red
        juce::Colour (0xffe08a4c),   // 2 orange
        juce::Colour (0xffe0c64c),   // 3 yellow
        juce::Colour (0xff6bc98a),   // 4 green
        juce::Colour (0xff4ca8e0),   // 5 blue
        juce::Colour (0xffa96be0)    // 6 violet
    };

    /** Band colours cycle through the palette. Past the sixth band the same six
        hues come back a shade brighter, so band 1 and band 7 are related but
        still tellable apart on the curve.
    */
    inline juce::Colour forBand (int index)
    {
        const auto safe = juce::jmax (0, index);
        const auto hue = band[(size_t) (safe % (int) band.size())];
        const auto cycle = safe / (int) band.size();

        return cycle == 0 ? hue : hue.brighter (juce::jmin (0.45f, 0.18f * (float) cycle));
    }
}

namespace metrics
{
    inline constexpr float cornerRadius   = 4.0f;
    inline constexpr float headerHeight   = 38.0f;
    inline constexpr float footerHeight   = 182.0f;
    inline constexpr float padding        = 10.0f;
    inline constexpr float knobLabelH     = 14.0f;
    inline constexpr float knobValueH     = 15.0f;
}

namespace fonts
{
    inline juce::Font regular (float height)
    {
        return juce::Font (juce::FontOptions().withHeight (height));
    }

    inline juce::Font bold (float height)
    {
        return juce::Font (juce::FontOptions().withHeight (height).withStyle ("Bold"));
    }

    /** Used for numeric readouts so digits do not jitter as values change. */
    inline juce::Font mono (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height,
                                              juce::Font::plain));
    }
}
} // namespace nodo::theme
