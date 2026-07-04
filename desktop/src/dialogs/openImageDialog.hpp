#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QSpinBox;
class QWidget;

// "Open another image" dialog. Mirrors browser/js/ui/openImageModal.js: pick an
// image OR video file, or a web URL, + an incognito flag, then choose to replace
// the current editor ("Open here") or launch it in a new window. A video source
// (local or URL) reveals a frame index to grab. exec(); on QDialog::Accepted read
// outcome()/source()/incognito() (rejected = canceled). A URL or video source is
// resolved asynchronously (via MediaLoader), so it only offers Here / NewWindow.
namespace stencil::gui {

  class OpenImageDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { Here, NewWindow, Replace };

    // `canReplace` enables the "Replace image" outcome + its rename/keep-annotations
    // options (only meaningful when a saved/linked project is open).
    explicit OpenImageDialog(QWidget* parent = nullptr, bool canReplace = false);

    // The chosen source: the URL when one is typed, else the browsed local path.
    QString source() const;
    bool isUrl() const;     // a URL was typed (vs a local file)
    bool isVideo() const;   // the source is a video (local or URL) → grab a frame
    int frame() const;      // 0-based video frame to grab (ignored for still images)
    bool incognito() const;
    bool rename() const;
    bool keepAnnotations() const;
    Outcome outcome() const { return outcome_; }

   private:
    void browse();
    void refreshButtons();

    QLineEdit* path_ = nullptr;
    QLineEdit* url_ = nullptr;
    QSpinBox* frame_ = nullptr;
    QWidget* frameRow_ = nullptr;
    QCheckBox* incognito_ = nullptr;
    QCheckBox* rename_ = nullptr;
    QCheckBox* keep_ = nullptr;
    QPushButton* here_ = nullptr;
    QPushButton* newWindow_ = nullptr;
    QPushButton* replace_ = nullptr;
    bool canReplace_ = false;
    Outcome outcome_ = Outcome::Here;
  };

}
