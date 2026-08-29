#include "VerbEngine.h"

namespace nodo::verb
{
namespace
{
    constexpr float maximumPredelayMs = 500.0f;
    constexpr float maximumSize = 4.0f;

    /** How far the line lengths wander at full motion, as a fraction of their
        length. Small on purpose: this is here to stop eight fixed delays
        ringing on eight fixed frequencies, not to be a chorus.
    */
    constexpr float maximumMotion = 0.004f;

    /** How fast the read heads travel when the size changes. Slow enough that
        the tail bends rather than clicks; a reverb whose size is being swept is
        supposed to sound like the room is moving.
    */
    constexpr float sizeGlideSeconds = 0.25f;

    /** The allpass lengths in front of the network, in milliseconds at a size of
        one. Mutually prime-ish and short — a diffuser wants to be shorter than
        the shortest line in the network or it starts sounding like the reverb
        rather than like the way in to it.
    */
    constexpr float diffuserMs[4] { 4.77f, 6.91f, 10.13f, 14.53f };

    /** The right channel's diffuser runs slightly different lengths. Feeding
        two identical diffusers from a mono source gives a tail that is
        identical on both sides, which is a mono reverb with extra steps.
    */
    constexpr float diffuserSpread = 1.11f;
}

void VerbEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = (int) juce::jmax (1u, spec.maximumBlockSize);

    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];

        const auto longest = lineLengthSamples (i, maximumSize, sampleRate);
        line.buffer.prepare (longest + 16);
        line.currentSamples = (float) lineLengthSamples (i, 1.0f, sampleRate);
        line.targetSamples = line.currentSamples;

        // Spread the movement of the eight lines around the circle so they do
        // not all lengthen and shorten together, which would be a pitch wobble
        // rather than a stirred tail.
        line.lfoPhase = (float) i / (float) numLines;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& diffuser = diffusers[(size_t) ch];
        const auto spread = ch == 0 ? 1.0f : diffuserSpread;

        for (int i = 0; i < 4; ++i)
        {
            const auto samples = diffuserMs[i] * spread * 0.001f * (float) sampleRate;
            diffuser.lengths[(size_t) i] = samples;
            diffuser.stages[(size_t) i].prepare ((int) std::ceil (samples * 1.2f) + 8);
            diffuser.phases[(size_t) i] = 0.37f * (float) i + 0.5f * (float) ch;
        }

        predelay[(size_t) ch].prepare ((int) std::ceil (maximumPredelayMs * 0.001 * sampleRate) + 8);
    }

    duckEnvelope.prepare (sampleRate);
    duckEnvelope.setRange (-6.0f, 40.0f);

    mixAmount.reset (sampleRate, 0.02);
    widthAmount.reset (sampleRate, 0.02);

    motionIncrement = (float) (0.13 / sampleRate);   // a slow stir, not a vibrato
    sizeGlideCoeff = std::exp (-1.0f / (sizeGlideSeconds * (float) sampleRate));
    levelCoeff = std::exp (-1.0f / (0.1f * (float) sampleRate));

    reset();
}

void VerbEngine::reset()
{
    for (auto& line : lines)
        line.reset();

    for (auto& diffuser : diffusers)
        diffuser.reset();

    for (auto& delay : predelay)
        delay.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        preLowCut[(size_t) ch].reset();
        preHighCut[(size_t) ch].reset();
        postLow[(size_t) ch].reset();
        postMid[(size_t) ch].reset();
        postHigh[(size_t) ch].reset();
    }

    duckEnvelope.reset();
    tailFollower = 0.0f;
    settingsValid = false;

    duckingDb.store (0.0f, std::memory_order_relaxed);
    tailLevel.store (0.0f, std::memory_order_relaxed);
}

void VerbEngine::applyPendingSettings()
{
    settings = pending;

    // ---- the damping filters, which are the decay -------------------------
    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[(size_t) i];

        line.targetSamples = (float) lineLengthSamples (i, settings.size, sampleRate);

        /*  Frozen, every line is handed back exactly what it gave: unity gain,
            no shelves. Anything less and a held chord fades; anything more and
            it grows. It has to be exactly one, which is why it is written as a
            special case rather than as a very long decay time.
        */
        if (settings.freeze)
        {
            line.gain = 1.0f;
            line.lowShelf.setCoefficients ({});
            line.highShelf.setCoefficients ({});
            continue;
        }

        const auto damping = designLineDamping (settings.decay,
                                                (int) std::round (line.targetSamples),
                                                sampleRate);

        line.gain = damping.broadbandGain;
        line.lowShelf.setCoefficients (damping.lowShelf);
        line.highShelf.setCoefficients (damping.highShelf);
    }

    // ---- the diffusers ----------------------------------------------------
    const auto diffusionAmount = juce::jlimit (0.0f, 1.0f, settings.diffusion);

    for (auto& diffuser : diffusers)
        for (auto& stage : diffuser.stages)
            stage.setCoefficient (0.82f * diffusionAmount);

    // ---- filters in and out -----------------------------------------------
    preFiltersActive = settings.preLowCutHz > 20.5f || settings.preHighCutHz < 19500.0f;

    if (preFiltersActive)
    {
        const auto hp = dsp::BiquadCoefficients::highPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.preLowCutHz), 0.707);
        const auto lp = dsp::BiquadCoefficients::lowPass (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.preHighCutHz), 0.707);

        for (int ch = 0; ch < 2; ++ch)
        {
            preLowCut[(size_t) ch].setCoefficients (hp);
            preHighCut[(size_t) ch].setCoefficients (lp);
        }
    }

    postActive = postEqActive (settings);

    if (postActive)
    {
        const auto low = dsp::BiquadCoefficients::lowShelf (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.postLowFreq),
            0.707, settings.postLowGain);
        const auto mid = dsp::BiquadCoefficients::peak (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.postMidFreq),
            juce::jmax (0.1f, settings.postMidQ), settings.postMidGain);
        const auto high = dsp::BiquadCoefficients::highShelf (
            sampleRate, juce::jlimit (20.0, sampleRate * 0.45, (double) settings.postHighFreq),
            0.707, settings.postHighGain);

        for (int ch = 0; ch < 2; ++ch)
        {
            postLow[(size_t) ch].setCoefficients (low);
            postMid[(size_t) ch].setCoefficients (mid);
            postHigh[(size_t) ch].setCoefficients (high);
        }
    }

    duckEnvelope.setTimes (settings.duckAttackMs, settings.duckReleaseMs);

    predelaySamples = juce::jlimit (0.0f, maximumPredelayMs, settings.predelayMs)
                        * 0.001f * (float) sampleRate;

    const auto mixTarget = juce::jlimit (0.0f, 1.0f, settings.mix);
    const auto widthTarget = juce::jlimit (0.0f, 2.0f, settings.width);

    // As everywhere else in the suite: a SmoothedValue starts at zero, so
    // ramping on the first block would fade the plugin in from silence every
    // time the transport starts.
    if (settingsValid)
    {
        mixAmount.setTargetValue (mixTarget);
        widthAmount.setTargetValue (widthTarget);
    }
    else
    {
        mixAmount.setCurrentAndTargetValue (mixTarget);
        widthAmount.setCurrentAndTargetValue (widthTarget);

        for (auto& line : lines)
            line.currentSamples = line.targetSamples;
    }

    settingsValid = true;
}

void VerbEngine::process (juce::dsp::AudioBlock<float>& block)
{
    applyPendingSettings();

    const auto numSamples = (int) block.getNumSamples();
    const auto channels = (int) juce::jmin ((size_t) 2, block.getNumChannels());

    if (numSamples <= 0 || channels <= 0)
        return;

    std::array<float*, 2> audio { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
        audio[(size_t) ch] = block.getChannelPointer ((size_t) ch);

    const auto mono = channels < 2;
    const auto motion = juce::jlimit (0.0f, 1.0f, settings.motion);
    const auto duckAmount = juce::jlimit (0.0f, 1.0f, settings.duckAmount);
    const auto frozen = settings.freeze;

    // Each line contributes to the two outputs with alternating signs, which
    // decorrelates the two sides without a second network.
    const auto outputScale = 1.0f / std::sqrt ((float) numLines);

    auto worstDuckDb = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto dryL = audio[0][n];
        const auto dryR = mono ? dryL : audio[1][n];

        // ---- the way in ---------------------------------------------------
        std::array<float, 2> injected { 0.0f, 0.0f };

        for (int ch = 0; ch < channels; ++ch)
        {
            auto value = ch == 0 ? dryL : dryR;

            if (preFiltersActive)
            {
                value = preLowCut[(size_t) ch].processSample (value);
                value = preHighCut[(size_t) ch].processSample (value);
            }

            predelay[(size_t) ch].write (value);
            const auto delayed = predelaySamples > 1.0f
                               ? predelay[(size_t) ch].read (predelaySamples)
                               : value;
            predelay[(size_t) ch].advance();

            auto& diffuser = diffusers[(size_t) ch];
            auto smeared = delayed;

            for (int i = 0; i < 4; ++i)
            {
                diffuser.phases[(size_t) i] += motionIncrement * 1.7f;

                if (diffuser.phases[(size_t) i] >= 1.0f)
                    diffuser.phases[(size_t) i] -= std::floor (diffuser.phases[(size_t) i]);

                const auto wobble = std::sin (diffuser.phases[(size_t) i]
                                              * juce::MathConstants<float>::twoPi);
                const auto length = diffuser.lengths[(size_t) i]
                                  * (1.0f + motion * maximumMotion * wobble);

                smeared = diffuser.stages[(size_t) i].process (smeared, length);
            }

            injected[(size_t) ch] = frozen ? 0.0f : smeared;
        }

        if (mono)
            injected[1] = injected[0];

        // ---- the network ---------------------------------------------------
        std::array<float, numLines> taken {};

        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];

            // The size slides rather than jumps: the read head travelling
            // through the buffer bends the tail, which is what a room being
            // resized ought to sound like.
            line.currentSamples = line.targetSamples
                                + sizeGlideCoeff * (line.currentSamples - line.targetSamples);

            line.lfoPhase += motionIncrement * (1.0f + 0.13f * (float) i);

            if (line.lfoPhase >= 1.0f)
                line.lfoPhase -= std::floor (line.lfoPhase);

            const auto wobble = std::sin (line.lfoPhase * juce::MathConstants<float>::twoPi);
            const auto distance = line.currentSamples * (1.0f + motion * maximumMotion * wobble);

            auto value = line.buffer.read (distance);

            /*  The damping filter *is* the decay: this is the only place the
                level of what goes round is decided, and making it a filter is
                what makes the decay time a function of frequency rather than
                one number.
            */
            if (! frozen)
            {
                value = line.lowShelf.processSample (value);
                value = line.highShelf.processSample (value);
                value *= line.gain;
            }

            taken[(size_t) i] = value;
        }

        // Energy preserving, so nothing here changes how fast it decays.
        auto mixed = taken;
        dsp::hadamardInPlace<numLines> (mixed);

        for (int i = 0; i < numLines; ++i)
        {
            // Alternate which side each line is fed from, so the two channels
            // put different energy into the network.
            const auto source = (i % 2 == 0) ? injected[0] : injected[1];

            /*  No safety clipper in here, unlike the delay's loop.

                A feedback delay network with an orthogonal matrix and a
                per-line gain below one is provably stable — there is nothing to
                protect against, and a clipper would be an audible defect rather
                than a safeguard. What does happen is that a long decay builds
                up on sustained input: the energy settles at 1/(1-g squared),
                which for a twenty second tail is about seventeen decibels above
                the input. That is what a cathedral does, floats have room for
                it, and the mix control is where it is dealt with.
            */
            const auto written = mixed[(size_t) i] + source;

            lines[(size_t) i].buffer.write (std::isfinite (written) ? written : 0.0f);
            lines[(size_t) i].buffer.advance();
        }

        // ---- the way out ---------------------------------------------------
        auto wetL = 0.0f, wetR = 0.0f;

        for (int i = 0; i < numLines; ++i)
        {
            const auto value = taken[(size_t) i];

            /*  Both sides hear all eight lines, with two different sign
                patterns rather than four lines each. The patterns are two rows
                of the same Hadamard matrix, so they are orthogonal and the two
                outputs are decorrelated — which is what makes it wide — while
                each side still gets the full density of the network.
            */
            wetL += (i & 1) ? -value : value;
            wetR += (i & 2) ? -value : value;
        }

        wetL *= outputScale;
        wetR *= outputScale;

        const auto magnitude = std::abs (wetL) + std::abs (wetR);
        tailFollower = magnitude + levelCoeff * (tailFollower - magnitude);

        if (postActive)
        {
            wetL = postHigh[0].processSample (postMid[0].processSample (postLow[0].processSample (wetL)));
            wetR = postHigh[1].processSample (postMid[1].processSample (postLow[1].processSample (wetR)));
        }

        const auto width = widthAmount.getNextValue();
        const auto mid = (wetL + wetR) * 0.5f;
        const auto side = (wetL - wetR) * 0.5f * width;

        wetL = mid + side;
        wetR = mid - side;

        /*  Ducking. The tail is pulled down while the source is playing and
            comes back in the gaps, which is how a long reverb goes on a voice
            without the words disappearing into it. The envelope reads the dry
            input, not the wet: what should decide is what is being sung, not
            what the reverb is doing about it.
        */
        if (duckAmount > 0.0f)
        {
            const auto level = duckEnvelope.process ((dryL + dryR) * 0.5f);
            const auto duck = 1.0f - duckAmount * level;

            wetL *= duck;
            wetR *= duck;

            const auto db = 20.0f * std::log10 (juce::jmax (1.0e-4f, duck));
            worstDuckDb = juce::jmin (worstDuckDb, db);
        }

        const auto wet = mixAmount.getNextValue();
        const auto dry = 1.0f - wet;

        audio[0][n] = dryL * dry + wetL * wet;

        if (! mono)
            audio[1][n] = dryR * dry + wetR * wet;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        preLowCut[(size_t) ch].snapToZero();
        preHighCut[(size_t) ch].snapToZero();
        postLow[(size_t) ch].snapToZero();
        postMid[(size_t) ch].snapToZero();
        postHigh[(size_t) ch].snapToZero();
    }

    for (auto& line : lines)
    {
        line.lowShelf.snapToZero();
        line.highShelf.snapToZero();
    }

    duckingDb.store (worstDuckDb, std::memory_order_relaxed);
    tailLevel.store (tailFollower, std::memory_order_relaxed);
}
} // namespace nodo::verb
