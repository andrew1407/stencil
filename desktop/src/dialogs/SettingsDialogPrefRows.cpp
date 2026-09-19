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

    addCheck(r, nativeMenuBar_, current.nativeMenuBar,
#ifdef Q_OS_WIN
          "Windows has no global menu bar, so this has no effect here.");
    nativeMenuBar_->setEnabled(false);
#else
          "On: the menu bar goes where your desktop puts it (the macOS menu bar, or a "
          "GNOME/Unity app menu). Off: it stays inside the window — use this if the "
          "in-window bar comes up empty on your desktop.\n\nTakes effect on restart. "
          "This dialog is always reachable with Ctrl+, so you can undo it even with "
          "no menus showing.");
#endif
    addRow(r, tr("Use the system menu bar"), nativeMenuBar_, /*column=*/false);

    addCheck(r, autosave_, current.autosave, "Automatically save the session as you edit");
    addRow(r, tr("Autosave"), autosave_, /*column=*/false);

    // "Auto-connect to servers on open" and "Sync changes to server" live in the Servers dialog (as
    // in the browser's modal); syncToServer rides through result() untouched from base_.

    addCheck(r, showPoints_, current.showPoints, "Show points on lines by default");
    addRow(r, tr("Show points"), showPoints_, /*column=*/false);
    addCheck(r, showLines_, current.showLines, "Show line strokes by default");
    addRow(r, tr("Show lines"), showLines_, /*column=*/false);

    page_ = new SearchComboBox(r.host);
    // Same options as the toolbar combo: Custom + the full ISO A/B/C series, labels with physical
    // sizes in the user's display unit, item data = the canonical name (read back in result()).
    fillPageSizeCombo(page_, /*includeCustom=*/true, current.units);
    {
      const int idx = page_->findData(current.pageSize);
      page_->setCurrentIndex(idx >= 0 ? idx : page_->findData("A3"));
    }
    page_->setToolTip("Default page format for cm/inch measurements");
    addRow(r, tr("Page size"), page_);
    connect(page_, &QComboBox::activated, this, [this] { applyLive(); });

    customW_ = new QDoubleSpinBox(r.host);
    customW_->setRange(1.0, 500.0);
    customW_->setSingleStep(0.1);
    customW_->setDecimals(1);
    customW_->setValue(current.customPageWidth);
    customW_->setToolTip("Custom page width in cm (used when page size is custom)");
    addRow(r, tr("Custom width (cm)"), customW_);
    connect(customW_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    customH_ = new QDoubleSpinBox(r.host);
    customH_->setRange(1.0, 500.0);
    customH_->setSingleStep(0.1);
    customH_->setDecimals(1);
    customH_->setValue(current.customPageHeight);
    customH_->setToolTip("Custom page height in cm (used when page size is custom)");
    addRow(r, tr("Custom height (cm)"), customH_);
    connect(customH_, &QAbstractSpinBox::editingFinished, this, [this] { applyLive(); });

    // "Open in…" targets (Project ▸ Open In…): where the browser app lives and
    // which Telegram bot to deep-link (empty hides the Telegram option).
    browserUrl_ = new QLineEdit(current.browserBaseUrl, r.host);
    browserUrl_->setToolTip(
        "Base URL of the Stencil browser app, used by \"Open In… → Browser app\"");
    addRow(r, tr("Browser app URL"), browserUrl_);
    connect(browserUrl_, &QLineEdit::editingFinished, this, [this] { applyLive(); });

    botUsername_ = new QLineEdit(current.telegramBotUsername, r.host);
    botUsername_->setPlaceholderText("e.g. my_stencil_bot (empty = hidden)");
    botUsername_->setToolTip(
        "Telegram bot username (without @) for \"Open In… → Telegram bot\"; "
        "leave empty to hide that option");
    addRow(r, tr("Telegram bot"), botUsername_);
    connect(botUsername_, &QLineEdit::editingFinished, this, [this] { applyLive(); });
  }

}
