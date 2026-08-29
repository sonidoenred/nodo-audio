#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <nodo_core/nodo_core.h>

namespace nodo::delay
{
/** How the two lines are wired to each other and to the input. FROZEN ORDER. */
enum class Routing
{
    /** Left in feeds the left line, right in feeds the right line. The cross
        control decides how much each line hands to the other, so this covers
        everything from two unrelated delays to a full bounce.
    */
    stereo = 0,

    /** The input is summed and goes into the left line only; each line then
        feeds the other. This is the classic bounce, and it is a routing rather
        than an amount — which is why it is here and not at the top of the cross
        knob.
    */
    pingPong,

    /** The input is summed and feeds both lines. With different times on the
        two lines this is where the useful asymmetric patterns live.
    */
    mono
};

/** What happens when the delay time changes. FROZEN ORDER. */
enum class TimeMode
{
    /** The read head slides to the new distance, so the tail bends in pitch on
        the way — because that is what happens when you change the speed of a
        machine that has tape moving through it. This is the mode people mean
        when they say a delay "sounds analogue".
    */
    tape = 0,

    /** The read head jumps and the old and new positions are crossfaded. No
        pitch bend, no glide: the repeats simply arrive at the new spacing. What
        you want when the delay time is automated to follow an arrangement.
    */
    fade
};

/** Note values for tempo sync. FROZEN ORDER — the index is saved. */
enum class NoteValue
{
    sixteenthTriplet = 0,
    sixteenth,
    sixteenthDotted,
    eighthTriplet,
    eighth,
    eighthDotted,
    quarterTriplet,
    quarter,
    quarterDotted,
    half,
    halfDotted,
    whole
};

inline constexpr int numNoteValues = 12;

juce::StringArray routingNames();
juce::StringArray timeModeNames();
juce::StringArray noteValueNames();

/** How many beats one repeat of this note value lasts. A beat is a quarter,
    which is what every host means by BPM.
*/
double noteValueInBeats (NoteValue value);

inline constexpr int numTaps = 16;
inline constexpr int numLfos = 2;
inline constexpr int numModSlots = 6;

/** One step of the rhythmic pattern. */
struct TapStep
{
    bool on { false };
    float level { 1.0f };
    float pan { 0.0f };
};

/** Where a modulation slot gets its number from. FROZEN ORDER. */
enum class ModSource { none = 0, lfo1, lfo2, envelope };

/** What a modulation slot points at. FROZEN ORDER — add at the end, never
    reorder, the index is written into every saved session.

    A curated list rather than "any parameter". Half of the targets in a
    fifty-slot matrix are things nobody modulates, and every one of them is a
    row you have to read past to find the one you wanted.
*/
enum class ModTarget
{
    none = 0,
    time,            ///< both lines together — the chorus and flanger case
    timeLeft,
    timeRight,
    feedback,
    cross,
    drive,
    loFi,
    loopHighpass,
    loopLowpass,
    mix,
    width,
    tapSpread,
    tapLevel
};

/** One past the last target. Used to size the per-sample accumulator, so it has
    to grow with the enum above.
*/
inline constexpr int numModTargets = (int) ModTarget::tapLevel + 1;

juce::StringArray modSourceNames();
juce::StringArray modTargetNames();

struct ModSlot
{
    ModSource source { ModSource::none };
    ModTarget target { ModTarget::none };
    float amount { 0.0f };        ///< -1 to 1
};

struct LfoSettings
{
    float rateHz { 1.0f };
    bool sync { false };
    NoteValue note { NoteValue::quarter };
    dsp::Lfo::Shape shape { dsp::Lfo::Shape::sine };
    float phase { 0.0f };
};

juce::StringArray lfoShapeNames();

struct DelaySettings
{
    /** Free running times, in milliseconds. Ignored when sync is on. */
    float timeLeftMs  { 375.0f };
    float timeRightMs { 500.0f };

    bool  sync { false };
    NoteValue noteLeft  { NoteValue::eighthDotted };
    NoteValue noteRight { NoteValue::quarter };

    /** The right line copies the left. Kept as a switch rather than as a
        convention, because two lines at the same time is the single most common
        setting and hunting two knobs to the same number is a chore.
    */
    bool linkTimes { false };

    float feedback { 0.35f };     ///< 0 to 1.1; past 1 the loop sustains
    float cross    { 0.0f };      ///< how much of each line feeds the other
    bool  invertFeedback { false };
    bool  freeze { false };

    float highpassHz { 20.0f };   ///< at the minimum the filter is bypassed
    float lowpassHz  { 20000.0f };///< at the maximum the filter is bypassed

    float drive { 0.0f };         ///< 0 to 1
    float loFi  { 0.0f };         ///< 0 to 1

    float modRateHz { 0.5f };
    float modDepth  { 0.0f };     ///< 0 to 1

    Routing routing { Routing::stereo };
    TimeMode timeMode { TimeMode::tape };

    float panLeft  { -1.0f };     ///< -1 hard left, +1 hard right
    float panRight {  1.0f };
    float width { 1.0f };         ///< 0 mono wet, 1 as is, 2 wider
    float mix { 0.35f };

    // ---- multi-tap ---------------------------------------------------------

    /** When off, everything below is ignored and the plugin behaves exactly as
        it did before any of it existed. That is a property worth having and it
        is pinned by a test.
    */
    bool multiTap { false };

    /** How many steps the delay time is divided into. The last step lands on
        the delay time itself, which is where the feedback happens, so at a
        division of one a single tap is the plain delay and nothing has changed.
    */
    int division { 4 };

    std::array<TapStep, numTaps> taps {};

    /** Rotates every tap's position in the picture, so the whole pattern can be
        narrowed or swung to one side without touching sixteen controls.
    */
    float tapSpread { 1.0f };

    // ---- modulation --------------------------------------------------------

    std::array<LfoSettings, numLfos> lfos {};
    float envelopeAttackMs { 10.0f };
    float envelopeReleaseMs { 200.0f };
    std::array<ModSlot, numModSlots> mod {};

    /** Filled in by the processor from the host, not by a parameter. */
    double bpm { 120.0 };
};

namespace ids
{
    inline const juce::String bypass     { "bypass" };
    inline const juce::String timeLeft   { "timeLeft" };
    inline const juce::String timeRight  { "timeRight" };
    inline const juce::String sync       { "sync" };
    inline const juce::String noteLeft   { "noteLeft" };
    inline const juce::String noteRight  { "noteRight" };
    inline const juce::String linkTimes  { "linkTimes" };
    inline const juce::String feedback   { "feedback" };
    inline const juce::String cross      { "cross" };
    inline const juce::String invert     { "invert" };
    inline const juce::String freeze     { "freeze" };
    inline const juce::String highpass   { "highpass" };
    inline const juce::String lowpass    { "lowpass" };
    inline const juce::String drive      { "drive" };
    inline const juce::String loFi       { "loFi" };
    inline const juce::String modRate    { "modRate" };
    inline const juce::String modDepth   { "modDepth" };
    inline const juce::String routing    { "routing" };
    inline const juce::String timeMode   { "timeMode" };
    inline const juce::String panLeft    { "panLeft" };
    inline const juce::String panRight   { "panRight" };
    inline const juce::String width      { "width" };
    inline const juce::String mix        { "mix" };

    // Added in phase two. Appended, never inserted: the order of the parameter
    // list is not frozen but the identifiers are, and a session saved before
    // any of this existed has to keep opening.
    inline const juce::String multiTap   { "multiTap" };
    inline const juce::String division   { "division" };
    inline const juce::String tapSpread  { "tapSpread" };
    inline const juce::String envAttack  { "envAttack" };
    inline const juce::String envRelease { "envRelease" };

    /** index is 1-based, to match what the interface shows. */
    juce::String tapOn (int index);
    juce::String tapLevel (int index);
    juce::String tapPan (int index);

    juce::String lfoRate (int lfo);
    juce::String lfoSync (int lfo);
    juce::String lfoNote (int lfo);
    juce::String lfoShape (int lfo);
    juce::String lfoPhase (int lfo);

    juce::String modSource (int slot);
    juce::String modTarget (int slot);
    juce::String modAmount (int slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
DelaySettings readSettings (juce::AudioProcessorValueTreeState& state);

/** The time each line actually runs at, in milliseconds, once sync and link
    have had their say. One function so the engine, the display and the tests
    can never disagree about it.
*/
float resolvedTimeMs (const DelaySettings& settings, int line);

inline bool loopFiltersActive (const DelaySettings& s) noexcept
{
    return s.highpassHz > 20.5f || s.lowpassHz < 19500.0f;
}

/** The rate an LFO actually runs at, once sync has had its say. */
float resolvedLfoRateHz (const DelaySettings& settings, int lfo);

/** True when the modulation section is doing nothing at all, so the engine can
    skip it rather than adding zero to everything once per sample.
*/
bool modulationActive (const DelaySettings& settings) noexcept;

/** How many steps are really in play. Below the division the pattern is not
    drawn and not read.
*/
inline int activeSteps (const DelaySettings& s) noexcept
{
    return s.multiTap ? juce::jlimit (1, numTaps, s.division) : 1;
}
} // namespace nodo::delay
