#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasTooltip.hpp"
#include "CanvasWidget.hpp"
#include "DataExportController.hpp"
#include "IncognitoOverlay.hpp"
#include "modalReveal.hpp"
#include "Notifications.hpp"
#include "numericInput.hpp"
#include "exportPreview.hpp"
#include "menuReveal.hpp"
#include "menuRowPolish.hpp"
#include "../support/modalChrome.hpp"   // confirmModal — the browser-styled question
#include "../support/shareImage.hpp"    // isShareSheetAvailable — no Share button on Linux

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

// The export-variant menus' Alt+hover previews and the toolbar buttons' export-options popups.

namespace stencil::gui {

  namespace {
    // Watches `menu` AND every ancestor QMenu: a hover-opened submenu holds no key grab, so Qt may deliver a bare Alt to the root.
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
        // Gliding OFF the previewed row hides the preview (browser wireAltPreview mouseleave parity).
        if (e->type() == QEvent::MouseMove && support::exportPreviewOwner() == menu_) {
          auto* mm = qobject_cast<QMenu*>(obj);
          QAction* act =
              mm ? mm->actionAt(static_cast<QMouseEvent*>(e)->position().toPoint()) : nullptr;
          if (mm != menu_ || !act || mm->actionGeometry(act) != support::exportPreviewOwnerRect())
            support::hideExportPreview();
        }
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
          // Autorepeats are consumed but must not re-show the preview.
          if (QAction* act = static_cast<QKeyEvent*>(e)->isAutoRepeat() ? nullptr
                                                                        : menu_->activeAction()) {
            const QImage img = renderFor_(act);
            // A KEY-triggered appearance forms from the CURSOR.
            if (!img.isNull())
              support::showExportPreview(img, menu_, menu_->actionGeometry(act), QCursor::pos());
          }
          // Consumed: a bare Alt reaching the menu bar enters mnemonic mode and closes this popup.
          return true;
        } else if (e->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Alt) {
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

    // Double-click / right-click on a copy/download TOOLBAR button opens its export-options popup (browser exportOptionsMenu.js);
    // a plain click is deferred so a following dblclick can cancel it. The button's defaultAction sync is untouched.
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

  // Wire the Alt+hover preview onto one export-variant menu — once, right after its actions are added.
  void MainWindow::wireExportPreviewHover(QMenu* menu) {
    // One render per row per open: hovered re-fires on every move and renderToImage is a full composite.
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

  // The toolbar buttons' export-options popups (browser exportOptionsMenu.js), built once after buildToolbar().
  void MainWindow::wireExportOptionsPopups() {
    auto buildMenu = [this](bool copy) {
      auto* m = new QMenu(this);
      populateExportVariantMenu(m, copy);
      support::wireMenuRowPolish(m, this, /*compact=*/true);
      return m;
    };
    copyImageOptionsMenu_ = buildMenu(/*copy=*/true);
    saveImageOptionsMenu_ = buildMenu(/*copy=*/false);

    // NOT buttonForAction(): it finds only a VISIBLE button, and the Image cluster starts hidden. The QToolButton persists, so a defaultAction() match is permanent.
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

