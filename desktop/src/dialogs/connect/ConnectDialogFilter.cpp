#include "ConnectDialog.hpp"
#include "dialogSheets.hpp"
#include "connectDialogParts.hpp"
#include "../../support/theme/filterFade.hpp"

#include "theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QGraphicsOpacityEffect>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QSize>

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
    return support::connRowSheet({border, input, GOLD, AMBER, mixSrgb(input, AMBER, 0.08), accent, info,
                                  accentShade(accent, dark), accentShade(accent, dark),
                                  themePalette(dark).danger, dangerHover(dark), AMBER_HOVER});
  }
}  // namespace stencil::gui

