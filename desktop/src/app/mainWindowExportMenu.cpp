#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "canvasTooltip.hpp"
#include "canvasWidget.hpp"
#include "dataExportController.hpp"
#include "incognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "notifications.hpp"
#include "numericInput.hpp"
#include "exportPreview.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "../support/shareImage.hpp"    // shareSheetAvailable — no Share button on Linux

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QActionGroup>
#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QGuiApplication>
#include <functional>

// MainWindow's action set: buildActions() (the app-wide QActions + hotkeys) and
// buildContextActions() (the canvas context-menu set). Split from mainWindow.cpp;
// same class, definitions only.

namespace stencil::gui {

  namespace {
    // Alt+hover preview for the same row (no new QMenu::hovered fires for a modifier
    // change). Watches `menu` AND every ancestor QMenu: a hover-opened submenu holds no
    // key grab of its own, so Qt can deliver a bare Alt to the chain's root instead.
    // Consumes only the Alt press/release itself.
    class AltPreviewFilter : public QObject {
     public:
      AltPreviewFilter(QMenu* menu, std::function<QImage(QAction*)> renderFor)
          : QObject(menu), menu_(menu), renderFor_(std::move(renderFor)) {
        for (QWidget* w = menu; qobject_cast<QMenu*>(w); w = w->parentWidget()) {
          w->installEventFilter(this);
          watched_.push_back(w);
        }
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (!watched_.contains(qobject_cast<QWidget*>(obj))) return false;
        // Gliding OFF the previewed row hides the preview (browser wireAltPreview
        // mouseleave parity): the open menu owns all pointer traffic, so its own moves
        // are the hover-out signal; landing on another previewable row re-shows it.
        if (e->type() == QEvent::MouseMove && support::exportPreviewOwner() == menu_) {
          auto* mm = qobject_cast<QMenu*>(obj);
          QAction* act =
              mm ? mm->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()) : nullptr;
          if (mm != menu_ || !act || mm->actionGeometry(act) != support::exportPreviewOwnerRect())
            support::hideExportPreview();
        }
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
          // Autorepeats still consumed (bare Alt must stay off the menu bar), but they
          // must not re-show — a platform that repeats a held modifier would replay
          // the preview's appearance for as long as Alt is down.
          if (QAction* act = static_cast<QKeyEvent*>(e)->isAutoRepeat() ? nullptr
                                                                        : menu_->activeAction()) {
            const QImage img = renderFor_(act);
            // A KEY-triggered appearance forms from the CURSOR (the row's centre is
            // the pointer-driven flights' origin) — user decision, both surfaces.
            if (!img.isNull())
              support::showExportPreview(img, menu_, menu_->actionGeometry(act), QCursor::pos());
          }
          // Consumed: a bare Alt reaching the menu bar enters mnemonic mode, stealing
          // focus and closing this popup (browser parity: popover.js preventDefault).
          return true;
        } else if (e->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
          // Released Alt pours the preview back into the CURSOR, mirroring the press.
          if (!static_cast<QKeyEvent*>(e)->isAutoRepeat())
            support::hideExportPreview(QCursor::pos());
          return true;
        }
        return false;
      }

     private:
      QMenu* menu_;
      QVector<QWidget*> watched_;
      std::function<QImage(QAction*)> renderFor_;
    };

    // Double-click / right-click on a copy/download-image TOOLBAR button opens its
    // export-options popup instead of the plain single-click action — the same split
    // browser's toolbar copy/download buttons use (js/ui/exportOptionsMenu.js). A
    // plain click is deferred (swallowed press/release + a short timer) so a following
    // dblclick can still cancel it; the button's own defaultAction sync (icon/tooltip/
    // enabled state) is untouched — only the trigger path is taken over.
    class ExportPopupFilter : public QObject {
     public:
      ExportPopupFilter(QToolButton* btn, QAction* act, QMenu* menu)
          : QObject(btn), btn_(btn), act_(act), menu_(menu) {
        timer_.setSingleShot(true);
        timer_.setInterval(250);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] {
          if (act_->isEnabled()) act_->trigger();
        });
      }

     protected:
      bool eventFilter(QObject* obj, QEvent* e) override {
        if (obj != btn_) return false;
        switch (e->type()) {
          case QEvent::MouseButtonPress:
            return static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton;
          case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton) return false;
            if (act_->isEnabled()) timer_.start();
            return true;
          case QEvent::MouseButtonDblClick:
            if (static_cast<QMouseEvent*>(e)->button() != Qt::LeftButton) return false;
            popup();
            return true;
          case QEvent::ContextMenu:
            popup();
            return true;
          default:
            return false;
        }
      }

     private:
      void popup() {
        timer_.stop();
        if (!act_->isEnabled()) return;
        menu_->popup(btn_->mapToGlobal(QPoint(0, btn_->height())));
      }
      QToolButton* btn_;
      QAction* act_;
      QMenu* menu_;
      QTimer timer_;
    };
  }  // namespace

  // Wire the Alt+hover live preview onto one export-variant menu (a nested context-menu
  // submenu, or a toolbar options popup) — call once, right after its actions are added.
  void MainWindow::wireExportPreviewHover(QMenu* menu) {
    // One render per row per menu-open: QMenu::hovered re-fires on every mouse move,
    // and renderToImage is a full native-resolution composite. Cleared on open AND
    // close, so a fresh open always renders against the current canvas.
    auto cache = std::make_shared<QHash<QAction*, QImage>>();
    auto renderFor = [this, cache](QAction* act) {
      const auto it = cache->constFind(act);
      if (it != cache->constEnd()) return it.value();
      const QImage img = exportVariantPreviewImage(act);
      cache->insert(act, img);
      return img;
    };
    connect(menu, &QMenu::hovered, this, [menu, renderFor](QAction* act) {
      if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier)) {
        const QImage img = renderFor(act);
        if (!img.isNull()) { support::showExportPreview(img, menu, menu->actionGeometry(act)); return; }
      }
      support::hideExportPreview();
    });
    connect(menu, &QMenu::aboutToShow, this, [cache] { cache->clear(); });
    connect(menu, &QMenu::aboutToHide, this, [cache] {
      cache->clear();
      support::hideExportPreview();
    });
    menu->installEventFilter(new AltPreviewFilter(menu, renderFor));
  }

  // The two toolbar buttons' own export-options popups (browser parity:
  // js/ui/exportOptionsMenu.js) — built once, reused on every double-click/right-click.
  // Called once, right after buildToolbar() (needs the live buttons).
  void MainWindow::wireExportOptionsPopups() {
    auto buildMenu = [this](bool copy) {
      auto* m = new QMenu(this);
      populateExportVariantMenu(m, copy);
      support::wireMenuRowPolish(m, this, /*compact=*/true);
      return m;
    };
    copyImageOptionsMenu_ = buildMenu(/*copy=*/true);
    saveImageOptionsMenu_ = buildMenu(/*copy=*/false);

    // NOT buttonForAction() — that only finds a CURRENTLY VISIBLE button, and with no
    // image loaded yet (construction time) the Image cluster's buttons start hidden
    // (mainWindowToolbar.cpp makeToolSection). The QToolButton object itself is built
    // once and persists (only its visibility toggles later), so any match by
    // defaultAction() — visible or not — is the right, permanent one to wire.
    auto wireButton = [this](QAction* act, QMenu* menu) {
      QToolButton* btn = nullptr;
      for (QToolButton* b : findChildren<QToolButton*>())
        if (b->defaultAction() == act) { btn = b; break; }
      if (!btn) return;
      support::revealMenuFrom(*menu, btn);
      btn->installEventFilter(new ExportPopupFilter(btn, act, menu));
    };
    wireButton(actCopyImage_, copyImageOptionsMenu_);
    wireButton(actSaveImage_, saveImageOptionsMenu_);
  }
}  // namespace stencil::gui

