#pragma once
#include <QDialog>
#include <QString>
#include <vector>

class QPlainTextEdit;

namespace stencil::gui {

  struct Project;

  // The project-description editor (browser descriptionModal parity): the shared modal
  // shell around one text area, Save writing the value back through the projects store.
  class DescriptionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit DescriptionDialog(const QString& current, QWidget* parent = nullptr);

    // The trimmed text, capped at MAX_CHARS (the row-menu prompt's own soft cap).
    QString text() const;
    static constexpr int MAX_CHARS = 2000;

    // The SAME store write the Projects row-menu "Add description" makes (commitRowEdit's local
    // branch). False when no project carries that id; an empty text clears.
    static bool apply(std::vector<Project>& projects, const QString& id, const QString& text,
                      long long now);

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;   // Ctrl+Enter in the field saves

   private:
    QPlainTextEdit* edit = nullptr;
  };

}  // namespace stencil::gui
