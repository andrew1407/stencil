#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>
#include <vector>

namespace stencil::gui {

  struct Project;
  class KeywordChips;

  // The project-keywords editor (browser keywordsModal parity): the shared modal shell
  // around a KeywordChips field — a line edit over the words already held, each an oval
  // with a ✕.
  class KeywordsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit KeywordsDialog(const QStringList& current, QWidget* parent = nullptr);

    // The words held, normalised the way the projects search wants them.
    QStringList keywords() const;

    // Write `keywords` onto project `id` and persist the list — the SAME store write the
    // Projects window's row-menu "Add keywords" makes. False when no project has that id.
    static bool apply(std::vector<Project>& projects, const QString& id,
                      const QStringList& keywords, long long now);

   private:
    KeywordChips* chips_ = nullptr;
  };

}  // namespace stencil::gui
