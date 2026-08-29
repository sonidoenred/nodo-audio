/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 nodo_ui
  vendor:             SonidoenRed
  version:            0.1.0
  name:               Nodo UI
  description:        Shared look and feel, widgets and header bar for the Nodo plugin suite.
  website:            https://sonidoenred.com
  license:            Proprietary
  minimumCppStandard: 20

  dependencies:       juce_gui_basics, nodo_core

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <nodo_core/nodo_core.h>

#include "theme/NodoTheme.h"
#include "theme/NodoLookAndFeel.h"
#include "widgets/NodoSlider.h"
#include "widgets/NodoKnob.h"
#include "widgets/NodoSlimSlider.h"
#include "widgets/NodoMeter.h"
#include "widgets/NodoHeader.h"
