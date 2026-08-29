/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 nodo_core
  vendor:             SonidoenRed
  version:            0.1.0
  name:               Nodo Core
  description:        Shared DSP, parameter and state utilities for the Nodo plugin suite.
  website:            https://sonidoenred.com
  license:            Proprietary
  minimumCppStandard: 20

  dependencies:       juce_audio_processors, juce_dsp

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "params/ParamFormat.h"
#include "dsp/Biquad.h"
#include "dsp/MatchedBiquad.h"
#include "dsp/ButterworthQ.h"
#include "dsp/Dynamics.h"
#include "dsp/Lookahead.h"
#include "dsp/FractionalDelay.h"
#include "dsp/Saturation.h"
#include "dsp/Modulation.h"
#include "dsp/Allpass.h"
#include "dsp/FeedbackMatrix.h"
#include "dsp/Loudness.h"
#include "dsp/SpectrumAnalyser.h"
#include "dsp/LevelFollower.h"
#include "state/StateVersion.h"
#include "presets/PresetManager.h"
