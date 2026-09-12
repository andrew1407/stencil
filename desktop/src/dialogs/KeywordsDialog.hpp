#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>
#include <vector>

class QPlainTextEdit;

namespace stencil::gui {

  struct Project;

  // The project-keywords editor (browser keywordsModal parity): the shared modal shell
  // around one text area; Enter saves, and the words come back normalised the way the
  // projects search wants them.
  class KeywordsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit KeywordsDialog(const QStringList& current, QWidget* parent = nullptr);

    // The entered words, normalised by parse().
    QStringList keywords() const;
    // Comma/space separated → lowercase unique words, in order (the browser store's
    // setKeywords rule, shared with the Projects window's row menu).
    static QStringList parse(const QString& raw);

    // Write `keywords` onto project `id` and persist the list — the SAME store write the
    // Projects window's row-menu "Add keywords" makes. False when no project has that id.
    static bool apply(std::vector<Project>& projects, const QString& id,
                      const QStringList& keywords, long long now);

   protected:
    bool eventFilter(QObject* obj, QEvent* event) override;   // Enter in the field saves

   private:
    QPlainTextEdit* edit_ = nullptr;
  };

}  // namespace stencil::gui
