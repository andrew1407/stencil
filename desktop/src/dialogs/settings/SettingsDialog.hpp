#pragma once
#include "fileStore.hpp"
#include <QDialog>
#include <QFont>
#include <QVector>
#include <functional>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

// Settings editor — the browser's "Visuals & Settings" modal (js/ui/modal.js):
// the shared shell, its search box, .vs-section captions over hairline .vs-row rows,
// and a footer hint beside Reset All. Live-apply, like the browser — no Save/Cancel,
// just Close. Desktop-only preferences ride along as extra sections. The AI-assistant
// rows are commit/discard and live in AssistantSettingsDialog (the chat dock's gear).
namespace stencil::gui {

  class SettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);
    Settings result() const;
    // Fired with result() on every change — MainWindow wires this to applySettings().
    void setOnChange(std::function<void(const Settings&)> cb) { onChange = std::move(cb); }

   signals:
    void visualsReset();                    // Reset All was applied (the owner toasts it)

   private:
    // The ctor's build context, handed down the buildXRows chain: the scroll body rows land
    // in, and the one mono font a colour well writes its hex in.
    struct Rows {
      QWidget* host = nullptr;
      QVBoxLayout* col = nullptr;
      QFont mono;
    };
    void addSection(Rows& r, const QString& text);
    void addRow(Rows& r, const QString& label, QWidget* field, bool column = true);
    QComboBox* addCombo(Rows& r, const QString& tip);
    void addWell(Rows& r, QPushButton*& btn, QString& hex, const QString& tip,
                 const QString& title);
    void addCheck(Rows& r, QCheckBox*& box, bool on, const QString& tip);
    // Section builders, in the order the browser's modal lists them.
    void buildAppearanceRows(Rows& r, const Settings& current);
    void buildMotionRows(Rows& r, const Settings& current);
    void buildNotifyRows(Rows& r, const Settings& current);
    void buildDrawingRows(Rows& r, const Settings& current);
    void buildPreferenceRows(Rows& r, const Settings& current);

    // Opens an animated picker anchored on `btn`, writes the chosen color into
    // `hex`, repaints the swatch, and applies live.
    void pickColorInto(QPushButton* btn, QString& hex, const QString& title);
    void applyLive();  // fires onChange with the current result()
    void applyFilter(const QString& query);
    void resetVisuals();   // the browser's VIS_DEFAULTS + the default accent

    // A section caption and the rows under it, for the search filter.
    struct Group {
      QLabel* title = nullptr;
      QVector<QPair<QString, QWidget*>> rows;   // label text, row widget
    };
    QVector<Group> groups;
    QLabel* empty = nullptr;
    QLineEdit* search = nullptr;

    // The motion rows SHOW what is in force — a skin may be holding the interface still over
    // the stored switches — so an untouched dialog hands the stored ones back unchanged.
    QString heldMotionMode;
    bool heldDrawAnim = true;
    bool heldBackdrop = true;
    bool motionTouched = false;

    std::function<void(const Settings&)> onChange;
    Settings base;  // preserves fields this dialog doesn't edit (formulas, llm*…)
    QComboBox* theme = nullptr;
    QComboBox* accent = nullptr;
    QCheckBox* nativeMenuBar = nullptr;
    QCheckBox* autosave = nullptr;
    QCheckBox* showPoints = nullptr;
    QCheckBox* showLines = nullptr;
    QPushButton* color = nullptr;
    QDoubleSpinBox* thickness = nullptr;
    QDoubleSpinBox* pointSize = nullptr;
    QComboBox* style = nullptr;
    QPushButton* fillColor = nullptr;      // area fill for newly-locked shapes
    QComboBox* page = nullptr;
    QDoubleSpinBox* customW = nullptr;
    QDoubleSpinBox* customH = nullptr;
    QSpinBox* holdDelay = nullptr;
    QCheckBox* drawAnim = nullptr;         // canvas stroke motion (browser vs-draw-anim)
    QCheckBox* modalBackdrop = nullptr;    // dim+blur behind windows (browser vs-modal-backdrop)
    QComboBox* motionMode = nullptr;       // particles | water | fire | slide | none (browser vs-motion-mode)
    QComboBox* notifyChannel = nullptr;    // toast | system (browser vs-notify-channel)
    QPushButton* selGlow = nullptr;        // selection highlight glow
    QPushButton* hoverRing = nullptr;      // point hover ring
    QPushButton* focusRing = nullptr;      // focused/clicked point ring
    QLineEdit* browserUrl = nullptr;   // "Open in…" browser-app base URL
    QLineEdit* botUsername = nullptr;  // "Open in…" Telegram bot username
    QString colorHex;
    QString fillHex;
    QString selGlowHex;
    QString hoverRingHex;
    QString focusRingHex;
  };

}
