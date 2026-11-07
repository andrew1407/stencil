#include "AquaCheckDelegate.hpp"

#include "../skin/SkinLookStrategy.hpp"
#include "../skinPrefs.hpp"
#include "../theme/theme.hpp"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QComboBox>
#include <QPainter>

namespace stencil::support {

  AquaCheckDelegate::AquaCheckDelegate(QComboBox* combo, QAbstractItemDelegate* inner, QObject* parent)
      : QStyledItemDelegate(parent), combo(combo), inner(inner) {
    setObjectName(QString::fromLatin1(AQUA_CHECK_DELEGATE_NAME));
  }

  bool AquaCheckDelegate::isCurrent(const QModelIndex& idx) const {
    if (!combo || combo->currentIndex() < 0 || !idx.isValid()) return false;
    QModelIndex src = idx;
    if (const auto* proxy = qobject_cast<const QAbstractProxyModel*>(idx.model())) src = proxy->mapToSource(idx);
    return src.model() == combo->model() && src.row() == combo->currentIndex();
  }

  void AquaCheckDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    if (inner) inner->paint(p, opt, idx);
    else QStyledItemDelegate::paint(p, opt, idx);
    if (!isAqua() || !isCurrent(idx)) return;
    const auto skinned = skinLook().palette(skinDark());   // one copy, for the one row that draws the ✓
    if (!skinned) return;
    // The gel bar (pointer or keyboard) carries --aq-on-select, a resting row --aq-ink.
    const stencil::gui::Palette& pal = *skinned;
    const bool isLit = opt.state & (QStyle::State_Selected | QStyle::State_MouseOver);
    QFont font = opt.font;
    font.setBold(true);
    p->save();
    p->setFont(font);
    p->setPen(isLit ? pal.onAccent : pal.textMain);
    const QRect margin(opt.rect.left() + AQUA_CHECK_LEFT_PX, opt.rect.top(),
                       AQUA_CHECK_MARGIN_PX - AQUA_CHECK_LEFT_PX, opt.rect.height());
    static const QString CHECK = QStringLiteral("\u2713");
    p->drawText(margin, Qt::AlignLeft | Qt::AlignVCenter, CHECK);
    p->restore();
  }

  QSize AquaCheckDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    return inner ? inner->sizeHint(opt, idx) : QStyledItemDelegate::sizeHint(opt, idx);
  }

  void wearAquaCheck(QComboBox* combo) {
    if (!combo || !isAqua() || combo->property(SWATCH_ROWS_PROPERTY).toBool()) return;
    QAbstractItemDelegate* have = combo->view()->itemDelegate();
    if (have && have->objectName() == QLatin1String(AQUA_CHECK_DELEGATE_NAME)) return;
    const bool hasOwnDelegate = have && !have->inherits("QComboMenuDelegate") && !have->inherits("QComboBoxDelegate");
    combo->view()->setProperty(AQUA_CHECK_PROPERTY, true);
    combo->view()->setItemDelegate(new AquaCheckDelegate(combo, hasOwnDelegate ? have : nullptr, combo->view()));
  }

}  // namespace stencil::support
