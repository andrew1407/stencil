#include "KeywordChips.hpp"
#include "iconSet.hpp"
#include "../../../support/control/FlowLayout.hpp"
#include "../../../support/modal/modalChrome.hpp"
#include "projectMetaDialog.hpp"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSet>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace stencil::gui {

  // The oval's height, browser .kw-chip.
  static constexpr int CHIP_H = 26;

  KeywordChips::KeywordChips(const QStringList& current, QWidget* parent)
      : QWidget(parent), words(parse(current)) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    // The word being proposed, above the words already held (browser .kw-add).
    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(6);
    input = new QLineEdit(this);
    input->setObjectName(QStringLiteral("keywordsInput"));
    input->setPlaceholderText(tr("Add a keyword…"));
    auto* addBtn = new QPushButton(tr("Add"), this);
    makeModalCta(addBtn, QStringLiteral("plus"));
    markModalPrimary(addBtn);
    addBtn->setAutoDefault(false);   // no tooltip: the placeholder beside it says what it does
    addRow->addWidget(fieldRing(input, this), 1);   // rings only while it has focus
    addRow->addWidget(addBtn);
    col->addLayout(addRow);

    chipArea = new QWidget;
    chipArea->setObjectName(QStringLiteral("keywordsChipArea"));
    flow = new FlowLayout(chipArea, 8, 6, 6);
    scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("keywordsChips"));
    scroll->setWidget(chipArea);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // the flow wraps instead
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // The well's floor: an empty FlowLayout hints at nothing, so the popover — sized from
    // its sizeHint — collapsed it to a strip. Twin: DescriptionDialog FIELD_FLOOR_PX.
    scroll->setMinimumHeight(285);
    col->addWidget(fieldRing(scroll, this, /*alwaysOn=*/true), 1);   // a well never focuses
    setMinimumWidth(META_FIELD_MIN_W);   // the compact shape is 420px wide, like the browser's

    // Clear all is laid out by the dialog, in its footer beside Cancel (clearButton()).
    clearBtn = new QPushButton(tr("Clear all"), this);
    makeModalDanger(clearBtn, QStringLiteral("trash"));   // never Cancel's ✕ beside it
    clearBtn->setAutoDefault(false);   // no tooltip: the label already says it
    connect(clearBtn, &QPushButton::clicked, this, [this] {
      words.clear();
      input->clear();   // else Save would put the typed word straight back
      rebuild();
      input->setFocus();
    });

    connect(addBtn, &QPushButton::clicked, this, &KeywordChips::addTyped);
    // Enter adds the word; it never accepts the dialog, or a half-typed keyword is lost.
    connect(input, &QLineEdit::returnPressed, this, &KeywordChips::addTyped);
    rebuild();
    animate = true;   // the opening list is laid out, never flown in
  }

  void KeywordChips::addTyped() {
    words = addTo(words, input->text());
    input->clear();
    rebuild();
    input->setFocus();
  }

  // Whether the surviving words no longer sit in the order the flow last laid them out.
  bool KeywordChips::orderChanged() const {
    QStringList shown;
    for (const QString& w : laidOut)
      if (chipFor.contains(w)) shown << w;
    return shown != words;
  }

  // The list is the truth; this brings the row to it. A word that went scatters and is
  // deleted, a word that arrived fades up, and every survivor glides to its new slot.
  void KeywordChips::rebuild() {
    QHash<QString, QRect> was;
    for (auto it = chipFor.cbegin(); it != chipFor.cend(); ++it)
      was.insert(it.key(), it.value()->geometry());

    const QSet<QString> still(words.cbegin(), words.cend());
    QList<QPointer<QFrame>> going;
    for (auto it = was.cbegin(); it != was.cend(); ++it)
      if (!still.contains(it.key())) going << chipFor.take(it.key());
    if (animate) playLeave(going);
    else for (QFrame* chip : going) { chip->setParent(nullptr); chip->deleteLater(); }

    QList<QFrame*> arrived;
    const QColor muted = palette().color(QPalette::PlaceholderText);
    for (const QString& word : words)
      if (!chipFor.contains(word)) {
        QFrame* chip = makeChip(word, muted);
        chipFor.insert(word, chip);
        arrived << chip;
      }
    // Re-adding MOVES an item to the end, so only do it when the ORDER really changed. On a plain
    // removal the survivors keep their places; restacking would shove the leaving chip to the front.
    if (!arrived.isEmpty() || orderChanged()) {
      for (const QString& word : words) {
        QFrame* chip = chipFor.value(word);
        flow->removeWidget(chip);
        flow->addWidget(chip);
      }
    }
    laidOut = words;
    flow->activate();
    // activate() posts a box that has not changed: a new chip sat at 0,0 and its arrival flew from the corner.
    flow->setGeometry(chipArea->rect());
    clearBtn->setEnabled(!words.isEmpty());
    // While chips are leaving, THEIR collapse moves the survivors; gliding them too would
    // race it and slide them over the dust.
    if (animate) playMotion(going.isEmpty() ? was : QHash<QString, QRect>(), arrived);
  }

  QFrame* KeywordChips::makeChip(const QString& word, const QColor& muted) {
    auto* chip = new QFrame(chipArea);
    chip->setProperty("kwChip", true);
    chip->setFixedHeight(CHIP_H);
    auto* row = new QHBoxLayout(chip);
    row->setContentsMargins(10, 0, 4, 0);
    row->setSpacing(4);
    auto* label = new QLabel(word, chip);
    QFont f = label->font();
    f.setPixelSize(13);   // browser .kw-chip font-size
    label->setFont(f);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    row->addWidget(label);
    auto* rm = new QPushButton(chip);
    rm->setProperty("kwChipX", true);
    rm->setIcon(themedIcon("x", muted, 12));
    rm->setIconSize(QSize(12, 12));
    rm->setFixedSize(18, 18);
    rm->setCursor(Qt::PointingHandCursor);
    rm->setAccessibleName(tr("Remove %1").arg(word));   // no tooltip: the word is beside it
    connect(rm, &QPushButton::clicked, this, [this, word] {
      words.removeAll(word);
      rebuild();
      input->setFocus();
    });
    row->addWidget(rm);   // AFTER the label: the ✕ closes the oval on the right
    chip->show();
    return chip;
  }

  QStringList KeywordChips::keywords() const { return addTo(words, input->text()); }

  QString KeywordChips::normalize(const QString& raw) {
    return raw.simplified().toLower();   // trims, and collapses inner runs of whitespace
  }

  QStringList KeywordChips::parse(const QStringList& list) {
    QStringList out;
    for (const QString& raw : list) {
      const QString k = normalize(raw);
      if (!k.isEmpty() && !out.contains(k)) out << k;
    }
    return out;
  }

  QStringList KeywordChips::addTo(const QStringList& list, const QString& raw) {
    const QString k = normalize(raw);
    if (k.isEmpty()) return list;
    QStringList next = list;
    next.removeAll(k);
    next.prepend(k);
    return next;
  }

}  // namespace stencil::gui
