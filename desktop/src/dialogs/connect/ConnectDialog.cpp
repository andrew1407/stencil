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


  ConnectDialog::ConnectDialog(stencil::net::ConnectionManager* manager, QWidget* parent)
      : QDialog(parent), manager(manager) {
    setWindowTitle(tr("Servers"));
    // The browser .app-modal width — also what fits the footer hint on one line.
    setMinimumWidth(MODAL_WIDTH);

    // Browser modal.js parity: shared modal shell (glyph + title + Close pill).
    ModalChrome chrome = installModalChrome(this, "server", tr("Servers"));
    QVBoxLayout* root = chrome.body;

    QPushButton* connectBtn = buildConnectForm(root);
    buildConnectionPrefs(root);
    buildConnectBatchBar(root);
    buildConnectionList(root);
    // Footer (browser settings-footer): the browser's exact sentence (modal.js .footer-hint) -
    // the longer invite-token clause made the desktop hint wrap to a second line.
    addModalFooter(chrome,
                   tr("Connections are saved and (optionally) restored on open · "
                      "server projects show a golden outline."));

    QObject::connect(reconnectAllBtn, &QPushButton::clicked, this, [this] {
      if (this->manager) this->manager->reconnectAllAsync();  // changed() → rebuildList() as each resolves
      rebuildList();
    });
    // Return belongs to the DEFAULT button, from either field. Deliberately no returnPressed beside
    // it: a QLineEdit emits that AND lets the key reach the default button, firing doConnect twice.
    QObject::connect(connectBtn, &QPushButton::clicked, this, &ConnectDialog::doConnect);
    if (this->manager)
      QObject::connect(this->manager, &stencil::net::ConnectionManager::changed, this,
                       &ConnectDialog::rebuildList);

    rebuildList();
    // The caret lands in the host field, as the browser window does (InfoDialog parity).
    QTimer::singleShot(0, urlEdit, [u = urlEdit] { u->setFocus(); });
  }

  QPushButton* ConnectDialog::buildConnectForm(QVBoxLayout* root) {
    root->addWidget(modalSectionLabel(tr("Connect a server"), this));
    auto* form = new QGridLayout;
    form->setColumnStretch(1, 1);
    form->setHorizontalSpacing(10);
    auto* urlLbl = new QLabel(tr("URL"));
    urlLbl->setToolTip(tr("Server URL, e.g. http://localhost:8090"));
    form->addWidget(urlLbl, 0, 0);
    urlEdit = new QLineEdit;
    urlEdit->setPlaceholderText("http://localhost:8090");
    form->addWidget(urlEdit, 0, 1);
    auto* tokenLbl = new QLabel(tr("Token"));
    tokenLbl->setToolTip(tr("Optional access token (issued otherwise)"));
    form->addWidget(tokenLbl, 1, 0);
    tokenEdit = new QLineEdit;
    tokenEdit->setPlaceholderText(tr("(optional)"));
    form->addWidget(tokenEdit, 1, 1);
    root->addLayout(form);

    // Connect (left) + Reconnect all (right), grouped on one row.
    auto* actions = new QHBoxLayout;
    auto* connectBtn = new QPushButton(tr("Connect"));
    connectBtn->setToolTip(tr("Connect to the server at the URL above"));
    // Accent CTA with a white glyph (browser default-button treatment).
    makeModalCta(connectBtn, "plus-circle");
    actions->addWidget(connectBtn);
    // AFTER the layout has parented it: setDefault() registers with its QDialog by walking up its
    // parents, so on a parentless button it only sets a flag that autoDefault juggling then cleared.
    connectBtn->setDefault(true);
    actions->addStretch(1);
    auto* reconnectAllBtn = new QPushButton(tr("Reconnect all"));
    this->reconnectAllBtn = reconnectAllBtn;
    makeModalCta(reconnectAllBtn, "refresh");
    reconnectAllBtn->setToolTip(tr("Re-establish every connection (re-validate / reissue tokens)"));
    actions->addWidget(reconnectAllBtn);
    root->addLayout(actions);
    return connectBtn;
  }

  void ConnectDialog::rebuildList() {
    if (!manager || !list) return;
    // While removal dust is playing, the retired rows keep their blank slots and the
    // empty state must NOT appear yet — the settle callback in scatterRows finalizes.
    if (!doomed.isEmpty()) {
      updateBatchBar();
      return;
    }
    list->clear();
    // Nothing to re-establish -> the button could only fail.
    if (reconnectAllBtn) reconnectAllBtn->setEnabled(!manager->getClients().isEmpty());
    const QStringList urls = manager->urls();
    // Rows the previous build didn't have — they get the gather-in below.
    QSet<QString> fresh;
    for (const QString& u : urls)
      if (!known.contains(u)) fresh.insert(u);
    known = QSet<QString>(urls.begin(), urls.end());
    // Drop any selected urls that are no longer connected.
    for (const QString& u : selected.values())
      if (!urls.contains(u)) selected.remove(u);
    if (urls.isEmpty()) {
      auto* empty = new QListWidgetItem(tr("No servers connected."), list);
      empty->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
      empty->setFlags(Qt::NoItemFlags);
      updateBatchBar();
      return;
    }
    int rowIndex = 0;
    for (const QString& url : urls) addConnectionRow(url, rowIndex++);
    applyKindFilter();
    updateBatchBar();
    // Deferred a turn: the edge fade needs the rows' laid-out geometry.
    QTimer::singleShot(0, this, [this] { applyRowReveal(); });
    // Newly-connected rows materialize as the removal played backwards: the slot opens
    // blank and the dust GATHERS into the row (Sweep::GATHER — browser ghostIn parity).
    if (!fresh.isEmpty() && isVisible() && !support::motionReduced()) {
      // Deferred a turn so the view has laid the new rows out (the grab needs geometry).
      QTimer::singleShot(0, this, [this, fresh] {
        list->doItemsLayout();   // geometry first — the grab is only as good as it
        const QStringList now = manager ? manager->urls() : QStringList();
        for (const QString& u : fresh) {
          const int row = now.indexOf(u);
          QListWidgetItem* it = (row >= 0 && row < list->count()) ? list->item(row) : nullptr;
          QWidget* w = it ? list->itemWidget(it) : nullptr;
          if (!w) continue;
          if (!w->isVisible()) w->show();   // the view may not have polished it yet
          // On the CONTROL clock, not the row's: Select all arrives in the same turn, and a row still
          // forming after the button had landed read as the two appearing one after the other.
          if (DisintegrateOverlay::over(w, this, DisintegrateOverlay::Sweep::GATHER, 0, 0,
                                        CONN_ARRIVE_MS)) {
            w->setVisible(false);   // the slot stays; the motes are what the eye follows
            QPointer<QWidget> wp(w);
            QTimer::singleShot(CONN_ARRIVE_MS, this, [wp] { if (wp) wp->setVisible(true); });
          }
        }
      });
    }
  }

}  // namespace stencil::gui
