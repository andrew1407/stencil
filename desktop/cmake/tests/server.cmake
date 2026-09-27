# The server-facing suites: stale credentials, the projects dialog's rows and batch bar, the
# co-edit primitives and the list walk.

# Stale sessions (net/serverClient + the Servers row): a refused credential is Expired, is not
# retried in a loop, keeps the saved connection, and the row offers Reconnect.
stencil_headless_test(stencil_serverauth_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/net/serverAuth.headless.cpp tests/net/serverAuthClassify.headless.cpp
    tests/net/serverAuthKind.headless.cpp tests/net/serverAuthRemint.headless.cpp
    tests/net/serverAuthInvite.headless.cpp tests/net/serverAuthAdmin.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_CONNECTDIALOG_SOURCES} ${STENCIL_SERVERCLIENT_SOURCES} src/net/connectionStore.cpp
    ${STENCIL_FILESTORE_SOURCES}
    src/io/deferredWrite.cpp
    src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}
    src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    src/support/menu/menuReveal.cpp
    src/support/menu/popupSlide.cpp
    src/support/menu/SearchCombo.cpp src/support/menu/SearchComboPopup.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The Projects dialog's multi-select surface: the batch bar, select all, the filter fade and batch
# remove; a mock QTcpServer serves the /projects listing.
stencil_headless_test(stencil_projectsbatch_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_APPTOOLTIP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/projects/list/ProjectsBatchBar.headless.cpp tests/dialogs/projects/list/ProjectsBatchBarRows.headless.cpp
    tests/dialogs/projects/list/ProjectsBatchBarBar.headless.cpp tests/dialogs/projects/list/ProjectsBatchBarRemove.headless.cpp
    tests/dialogs/projects/list/ProjectsBatchBarFilter.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_TIPCONTENT_SOURCES}
    ${STENCIL_PROJECTS_DIALOG_SOURCES}
    src/dialogs/meta/ExpirationDialog.cpp src/dialogs/meta/ExpirationDialogCalendar.cpp
    ${STENCIL_SERVERCLIENT_SOURCES} ${STENCIL_FETCHGUARD_SOURCES}
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}
    src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    src/support/menu/menuReveal.cpp
    src/support/menu/popupSlide.cpp
    src/support/menu/SearchCombo.cpp src/support/menu/SearchComboPopup.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# The same dialog's composition + row-data pin: what it builds, and what refresh() writes per row.
stencil_headless_test(stencil_projectsdialogrows_headless
  SOURCES ${STENCIL_DUSTKIT_SOURCES}
    ${STENCIL_CONTROLREVEAL_SOURCES}
    ${STENCIL_FILTERFADE_SOURCES}
    ${STENCIL_APPTOOLTIP_SOURCES}
    ${STENCIL_DISINTEGRATE_SOURCES}
    tests/dialogs/projects/list/ProjectsDialogRows.headless.cpp ${STENCIL_MODALCHROME_SOURCES}
    ${STENCIL_TIPCONTENT_SOURCES}
    ${STENCIL_PROJECTS_DIALOG_SOURCES}
    src/dialogs/meta/ExpirationDialog.cpp src/dialogs/meta/ExpirationDialogCalendar.cpp
    ${STENCIL_SERVERCLIENT_SOURCES} ${STENCIL_FETCHGUARD_SOURCES}
    ${STENCIL_FILESTORE_SOURCES} src/io/deferredWrite.cpp src/support/guiHelpers.cpp src/support/guiHelpersColor.cpp
    ${STENCIL_THEME_SOURCES}
    src/support/icon/iconSet.cpp src/support/modal/modalReveal.cpp
    src/support/menu/menuReveal.cpp
    src/support/menu/popupSlide.cpp
    src/support/menu/SearchCombo.cpp src/support/menu/SearchComboPopup.cpp
    resources/app.qrc
  LIBS stencil_core Qt6::Widgets Qt6::Network Qt6::Svg)

# Co-edit smoke over two connections as two editors; self-skips with no server reachable
# (STENCIL_TEST_SERVER, default http://localhost:8090).
stencil_headless_test(stencil_coedit_headless
  SOURCES tests/net/coEdit.headless.cpp tests/net/coEditOpen.headless.cpp tests/net/coEditMerge.headless.cpp
    ${STENCIL_SERVERCLIENT_SOURCES}
    resources/app.qrc   # the SSRF table a literal STENCIL_TEST_SERVER is judged by
  LIBS stencil_core Qt6::Gui Qt6::Network)

# The project list's nextCursor walk and its loop guards, against the in-process MockRest.
stencil_headless_test(stencil_serverlist_headless
  SOURCES tests/net/serverList.headless.cpp ${STENCIL_SERVERCLIENT_SOURCES} resources/app.qrc
  LIBS stencil_core Qt6::Gui Qt6::Network)
