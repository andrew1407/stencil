#include "connectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../support/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../support/disintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/dissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../support/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../support/flowLayout.hpp"           // the batch bar wraps, never clips
#include "../support/guiHelpers.hpp"           // confirmYesNo()
#include "../support/modalChrome.hpp"          // the browser modal shell
#include "../support/modalReveal.hpp"          // motionReduced()
#include "../support/controlReveal.hpp"     // the batch bar comes and goes as sand
#include "../support/shimmerOverlay.hpp"       // the row's glass hover sweep

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"   // infoBackground: the browser's --bg-info, for the row hover
#include "reorderableListWidget.hpp"
#include "searchCombo.hpp"
#include "serverClient.hpp"

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

  // The urls whose rows the kind filter leaves on view — Select all's pool (browser
  // connectModal.js `shownUrls`). Read off the rows, never manager_->urls() by index:
  // a row retired under its removal dust keeps its slot after the manager has let go.
  QStringList ConnectDialog::shownUrls() const {
    QStringList out;
    if (!list_) return out;
    const QString mode =
        kindFilter_ ? kindFilter_->currentData().toString() : QStringLiteral("all");
    for (int i = 0; i < list_->count(); ++i) {
      const QListWidgetItem* it = list_->item(i);
      if (it->data(Qt::UserRole).isNull()) continue;   // placeholder lines
      const QString url = it->data(kRowUrlRole).toString();
      if (url.isEmpty() || doomed_.contains(url)) continue;
      if (kindMatches(mode, it->data(Qt::UserRole).toBool())) out << url;
    }
    return out;
  }

  bool ConnectDialog::allShownSelected() const {
    const QStringList shown = shownUrls();
    if (shown.isEmpty()) return false;
    for (const QString& u : shown)
      if (!selected_.contains(u)) return false;
    return true;
  }

  // Select-all toggles over the CURRENT filtered view, so a filtered "select all" never
  // sweeps up connections the user cannot see; deselect clears the WHOLE selection.
  void ConnectDialog::toggleSelectAll() {
    if (allShownSelected()) selected_.clear();
    else for (const QString& u : shownUrls()) selected_.insert(u);
    rebuildList();   // re-syncs every row's checkbox; ends in updateBatchBar
  }

  // The row transition: a row the picker EXCLUDES is gone at once — it was never
  // disconnected, so there is no exit to watch — and the rows that are LEFT arrive
  // (support/filterFade), deliberately quieter than the disconnect scatter.
  ListFilterFade* ConnectDialog::filterFade() {
    if (filterFade_ || !list_) return filterFade_;
    filterFade_ = new ListFilterFade(list_);
    filterFade_->writeRow = [this](QListWidgetItem* it, double p) {
      const QVariant full = it->data(kFilterFullHeightRole);
      if (full.isValid()) it->setSizeHint(QSize(rowWidth(), filterHeight(full.toInt(), p)));
      QWidget* w = list_->itemWidget(it);
      if (!w) return;
      // Settled either way — landed in, or out and hidden — hand the row back to the
      // scroll-edge reveal. A hidden row holding a graphics effect is bookkeeping nobody
      // can see and everything downstream has to work around.
      if (p >= 1.0 || p <= 0.0) {
        if (w->property(kFilterFadeProperty).toBool()) {
          w->setProperty(kFilterFadeProperty, false);
          w->setGraphicsEffect(nullptr);
        }
        return;
      }
      w->setProperty(kFilterFadeProperty, true);   // applyRowReveal skips it meanwhile
      auto* fx = dynamic_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
      if (!fx) {
        fx = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(fx);
      }
      fx->setOpacity(filterOpacity(p));
    };
    // The edge dissolve reads laid-out geometry, so it re-runs after every frame.
    filterFade_->afterFrame = [this] { applyRowReveal(); };
    return filterFade_;
  }

  void ConnectDialog::applyKindFilter() {
    if (!list_) return;
    const QString mode =
        kindFilter_ ? kindFilter_->currentData().toString() : QStringLiteral("all");
    // Any previous "nothing matches" line goes first, so the list holds only real rows
    // whenever something matches (their indices line up with manager_->urls()).
    for (int i = list_->count() - 1; i >= 0; --i)
      if (list_->item(i)->data(Qt::UserRole + 1).toBool()) delete list_->takeItem(i);
    auto wanted = [&mode](QListWidgetItem* it) {
      if (it->data(Qt::UserRole).isNull()) return true;   // "No servers connected." line
      return kindMatches(mode, it->data(Qt::UserRole).toBool());
    };
    int rows = 0, shown = 0;
    for (int i = 0; i < list_->count(); ++i) {
      QListWidgetItem* it = list_->item(i);
      if (it->data(Qt::UserRole).isNull()) continue;
      ++rows;
      if (wanted(it)) ++shown;
    }
    if (auto* fade = filterFade()) fade->apply(wanted);
    updateBatchBar();   // Select all's pool is the filtered view
    if (rows == 0 || shown > 0) return;
    // Appended AFTER the rows, so the indices above stay valid while it is up.
    auto* none = new QListWidgetItem(mode == QLatin1String("admin")
                                         ? tr("No connection holds an admin credential.")
                                         : tr("Every connection holds an admin credential."),
                                     list_);
    none->setData(Qt::UserRole + 1, true);
    none->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
    none->setFlags(Qt::NoItemFlags);
  }

  // The projects list's card look (theme.cpp QListWidget::item — 6px radius, accent-soft
  // hover, ghost row buttons like QPushButton#pointDelBtn) plus the connection states:
  // admin gold and expired amber (browser .connect-expired). Set once; it cascades.
  QString ConnectDialog::rowStyleSheet() const {
    const QColor accent = palette().color(QPalette::Highlight);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QColor input = palette().color(QPalette::Base);        // --input-bg
    const QColor info = infoBackground(dark);           // --bg-info
    const QColor border = themePalette(dark).borderMain;         // --border-main
    // The browser's .connect-row, value for value (css/components.css): a filled card on
    // --input-bg with a --border-main hairline at radius 8, hovering to --bg-info. The
    // transient states come AFTER the admin gold so they still win the border, as the
    // cascade there does — admin gold (its ring folded into a 2px border, box-shadow
    // having no Qt spelling), expired amber over an 8% wash, selected accent.
    return QString(
               // The app-wide sheet pads every QListWidget::item by 4px and gives it its
               // own hover/selected fill (theme.cpp); both are wrong here — the padding
               // squeezes the 43px card and the fill doubles the card's. The slot is
               // nothing, the card everything, and the list itself is frameless.
               "QListWidget#connList{border:none;background:transparent;}"
               "QListWidget::item{padding:0;background:transparent;border:none;}"
               "QListWidget::item:hover,QListWidget::item:selected{background:transparent;}"
               // The bare select box: the app-wide `spacing: 7px` is a label gap, and with
               // no label Qt still reserved it to the box's right — 7px the browser's
               // 16px .connect-select never has, pushing the dot away from it.
               "QCheckBox#connRowSelect{spacing:0px;}"
               "QWidget#connRow,QWidget#connRowAdmin,QWidget#connRowExpired{"
               "border:1px solid %1;border-radius:8px;background:%2;}"
               "QWidget#connRowAdmin{border:2px solid %3;}"
               "QWidget#connRowExpired{border:1px solid %4;background:%5;}"
               "QWidget#connRow[selected=\"true\"],QWidget#connRowAdmin[selected=\"true\"],"
               "QWidget#connRowExpired[selected=\"true\"]{border-color:%6;background:%7;}"
               "QWidget#connRow[hovered=\"true\"],QWidget#connRowAdmin[hovered=\"true\"],"
               "QWidget#connRowExpired[hovered=\"true\"]{background:%7;}"
               // …and the row's own buttons are the app's filled chrome, just compact:
               // accent fill, white glyph, no border, padding 5px 8px at radius 4 — 31x25,
               // matching the browser only BECAUSE the border is none. The content box is
               // pinned to the 15px glyph so every action is that size, the expired
               // row's included, or it sits 2px off the line beside the trash.
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
               // The expired row's fix wears amber, not the accent every other row action
               // wears — it matches the row it belongs to.
               "QPushButton#expiredReconnect{background:%4;color:#1f1f1f;}"
               "QPushButton#expiredReconnect:hover{background:%12;}")
        .arg(border.name(), input.name(), kGold.name(), kAmber.name(),
             mixSrgb(input, kAmber, 0.08).name(), accent.name(), info.name(),
             accentShade(accent, dark).name(), accentShade(accent, dark).name())
        .arg(themePalette(dark).danger.name(), dangerHover(dark).name(), kAmberHover.name());
  }
}  // namespace stencil::gui

