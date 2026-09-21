#include "ConnectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../../support/motion/scrollReveal.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"
#include "../../support/motion/DissolveEffect.hpp"
#include "../../support/theme/filterFade.hpp"
#include "../../support/control/FlowLayout.hpp"
#include "../../support/guiHelpers.hpp"
#include "../../support/modal/modalChrome.hpp"
#include "../../support/modal/modalReveal.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "../../support/motion/ShimmerOverlay.hpp"

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"
#include "ReorderableListWidget.hpp"
#include "SearchCombo.hpp"
#include "ServerClient.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QColor>
#include <QGuiApplication>
#include <QFont>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace stencil::gui {

  // Browser modal.js `shownUrls`. Read off the rows, never manager->urls() by index: a row
  // retired under its removal dust keeps its slot after the manager has let go.
  QStringList ConnectDialog::shownUrls() const {
    QStringList out;
    if (!list) return out;
    const QString mode =
        kindFilter ? kindFilter->currentData().toString() : QStringLiteral("all");
    for (int i = 0; i < list->count(); ++i) {
      const QListWidgetItem* it = list->item(i);
      if (it->data(Qt::UserRole).isNull()) continue;
      const QString url = it->data(ROW_URL_ROLE).toString();
      if (url.isEmpty() || doomed.contains(url)) continue;
      if (kindMatches(mode, it->data(Qt::UserRole).toBool())) out << url;
    }
    return out;
  }

  bool ConnectDialog::allShownSelected() const {
    const QStringList shown = shownUrls();
    if (shown.isEmpty()) return false;
    for (const QString& u : shown)
      if (!selected.contains(u)) return false;
    return true;
  }

  // Over the CURRENT filtered view; deselect clears the WHOLE selection.
  void ConnectDialog::toggleSelectAll() {
    if (allShownSelected()) selected.clear();
    else for (const QString& u : shownUrls()) selected.insert(u);
    rebuildList();
  }

  // An EXCLUDED row is gone at once (it was never disconnected); the rows LEFT arrive via support/filterFade.
  ListFilterFade* ConnectDialog::getFilterFade() {
    if (filterFade || !list) return filterFade;
    filterFade = new ListFilterFade(list);
    filterFade->writeRow = [this](QListWidgetItem* it, double p) {
      const QVariant full = it->data(FILTER_FULL_HEIGHT_ROLE);
      if (full.isValid()) it->setSizeHint(QSize(rowWidth(), filterHeight(full.toInt(), p)));
      QWidget* w = list->itemWidget(it);
      if (!w) return;
      // Settled either way, hand the row back to the scroll-edge reveal.
      if (p >= 1.0 || p <= 0.0) {
        if (w->property(FILTER_FADE_PROPERTY).toBool()) {
          w->setProperty(FILTER_FADE_PROPERTY, false);
          w->setGraphicsEffect(nullptr);
        }
        return;
      }
      w->setProperty(FILTER_FADE_PROPERTY, true);
      auto* fx = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setOpacity(filterOpacity(p));
    };
    filterFade->afterFrame = [this] { applyRowReveal(); };
    return filterFade;
  }

  void ConnectDialog::applyKindFilter() {
    if (!list) return;
    const QString mode =
        kindFilter ? kindFilter->currentData().toString() : QStringLiteral("all");
    // The placeholder goes first so real-row indices line up with manager->urls().
    for (int i = list->count() - 1; i >= 0; --i)
      if (list->item(i)->data(Qt::UserRole + 1).toBool()) delete list->takeItem(i);
    auto wanted = [&mode](QListWidgetItem* it) {
      if (it->data(Qt::UserRole).isNull()) return true;
      return kindMatches(mode, it->data(Qt::UserRole).toBool());
    };
    int rows = 0, shown = 0;
    for (int i = 0; i < list->count(); ++i) {
      QListWidgetItem* it = list->item(i);
      if (it->data(Qt::UserRole).isNull()) continue;
      ++rows;
      if (wanted(it)) ++shown;
    }
    if (auto* fade = getFilterFade()) fade->apply(wanted);
    updateBatchBar();
    if (rows == 0 || shown > 0) return;
    auto* none = new QListWidgetItem(mode == QLatin1String("admin")
                                         ? tr("No connection holds an admin credential.")
                                         : tr("Every connection holds an admin credential."),
                                     list);
    none->setData(Qt::UserRole + 1, true);
    none->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    none->setFlags(Qt::NoItemFlags);
  }

  // The projects list's card look (theme.cpp QListWidget::item) plus admin gold and expired amber
  // (browser .connect-expired). Set once; it cascades.
  QString ConnectDialog::rowStyleSheet() const {
    const QColor accent = palette().color(QPalette::Highlight);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor input = palette().color(QPalette::Base);
    const QColor info = infoBackground(dark);
    const QColor border = themePalette(dark).borderMain;
    // Browser .connect-row, value for value (css/components.css). The transient states come AFTER the
    // admin gold so they still win the border (its ring folded into a 2px border — box-shadow has no Qt spelling).
    return QString(
               // The app-wide sheet pads every QListWidget::item by 4px with its own fill; the slot is nothing, the card everything.
               "QListWidget#connList{border:none;background:transparent;}"
               "QListWidget::item{padding:0;background:transparent;border:none;}"
               "QListWidget::item:hover,QListWidget::item:selected{background:transparent;}"
               // With no label Qt still reserves the app-wide `spacing: 7px` to the box's right.
               "QCheckBox#connRowSelect{spacing:0px;}"
               "QWidget#connRow,QWidget#connRowAdmin,QWidget#connRowExpired{"
               "border:1px solid %1;border-radius:8px;background:%2;}"
               "QWidget#connRowAdmin{border:2px solid %3;}"
               "QWidget#connRowExpired{border:1px solid %4;background:%5;}"
               "QWidget#connRow[selected=\"true\"],QWidget#connRowAdmin[selected=\"true\"],"
               "QWidget#connRowExpired[selected=\"true\"]{border-color:%6;background:%7;}"
               "QWidget#connRow[hovered=\"true\"],QWidget#connRowAdmin[hovered=\"true\"],"
               "QWidget#connRowExpired[hovered=\"true\"]{background:%7;}"
               // Compact filled chrome: 31x25 matches the browser only BECAUSE the border is none; the content box
               // is pinned to the 15px glyph so the expired row's action is the same size.
               "QPushButton[rowAction=\"true\"]{background:%6;border:none;"
               "border-radius:4px;color:#ffffff;padding:5px 8px;"
               "min-height:15px;max-height:15px;}"
               "QPushButton#rowReconnect,QPushButton#expiredReconnect,"
               "QPushButton#rowDisconnect,QPushButton#inviteBtn{"
               "min-width:15px;max-width:15px;}"
               "QPushButton[rowAction=\"true\"]:hover{background:%8;}"
               "QPushButton[rowAction=\"true\"]:pressed{background:%9;}"
               "QPushButton#rowDisconnect{background:%10;}"
               "QPushButton#rowDisconnect:hover{background:%11;}"
               "QPushButton#expiredReconnect{background:%4;color:#1f1f1f;}"
               "QPushButton#expiredReconnect:hover{background:%12;}")
        .arg(border.name(), input.name(), GOLD.name(), AMBER.name(),
             mixSrgb(input, AMBER, 0.08).name(), accent.name(), info.name(),
             accentShade(accent, dark).name(), accentShade(accent, dark).name())
        .arg(themePalette(dark).danger.name(), dangerHover(dark).name(), AMBER_HOVER.name());
  }
}  // namespace stencil::gui

