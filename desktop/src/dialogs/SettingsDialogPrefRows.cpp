// App preferences: the desktop-only rows the browser modal has no home for.
#include "SettingsDialog.hpp"
#include "../support/SearchCombo.hpp"
#include "guiHelpers.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>

namespace stencil::gui {

  void SettingsDialog::buildPreferenceRows(Rows& r, const Settings& current) {
    // App preferences (desktop-only; browser has no home for these)
    addSection(r, tr("App preferences"));

    addCheck(r, nativeMenuBar, current.nativeMenuBar,
#ifdef Q_OS_WIN
          "Windows has no global menu bar, so this has no effect here.");
    nativeMenuBar->setEnabled(false);
#else
          "On: the menu bar goes where your desktop puts it (the macOS menu bar, or a "
          "GNOME/Unity app menu). Off: it stays inside the window — use this if the "
          "in-window bar comes up empty on your desktop.\n\nTakes effect on restart. "
          "This dialog is always reachable with Ctrl+, so you can undo it even with "
          "no menus showing.");
#endif
    addRow(r, tr("Use the system menu bar"), nativeMenuBar, /*column=*/false);

    addCheck(r, autosave, current.autosave, "Automatically save the session as you edit");
    addRow(r, tr("Autosave"), autosave, /*column=*/false);

    // "Auto-connect to servers on open" and "Sync changes to server" live in the Servers dialog (as
    // in the browser's modal); syncToServer rides through result() untouched from base.

    addCheck(r, showPoints, current.showPoints, "Show points on lines by default");
    addRow(r, tr("Show points"), showPoints, /*column=*/false);
    addCheck(r, showLines, current.showLines, "Show line strokes by default");
    addRow(r, tr("Show lines"), showLines, /*column=*/false);

    page = new SearchComboBox(r.host);
    // Same options as the toolbar combo: Custom + the full ISO A/B/C series, labels with physical
    // sizes in the user's display unit, item data = the canonical name (read back in result()).
    fillPageSizeCombo(page, /*includeCustom=*/true, current.units);
    {
      const int idx = page->findData(current.pageSize);
      page->setCurrentIndex(idx >= 0 ? idx : page->findData("A3"));
    }
    page->setToolTip("Default page format for cm/inch measurements");
    addRow(r, tr("Page size"), page);
    connect(page, &QComboBox::activated, this, [this] { applyLive(); });

    customW = new QDoubleSpinBox(r.host);
    customW->setRange(1.0, 500.0);
    customW->setSingleStep(0.1);
    customW->setDecimals(1);
    customW->setValue(current.customPageWidth);
    customW->setToolTip("Custom page width in cm (used when page size is custom)");
    addRow(r, tr("Custom width (cm)"), customW);
    connect(customW, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    customH = new QDoubleSpinBox(r.host);
    customH->setRange(1.0, 500.0);
    customH->setSingleStep(0.1);
    customH->setDecimals(1);
    customH->setValue(current.customPageHeight);
    customH->setToolTip("Custom page height in cm (used when page size is custom)");
    addRow(r, tr("Custom height (cm)"), customH);
    connect(customH, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    // "Open in…" targets (Project ▸ Open In…): where the browser app lives and
    // which Telegram bot to deep-link (empty hides the Telegram option).
    browserUrl = new QLineEdit(current.browserBaseUrl, r.host);
    browserUrl->setToolTip(
        "Base URL of the Stencil browser app, used by \"Open In… → Browser app\"");
    addRow(r, tr("Browser app URL"), browserUrl);
    connect(browserUrl, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    botUsername = new QLineEdit(current.telegramBotUsername, r.host);
    botUsername->setPlaceholderText("e.g. my_stencil_bot (empty = hidden)");
    botUsername->setToolTip(
        "Telegram bot username (without @) for \"Open In… → Telegram bot\"; "
        "leave empty to hide that option");
    addRow(r, tr("Telegram bot"), botUsername);
    connect(botUsername, &QLineEdit::editingFinished, this, [this] { applyLive(); });
  }

}
