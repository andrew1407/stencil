#pragma once
#include "fileStore.hpp"
#include <QDialog>
#include <QVector>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

// Settings editor — the browser's "Style & Visual Settings" modal (js/ui/visualsModal.js):
// the shared shell, its search box, .vs-section captions over hairline .vs-row rows,
// and a footer hint beside Reset All. Live-apply, like the browser — no Save/Cancel,
// just Close. Desktop-only preferences ride along as extra sections. The AI-assistant
// rows are commit/discard and stay behind AssistantSettingsDialog; this links to it.
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
    void visualsReset();                    // Reset All was applied (the owner toasts it)

   private:
    // Opens an animated picker anchored on `btn`, writes the chosen color into
    // `hex`, repaints the swatch, and applies live.
    void pickColorInto(QPushButton* btn, QString& hex, const QString& title);
    void applyLive();  // fires onChange_ with the current result()
    void applyFilter(const QString& query);
    void resetVisuals();   // the browser's VIS_DEFAULTS + the default accent

    // A section caption and the rows under it, for the search filter.
    struct Group {
      QLabel* title = nullptr;
      QVector<QPair<QString, QWidget*>> rows;   // label text, row widget
    };
    QVector<Group> groups_;
    QLabel* empty_ = nullptr;
    QLineEdit* search_ = nullptr;

    std::function<void(const Settings&)> onChange_;
    Settings base_;  // preserves fields this dialog doesn't edit (formulas, llm*…)
    QComboBox* theme_ = nullptr;
    QComboBox* accent_ = nullptr;
    QCheckBox* nativeMenuBar_ = nullptr;
    QCheckBox* autosave_ = nullptr;
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
    QCheckBox* drawAnim_ = nullptr;         // canvas stroke motion (browser vs-draw-anim)
    QComboBox* motionMode_ = nullptr;       // particles | slide | none (browser vs-motion-mode)
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
