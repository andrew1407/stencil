#pragma once
#include "fileStore.hpp"
#include <QDialog>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QLineEdit;

// Settings editor. Sectioned like the browser's "Default Visuals" modal
// (js/ui/visualsModal.js), plus the desktop-only preferences browser has no
// home for. Live-apply, like visualsModal.js — no Save/Cancel, just Close.
// The AI-assistant rows are the one exception (browser's own llmSettingsModal.js
// is commit/discard too) and stay behind AssistantSettingsDialog; this just
// links to it.
namespace stencil::gui {

  class SettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);
    Settings result() const;
    // Fired with result() on every change — MainWindow wires this to applySettings().
    void setOnChange(std::function<void(const Settings&)> cb) { onChange_ = std::move(cb); }

   signals:
    void openAssistantSettingsRequested();  // "Open Assistant Settings…" row

   private:
    // Opens an animated picker anchored on `btn`, writes the chosen color into
    // `hex`, repaints the swatch, and applies live.
    void pickColorInto(QPushButton* btn, QString& hex, const QString& title);
    void applyLive();  // fires onChange_ with the current result()

    std::function<void(const Settings&)> onChange_;
    Settings base_;  // preserves fields this dialog doesn't edit (formulas, llm*…)
    QComboBox* theme_ = nullptr;
    QComboBox* accent_ = nullptr;
    QCheckBox* nativeMenuBar_ = nullptr;
    QCheckBox* autosave_ = nullptr;
    QCheckBox* syncToServer_ = nullptr;
    QCheckBox* showPoints_ = nullptr;
    QCheckBox* showLines_ = nullptr;
    QPushButton* color_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* pointSize_ = nullptr;
    QComboBox* style_ = nullptr;
    QPushButton* fillColor_ = nullptr;      // area fill for newly-locked shapes
    QComboBox* page_ = nullptr;
    QDoubleSpinBox* customW_ = nullptr;
    QDoubleSpinBox* customH_ = nullptr;
    QSpinBox* holdDelay_ = nullptr;
    QPushButton* selGlow_ = nullptr;        // selection highlight glow
    QPushButton* hoverRing_ = nullptr;      // point hover ring
    QPushButton* focusRing_ = nullptr;      // focused/clicked point ring
    QLineEdit* browserUrl_ = nullptr;   // "Open in…" browser-app base URL
    QLineEdit* botUsername_ = nullptr;  // "Open in…" Telegram bot username
    QString colorHex_;
    QString fillHex_;
    QString selGlowHex_;
    QString hoverRingHex_;
    QString focusRingHex_;
  };

}
