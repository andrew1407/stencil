#pragma once
// The aqua combo list's current value (browser css/aqua/menus.css .accent-dd-opt[aria-selected]::before,
// .zoom-menu-item.active): a bold ✓ in the row's 24px left margin, in the row's own ink, never a tint.
// A list whose rows carry colour swatches keeps the swatch as its mark and draws no ✓.
#include <QComboBox>
#include <QPointer>
#include <QStyledItemDelegate>

namespace stencil::support {

  // The margin is qss/aqua's padding-left on a list carrying AQUA_CHECK_PROPERTY; the ✓ sits 7px in.
  inline constexpr int AQUA_CHECK_MARGIN_PX = 24;
  inline constexpr int AQUA_CHECK_LEFT_PX = 7;
  inline constexpr char AQUA_CHECK_PROPERTY[] = "aquaCheck";
  // Set on a combo whose rows are colour swatches: it never takes the ✓.
  inline constexpr char SWATCH_ROWS_PROPERTY[] = "swatchRows";
  inline constexpr char AQUA_CHECK_DELEGATE_NAME[] = "stencilAquaCheck";

  // Wraps `inner` (null: the plain row) and adds the ✓ while the aqua skin is on.
  class AquaCheckDelegate : public QStyledItemDelegate {
   public:
    AquaCheckDelegate(QComboBox* combo, QAbstractItemDelegate* inner, QObject* parent);
    QAbstractItemDelegate* getInner() const { return inner; }
    // The row holds the combo's value; a proxy row is mapped to its source first.
    bool isCurrent(const QModelIndex& idx) const;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

   private:
    QPointer<QComboBox> combo;
    QPointer<QAbstractItemDelegate> inner;
  };

  // A plain QComboBox opened under aqua: its list paints the ✓ rows instead of Qt's menu rows and
  // their check indicator; a delegate of the caller's own keeps painting under the ✓.
  void wearAquaCheck(QComboBox* combo);

}  // namespace stencil::support
