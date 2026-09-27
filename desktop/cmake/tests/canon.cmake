# The shared config the desktop reads: LLM settings, motion prefs, the qrc canon, interaction
# timings, the logo stage, the canon assets and the webcore skin.

# LLM settings defaults + the fileStore JSON round-trip of the §5 llm* keys and the dock blob.
stencil_headless_test(stencil_llmsettings_headless
  SOURCES tests/llm/client/llmSettings.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# Motion preferences (support/motionPrefs.hpp): the interface modes, the drawing switch, the
# particle gate and the two settings keys.
stencil_headless_test(stencil_motionprefs_headless
  SOURCES src/support/icon/motionIcons.cpp
    ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/support/motionPrefs.headless.cpp tests/support/motionPrefsStyle.headless.cpp
    tests/support/motionPrefsGate.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The qrc-embedded browser JSON parses and matches what theme / iconSet / fileStore / the LLM
# client consume — a broken app.qrc alias fails here fast.
stencil_headless_test(stencil_configcanon_headless
  SOURCES tests/support/theme/configCanon.headless.cpp tests/support/theme/configCanonLlm.headless.cpp
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    src/support/icon/iconSet.cpp ${STENCIL_THEME_SOURCES} src/support/logo/logoStageRules.cpp
    src/support/logo/logoStageConfig.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The shared interaction timings (support/uiTimings.hpp) against constants.json and motion.json FLIP_*.
stencil_headless_test(stencil_uitimings_headless
  SOURCES tests/support/uiTimings.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# The drawing and highlight defaults (support/theme/defaultVisuals.hpp) against constants.json.
stencil_headless_test(stencil_defaultvisuals_headless
  SOURCES tests/support/theme/defaultVisuals.headless.cpp ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets)

# The line thickness and point-size ranges (support/control/lineLimits.hpp) against constants.json.
stencil_headless_test(stencil_linelimits_headless
  SOURCES tests/support/control/lineLimits.headless.cpp resources/app.qrc
  LIBS Qt6::Core)

# The logo stage's table and kinematics against the browser's printed values.
stencil_headless_test(stencil_logostagerules_headless
  SOURCES tests/support/logo/logoStageRules.headless.cpp ${STENCIL_LOGOSTAGE_SOURCES}
    ${STENCIL_DUSTKIT_SOURCES} ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets
  INCLUDE_TESTS)
stencil_headless_test(stencil_logostagecloud_headless
  SOURCES tests/support/logo/logoStageCloud.headless.cpp ${STENCIL_LOGOSTAGE_SOURCES}
    ${STENCIL_DUSTKIT_SOURCES} ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets
  INCLUDE_TESTS)

# Drift guards for the assets the desktop reads instead of embedding: theme tokens, the app QSS
# token map, mediaTypes.json and the prompt canon's context suffixes.
stencil_headless_test(stencil_canonassets_headless
  SOURCES tests/support/theme/canonAssets.headless.cpp src/app/open/launchOptions.cpp ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp src/io/mediaTypes.cpp ${STENCIL_OPREGISTRY_SOURCES} ${STENCIL_OPSCHEMA_SOURCES}
    ${STENCIL_THEME_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg)

# The webcore skin against the browser's, value for value.
stencil_headless_test(stencil_webcore_headless
  SOURCES tests/support/webcore/rules.headless.cpp ${STENCIL_WEBCORE_SOURCES} ${STENCIL_THEME_SOURCES}
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Svg
  INCLUDE_TESTS)
