#include "ConnectDialog.hpp"
#include "connectDialogParts.hpp"
#include "../support/scrollReveal.hpp"             // revealDissolve (scroll edge fade)
#include "../support/DisintegrateOverlay.hpp"  // disconnected rows come apart
#include "../support/DissolveEffect.hpp"       // scroll-edge grain dissolve
#include "../support/filterFade.hpp"           // filtered-out rows fade + collapse
#include "../support/FlowLayout.hpp"           // the batch bar wraps, never clips
#include "../support/guiHelpers.hpp"           // confirmYesNo()
#include "../support/modalChrome.hpp"          // the browser modal shell
#include "../support/modalReveal.hpp"          // motionReduced()
#include "../support/controlReveal.hpp"     // the batch bar comes and goes as sand
#include "../support/ShimmerOverlay.hpp"       // the row's glass hover sweep

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
    if (!manager_) return;
    if (!confirmYesNo(this, tr("Disconnect server"), tr("Disconnect and forget %1?").arg(url)))
      return;
    scatterRows({url});
    manager_->disconnectFrom(url);
    rebuildList();
  }

  void ConnectDialog::updateBatchBar() {
    if (!batchBar_) return;
    const int n = selected_.size();
    // The bar hosts Select all too, so it shows whenever the view has rows; only the
    // selection-only controls inside come and go with the checked set (browser parity:
    // connectModal.js updateBatchBar). A bar that never opens has nothing beneath it to
    // jump, and the swap inside it is the app's control reveal (support/controlReveal).
    const QStringList shown = shownUrls();
    // Opens at once, closes only once its contents have flown (support/controlReveal).
    // The browser animates the bar's OWN slot as well (connectModal.js), which a Qt item
    // view will not tolerate here: an animating bar re-lays out the list every frame, and
    // the view re-shows a row meant to be hidden under its gather motes.
    revealBar(batchBar_, [this] { return !selected_.isEmpty() || !shownUrls().isEmpty(); });
    // Text first, so the count is already right when its slot opens.
    if (batchCount_) {
      batchCount_->setText(tr("%1 selected").arg(n));
      revealControls(batchCount_, n > 0);
    }
    // The GROUP is what flies — one flight, not one per button.
    if (batchSelectedGroup_) revealControls(batchSelectedGroup_, n > 0);
    if (selectAllBtn_) {
      revealControls(selectAllBtn_, !shown.isEmpty());
      // Label AND glyph say which way it goes: a check gathers, a cross lets go
      // (browser icons.js setSelectAllFace).
      const bool all = allShownSelected();
      selectAllBtn_->setText(all ? tr("Deselect all") : tr("Select all"));
      selectAllBtn_->setIcon(labelIcon(all ? "x" : "check", QColor("#ffffff"), 13));
    }
  }

  // The expired row's way back in (browser parity): ask the SERVER for a fresh
  // session first — an open server just signs you in — and only if it refuses ask
  // for a token. Either a session token or the server's ADMIN token works: the
  // client's connect path already mints a session from an admin token.
  void ConnectDialog::reauthenticate(const QString& url) {
    QPointer<ConnectDialog> self(this);
    manager_->reconnectAsync(url, [this, self, url](bool ok, QString err) {
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
      // Shown, not echoed as dots: the Token field a few rows above is plain text too,
      // and a pasted token you cannot read is one you cannot check. Empty is
      // refused outright — the button would otherwise be a dead click (browser parity:
      // the same `validate` on app.prompt).
      spec.validate = [](const QString& t) {
        return t.isEmpty() ? tr("Paste a token to reconnect") : QString();
      };
      const auto token = promptModal(this, spec);
      if (!self || !token || token->isEmpty()) return;
      manager_->reauthenticateAsync(url, *token, [this, self, url](bool ok, QString cerr) {
        if (!self) return;
        emit toast(ok ? tr("Reconnected to %1").arg(url) : tr("Reconnect failed — %1").arg(cerr),
                   !ok);
        rebuildList();
      });
    });
  }
}  // namespace stencil::gui

