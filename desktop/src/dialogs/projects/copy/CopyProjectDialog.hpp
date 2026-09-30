#pragma once
// "Make a copy": the one confirmation every entry point opens (browser ui/modal/copyProjectModal.js).
// A server source copies onto its server unless "Make a local copy" is ticked; only a local copy can
// open incognito, and then it is never saved, so "Just copy" waits for incognito to be off.
#include <QDialog>
#include <QString>

class QCheckBox;
class QPushButton;

namespace stencil::gui {

  class CopyProjectDialog : public QDialog {
    Q_OBJECT
   public:
    enum Outcome { JUST_COPY, OPEN, OPEN_NEW_WINDOW };

    // `scopeLabel` is the scope row's label ("Image and layout"); `serverUrl` names the server the
    // source lives on, empty for a local one.
    CopyProjectDialog(QWidget* parent, const QString& name, const QString& scopeLabel,
                      const QString& copyName, const QString& serverUrl);

    Outcome getOutcome() const { return outcome; }
    bool getIncognito() const;
    bool getLocal() const;

   private:
    void sync();
    void finish(Outcome how);

    QString serverUrl;
    QCheckBox* local = nullptr;
    QCheckBox* incognito = nullptr;
    QPushButton* justCopy = nullptr;
    Outcome outcome = JUST_COPY;
  };

}  // namespace stencil::gui
