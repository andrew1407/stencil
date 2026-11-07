// Headless checks for support/aqua/AquaCheckDelegate.hpp (browser twin: css/aqua/menus.css): under aqua a
// combo list's current value paints a ✓ in its rows' 24px left margin and no other row does; the
// motion list keeps its painter inside it, a swatch list takes none, and a plain combo drops Qt's
// menu rows and their indicator for it.
#include "../../../src/support/aqua/AquaCheckDelegate.hpp"
#include "../../../src/support/skin/skinAppliers.hpp"
#include "../../../src/support/skinPrefs.hpp"
#include "../../../src/support/aqua/sheet.hpp"
#include "../../../src/support/icon/motionIcons.hpp"
#include "../../../src/support/menu/SearchCombo.hpp"
#include "../../../src/support/motion/RowHoverSlide.hpp"

#include <QApplication>
#include <QListView>
#include <QTest>

#include "../check.hpp"

using namespace stencil::support;
using stencil::gui::SearchComboBox;

namespace {
  // Opaque pixels darker than the stripes in row `row`'s left margin, where only the ✓ may be.
  int inkInMargin(QListView* list, int row) {
    const QImage img = list->viewport()->grab().toImage();
    const QRect r = list->visualRect(list->model()->index(row, 0));
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
      for (int x = r.left() + 2; x < r.left() + AQUA_CHECK_MARGIN_PX - 2; ++x)
        if (img.rect().contains(x, y) && img.pixelColor(x, y).alpha() > 128 && img.pixelColor(x, y).lightness() < 110) ++n;
    return n;
  }

  AquaCheckDelegate* checkOf(QListView* list) {
    return static_cast<AquaCheckDelegate*>(
        list->findChild<QObject*>(QString::fromLatin1(AQUA_CHECK_DELEGATE_NAME), Qt::FindDirectChildrenOnly));
  }
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  setSkin(Skin::AQUA);
  applyAquaLook(true);
  setSkinDark(false);
  qApp->setStyleSheet(buildAquaStylesheet(false, QStringLiteral("blue")));

  SearchComboBox combo(nullptr, /*searchable=*/false);
  for (const char* z : {"50%", "75%", "100%", "150%"}) combo.addItem(QString::fromLatin1(z), QString::fromLatin1(z));
  combo.setCurrentIndex(2);
  combo.resize(160, 26);
  combo.show();
  check(QTest::qWaitForWindowExposed(&combo), "the combo is on screen");
  combo.showPopup();
  QListView* list = combo.popupList();
  QTest::qWait(50);
  check(list->property(AQUA_CHECK_PROPERTY).toBool(), "the list carries the ✓ margin's property");
  AquaCheckDelegate* mark = checkOf(list);
  check(mark != nullptr, "its rows are painted through the ✓ delegate");
  check(mark && mark->isCurrent(list->model()->index(2, 0)) && !mark->isCurrent(list->model()->index(1, 0)),
        "…which knows the combo's value through the search proxy");
  list->setCurrentIndex(list->model()->index(0, 0));   // the gel bar off the value: it rests in ink
  QTest::qWait(30);
  check(inkInMargin(list, 2) > 4, "the current value's row shows a ✓ in its left margin");
  check(inkInMargin(list, 1) == 0 && inkInMargin(list, 3) == 0, "no other row marks its margin, its text past it");
  for (int r = 0; r < combo.count(); ++r)
    check(!list->model()->index(r, 0).data(Qt::CheckStateRole).isValid(), "no row carries Qt's own check");
  combo.hidePopup();

  SearchComboBox motion(nullptr, false);
  motion.addItem(QStringLiteral("Slide"), QStringLiteral("slide"));
  motion.addItem(QStringLiteral("Water"), QStringLiteral("water"));
  auto* glyphs = new MotionIconDelegate(motion.popupList(), &motion);
  motion.setListDelegate(glyphs);
  check(checkOf(motion.popupList()) && checkOf(motion.popupList())->getInner() == glyphs,
        "the motion list keeps its glyph painter inside the ✓");
  auto* slide = static_cast<stencil::gui::SlidingRowDelegate*>(motion.popupList()->itemDelegate());
  check(motion.popupList()->itemDelegate()->objectName() == QLatin1String(stencil::gui::SLIDING_DELEGATE_NAME)
            && slide->getInner() == checkOf(motion.popupList()),
        "…and the row slide wraps them both, once");

  SearchComboBox swatches(nullptr, false);
  swatches.setProperty(SWATCH_ROWS_PROPERTY, true);
  swatches.addItem(QStringLiteral("Blue"), QStringLiteral("blue"));
  check(!checkOf(swatches.popupList()) && !swatches.popupList()->property(AQUA_CHECK_PROPERTY).toBool(),
        "a swatch list keeps its chip as the mark: no ✓, no margin");

  QComboBox plain;
  plain.addItems({QStringLiteral("One"), QStringLiteral("Two")});
  wearAquaCheck(&plain);
  check(plain.itemDelegate()->objectName() == QLatin1String(AQUA_CHECK_DELEGATE_NAME)
            && !plain.itemDelegate()->inherits("QComboMenuDelegate"),
        "a plain combo's list drops Qt's menu rows and their check indicator");
  check(plain.view()->property(AQUA_CHECK_PROPERTY).toBool(), "…and takes the ✓ margin");

  setSkin(Skin::DEFAULT);
  applyAquaLook(false);
  QComboBox later;
  wearAquaCheck(&later);
  check(later.itemDelegate()->objectName() != QLatin1String(AQUA_CHECK_DELEGATE_NAME), "off aqua a plain combo is Qt's");
  return failures ? 1 : 0;
}
