#pragma once
#include <QDialog>
#include <QString>
#include <QVector>

class QLineEdit;
class QLabel;
class QWidget;

// Controls & Shortcuts Info dialog. Renders browser/js/config/infoConfig.json, embedded
// as a Qt resource, like the browser info modal (browser/js/ui/infoModal.js): the shared
// shell, its live search box, and uppercase group titles over key / description rows.
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
      QWidget* widget = nullptr;
    };
    struct Section {
      QString title;
      QLabel* titleWidget = nullptr;
      QVector<Row> rows;
    };

    void loadSections();
    void build();
    void applyFilter(const QString& filter);

    QLineEdit* search_ = nullptr;
    QWidget* content_ = nullptr;
    QLabel* empty_ = nullptr;
    QVector<Section> sections_;
  };

}
