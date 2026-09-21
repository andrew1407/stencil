#pragma once
#include <QColor>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

class QFrame;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace stencil::gui {

  class FlowLayout;

  // One keyword list, edited as chips (browser ui/keywordChips.js twin). The list is the value;
  // the input only proposes words.
  class KeywordChips : public QWidget {
    Q_OBJECT
   public:
    explicit KeywordChips(const QStringList& current, QWidget* parent = nullptr);

    // The words held, plus whatever is still typed — Save never drops a half-entered one.
    QStringList keywords() const;

    // ONE keyword, however many words it has: trimmed, inner whitespace collapsed,
    // lowercased. Empty when nothing was typed.
    static QString normalize(const QString& raw);
    // A stored list, cleaned and de-duplicated in order.
    static QStringList parse(const QStringList& list);
    // `raw` as one keyword, prepended to `list`. One already held MOVES to the front
    // rather than doubling, so a re-add is never a silent no-op.
    static QStringList addTo(const QStringList& list, const QString& raw);

    QLineEdit* getInput() const { return input; }
    // Seated in the dialog's footer, beside Cancel — never over the well.
    QPushButton* clearButton() const { return clearBtn; }

   private:
    void rebuild();
    void addTyped();
    // The chips whose word survived glide from `was` to the slot the flow just gave them.
    // Browser twin: keywordChips.js render().
    void playMotion(const QHash<QString, QRect>& was, const QList<QFrame*>& arrived);
    // A chip leaves the way the browser's does (css kwChipLeave): the slot is HELD while its grains
    // read, then its width collapses - and that collapse is what slides the chips behind it.
    void playLeave(const QList<QPointer<QFrame>>& going);
    QFrame* makeChip(const QString& word, const QColor& muted);

    bool orderChanged() const;

    QStringList words;
    QStringList laidOut;              // the order the flow currently holds
    QHash<QString, QFrame*> chipFor;   // patched in place, never rebuilt, so a chip can fly
    QLineEdit* input = nullptr;
    QPushButton* clearBtn = nullptr;
    QScrollArea* scroll = nullptr;
    QWidget* chipArea = nullptr;
    FlowLayout* flow = nullptr;
  };

}  // namespace stencil::gui
