#include "ConnectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../../support/motion/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../../support/motion/DisintegrateOverlay.hpp"  // disconnected rows come apart
#include "../../support/motion/DissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../../support/theme/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../../support/control/FlowLayout.hpp"           // the batch bar wraps, never clips
#include "../../support/guiHelpers.hpp"           // confirmYesNo()
#include "../../support/modal/modalChrome.hpp"          // the browser modal shell
#include "../../support/modal/modalReveal.hpp"          // motionReduced()
#include "../../support/control/reveal/controlReveal.hpp"     // the batch bar comes and goes as sand
#include "../../support/motion/ShimmerOverlay.hpp"       // the row's glass hover sweep

#include "connectionStore.hpp"
#include "iconSet.hpp"
#include "theme.hpp"   // infoBackground: the browser's --bg-info, for the row hover
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

  void ConnectDialog::confirmDisconnect(const QString& url) {
    if (!manager) return;
    if (!confirmYesNo(this, tr("Disconnect server"), tr("Disconnect and forget %1?").arg(url)))
      return;
    scatterRows({url});
    manager->disconnectFrom(url);
    rebuildList();
  }

  void ConnectDialog::updateBatchBar() {
    if (!batchBar) return;
    const int n = selected.size();
    // The bar hosts Select all too, so it shows whenever the view has rows; only the selection-only
    // controls inside come and go with the checked set (connectModal.js updateBatchBar).
    const QStringList shown = shownUrls();
    // Opens at once, closes only once its contents have flown (support/controlReveal). NOT the
    // browser's animated bar slot: an animating bar re-lays out the list every frame.
    revealBar(batchBar, [this] { return !selected.isEmpty() || !shownUrls().isEmpty(); });
    // Text first, so the count is already right when its slot opens.
    if (batchCount) {
      batchCount->setText(tr("%1 selected").arg(n));
      revealControls(batchCount, n > 0);
    }
    // The GROUP is what flies — one flight, not one per button.
    if (batchSelectedGroup) revealControls(batchSelectedGroup, n > 0);
    if (selectAllBtn) {
      revealControls(selectAllBtn, !shown.isEmpty());
      // Label AND glyph say which way it goes: a check gathers, a cross lets go
      // (browser icons.js setSelectAllFace).
      const bool all = allShownSelected();
      selectAllBtn->setText(all ? tr("Deselect all") : tr("Select all"));
      selectAllBtn->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
    }
  }

  // The expired row's way back in (browser parity): ask the SERVER for a fresh session first, and
  // only if it refuses ask for a token. A session token or the server's ADMIN token both work.
  void ConnectDialog::reauthenticate(const QString& url) {
    QPointer<ConnectDialog> self(this);
    manager->reconnectAsync(url, [this, self, url](bool ok, QString err) {
      if (!self) return;
      if (ok) {
        rebuildList();
        return;
      }
      Q_UNUSED(err);   // the browser's wording names the server, not the refusal
      // The token prompt on the shell (browser connectModal.js), echoing dots.
      PromptSpec spec;
      spec.title = tr("Session expired");
      spec.message = tr("%1 refused the saved session. Paste an access token — or the "
                        "server's admin token, which mints a fresh session for you.")
                         .arg(url);
      spec.confirmLabel = tr("Reconnect");
      spec.confirmIcon = QStringLiteral("link");
      // Shown, not echoed as dots: a pasted token you cannot read is one you cannot check. Empty is
      // refused outright (browser parity: the same `validate` on app.prompt).
      spec.validate = [](const QString& t) {
        return t.isEmpty() ? tr("Paste a token to reconnect") : QString();
      };
      const auto token = promptModal(this, spec);
      if (!self || !token || token->isEmpty()) return;
      manager->reauthenticateAsync(url, *token, [this, self, url](bool ok, QString cerr) {
        if (!self) return;
        emit toast(ok ? tr("Reconnected to %1").arg(url) : tr("Reconnect failed — %1").arg(cerr),
                   !ok);
        rebuildList();
      });
    });
  }
}  // namespace stencil::gui

