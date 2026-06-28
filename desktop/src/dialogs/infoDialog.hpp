#pragma once
#include <QDialog>
#include <QString>
#include <QVector>

class QLineEdit;
class QTextBrowser;

// Info & Shortcuts dialog. Renders browser/js/config/infoConfig.json (usage tips)
// and hotkeysConfig.json (key bindings), both embedded as Qt resources, like the
// browser info modal (browser/js/ui/infoModal.js) — including its live search box
// and themed key "chips", so the desktop reference matches the web one.
namespace stencil::gui {

  class InfoDialog : public QDialog {
    Q_OBJECT
   public:
    explicit InfoDialog(QWidget* parent = nullptr);

   private:
    // `left` is the shortcut/term, `right` its description.
    struct Row {
      QString left;
      QString right;
    };
    struct Section {
      QString title;
      QVector<Row> rows;
    };

    void loadSections();
    void render(const QString& filter);

    QLineEdit* search_ = nullptr;
    QTextBrowser* browser_ = nullptr;
    QVector<Section> sections_;
  };

}
