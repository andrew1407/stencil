# The dialogs: settings, projects, open image, links, script, crop, the meta editors and the
# servers list, each a TU family that travels under one name.

# The projects dialog is split across Projects*.cpp partials (plus its row delegate),
# all defining ProjectsDialog:: / ProjectRowDelegate:: members; they travel together.
set(STENCIL_PROJECTS_DIALOG_SOURCES
  src/dialogs/projects/ProjectsDialog.cpp
  src/dialogs/projects/ProjectsDialogBuild.cpp
  src/dialogs/projects/ProjectsDialogEvents.cpp
  src/dialogs/projects/list/ProjectsDialogList.cpp
  src/dialogs/projects/list/ProjectsDialogRefresh.cpp
  src/dialogs/projects/list/ProjectsDialogRows.cpp
  src/dialogs/projects/list/ProjectsDialogViewport.cpp
  src/dialogs/projects/row/ProjectRowDelegate.cpp
  src/dialogs/projects/row/ProjectRowDelegateRow.cpp
  src/dialogs/projects/list/ProjectsBatchBar.cpp
  src/dialogs/projects/list/ProjectsFilter.cpp
  src/dialogs/projects/row/ProjectsHoverPreview.cpp
  src/dialogs/projects/ProjectsOpen.cpp
  src/dialogs/projects/ProjectsRename.cpp
  src/dialogs/projects/row/projectsRowChrome.cpp
  src/dialogs/projects/row/ProjectsRowIcons.cpp
  src/dialogs/projects/row/ProjectsRowMenu.cpp
  src/dialogs/projects/row/ProjectsRows.cpp
  src/dialogs/projects/row/ProjectsThumbs.cpp
  src/dialogs/projects/ProjectsTransfer.cpp)

# The connect list (dialogs/ConnectDialog.hpp) is seven TUs behind a private parts header:
# the build and row rebuild, the filter and row QSS, the row actions and the re-auth.
set(STENCIL_CONNECTDIALOG_SOURCES
  src/dialogs/connect/ConnectDialog.cpp
  src/dialogs/connect/ConnectDialogBatchBar.cpp
  src/dialogs/connect/ConnectDialogRow.cpp
  src/dialogs/connect/ConnectDialogRowActions.cpp
  src/dialogs/connect/ConnectDialogFilter.cpp
  src/dialogs/connect/ConnectDialogActions.cpp
  src/dialogs/connect/ConnectDialogAuth.cpp)

# The links dialog (dialogs/LinksDialog.hpp) is a TU family: the shell, two constructor build
# stages, the preview/scrub player and the show path.
set(STENCIL_LINKSDIALOG_SOURCES
  src/dialogs/meta/links/LinksDialog.cpp
  src/dialogs/meta/links/LinksDialogQuickCrop.cpp
  src/dialogs/meta/links/LinksDialogPreviewWiring.cpp
  src/dialogs/meta/links/LinksDialogPreview.cpp
  src/dialogs/meta/links/LinksDialogShow.cpp)

# The open dialog (dialogs/OpenImageDialog.hpp) is several TUs behind a private parts
# header: the build, the preview, the video scrub player, the preview's dust flourish, its
# per-tab cache (a switch re-shows a decode instead of re-fetching it), the tab state, the
# crop stage and the read-out.
set(STENCIL_OPENIMAGE_SOURCES
  src/dialogs/openImage/OpenImageDialog.cpp
  src/dialogs/openImage/OpenImageDialogBuildTabs.cpp
  src/dialogs/openImage/preview/OpenImageDialogBuildPreview.cpp
  src/dialogs/openImage/preview/OpenImageDialogBuildCrop.cpp
  src/dialogs/openImage/preview/OpenImageDialogPreview.cpp
  src/dialogs/openImage/preview/OpenImageDialogFit.cpp
  src/dialogs/openImage/preview/OpenImageDialogScrub.cpp
  src/dialogs/openImage/dust/OpenImageDialogDust.cpp
  src/dialogs/openImage/dust/OpenImageDialogSizeDust.cpp
  src/dialogs/openImage/dust/OpenImageDialogAlbumDust.cpp
  src/dialogs/meta/keywords/chipDust.cpp
  src/dialogs/openImage/preview/OpenImageDialogCache.cpp
  src/dialogs/openImage/OpenImageDialogState.cpp
  src/dialogs/openImage/preview/OpenImageDialogCropStage.cpp
  src/dialogs/openImage/OpenImageDialogResult.cpp)

list(APPEND STENCIL_GUI_SOURCES
  src/dialogs/settings/SettingsDialog.cpp
  src/dialogs/settings/SettingsDialogMotionRows.cpp
  src/dialogs/settings/SettingsDialogDrawRows.cpp
  src/dialogs/settings/SettingsDialogPrefRows.cpp
  src/dialogs/settings/SettingsDialogState.cpp
  src/dialogs/settings/AssistantSettingsDialog.cpp
  src/dialogs/settings/LlmSettingsForm.cpp
  src/dialogs/settings/LlmSettingsFormRows.cpp
  src/dialogs/settings/LlmSettingsFormState.cpp
  ${STENCIL_PROJECTS_DIALOG_SOURCES}
  src/dialogs/meta/ExpirationDialog.cpp
  src/dialogs/meta/ExpirationDialogCalendar.cpp
  ${STENCIL_OPENIMAGE_SOURCES}
  ${STENCIL_LINKSDIALOG_SOURCES}
  src/dialogs/meta/DescriptionDialog.cpp
  src/dialogs/script/ScriptDialog.cpp
  src/dialogs/script/ScriptDialogFile.cpp
  src/dialogs/script/ScriptEditorWidget.cpp
  src/dialogs/script/ScriptHighlighter.cpp
  src/dialogs/script/ScriptMenuPanel.cpp
  src/dialogs/script/ScriptMenuPanelState.cpp
  src/dialogs/meta/keywords/KeywordsDialog.cpp
  src/dialogs/meta/keywords/KeywordChips.cpp
  src/dialogs/meta/keywords/KeywordChipsMotion.cpp
  src/dialogs/crop/CropDialog.cpp
  src/dialogs/crop/CropDialogDrag.cpp
  src/dialogs/meta/InfoDialog.cpp
  src/dialogs/settings/ShortcutsDialog.cpp
  src/dialogs/settings/ShortcutsDialogRows.cpp
  ${STENCIL_CONNECTDIALOG_SOURCES}
  src/dialogs/meta/OpenInDialog.cpp)
