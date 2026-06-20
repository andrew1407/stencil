#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QPushButton;

// "Open another image" dialog. Mirrors browser/js/ui/openImageModal.js: pick an
// image file + an incognito flag, then choose to replace the current editor
// ("Open here") or launch it in a new window ("Open in new window"). exec(); on
// QDialog::Accepted read outcome()/path()/incognito() (rejected = canceled).
namespace stencil::gui {

  class OpenImageDialog : public QDialog {
    Q_OBJECT
   public:
    enum class Outcome { Here, NewWindow };

    explicit OpenImageDialog(QWidget* parent = nullptr);

    QString path() const;
    bool incognito() const;
    Outcome outcome() const { return outcome_; }

   private:
    void browse();
    void refreshButtons();

    QLineEdit* path_ = nullptr;
    QCheckBox* incognito_ = nullptr;
    QPushButton* here_ = nullptr;
    QPushButton* newWindow_ = nullptr;
    Outcome outcome_ = Outcome::Here;
  };

}
