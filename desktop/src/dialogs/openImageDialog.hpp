#pragma once
#include <QColor>
#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QRadioButton;
class QToolButton;
class QSpinBox;
class QTabWidget;
class QFormLayout;
class QWidget;

// Unified "Open Image" dialog. Mirrors browser/js/ui/openImageModal.js: the single way
// to get an image into the editor — a local FILE, a web URL/reference, or a NEW BLANK
// canvas — chosen with the Source selector. For a file/URL source, choose to replace
// the current editor ("Open here") or launch it in a new window; a video source (local
// or URL) reveals a frame index to grab. For the blank source, pick a fill color + size
// and "Create blank". exec(); on QDialog::Accepted read outcome() and the matching
// getters (rejected = canceled).
namespace stencil::gui {

  class OpenImageDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { Here, NewWindow, Replace, Blank };

    // `canReplace` enables the "Replace image" outcome + its rename/keep-annotations
    // options (only meaningful when a saved/linked project is open). blankW/blankH seed
    // the new-blank size (the current page at 96 dpi). startBlank opens straight in
    // blank mode (the idle-canvas / projects "new blank" shortcuts).
    explicit OpenImageDialog(QWidget* parent, bool canReplace,
                             int blankW, int blankH, bool startBlank = false);

    // ── File / URL source ──
    // The chosen source: the URL when one is typed, else the browsed local path.
    QString source() const;
    bool isUrl() const;     // a URL was typed (vs a local file)
    bool isVideo() const;   // the source is a video (local or URL) → grab a frame
    int frame() const;      // 0-based video frame to grab (ignored for still images)
    bool incognito() const;
    bool rename() const;
    bool keepAnnotations() const;

    // ── New-blank source ──
    QColor blankColor() const;
    int blankWidth() const;
    int blankHeight() const;

    Outcome outcome() const { return outcome_; }

   private:
    void browse();
    void pickCustomColor();
    void applyMode();       // swap the footer actions to match the active tab
    void refreshButtons();  // enable file/URL actions once a source is chosen

    // Source tabs: 0 = Local file, 1 = URL link, 2 = Blank.
    QTabWidget* tabs_ = nullptr;

    // File / URL controls.
    QLineEdit* path_ = nullptr;
    QLineEdit* url_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QWidget* frameRow_ = nullptr;
    QCheckBox* incognito_ = nullptr;
    QFormLayout* commonForm_ = nullptr;  // holds the Incognito row (hidden on the Blank tab)
    QCheckBox* rename_ = nullptr;
    QCheckBox* keep_ = nullptr;
    QWidget* replaceRow_ = nullptr;

    // Blank controls.
    QRadioButton* white_ = nullptr;
    QRadioButton* black_ = nullptr;
    QRadioButton* customColorRadio_ = nullptr;
    QToolButton* customSwatch_ = nullptr;
    QSpinBox* blankWidth_ = nullptr;
    QSpinBox* blankHeight_ = nullptr;
    QColor customColor_{Qt::white};

    // Footer actions.
    QPushButton* here_ = nullptr;
    QPushButton* newWindow_ = nullptr;
    QPushButton* replace_ = nullptr;
    QPushButton* createBlank_ = nullptr;

    bool canReplace_ = false;
    Outcome outcome_ = Outcome::Here;
  };

}
