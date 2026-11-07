#include "ConnectDialog.hpp"
#include "../../support/control/reveal/modeGhost.hpp"
#include "connectDialogParts.hpp"
#include "../../support/motion/DisintegrateOverlay.hpp"  // disconnected rows come apart
#include "../../support/modal/modalChrome.hpp"          // the browser modal shell

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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
    setMaximumHeight(modalHeightCap(this));   // the rows grow it to the browser's cap, then scroll
    // The caret lands in the host field, as the browser window does (InfoDialog parity).
    QTimer::singleShot(0, urlEdit, [u = urlEdit] { u->setFocus(); });
  }

  QPushButton* ConnectDialog::buildConnectForm(QVBoxLayout* root) {
    root->addWidget(modalSectionLabel(tr("Connect a server"), this));
    // Browser .vs-row each: the label column, the field, a hairline under the row.
    constexpr int LABEL_W = 48;
    urlEdit = new QLineEdit;
    urlEdit->setPlaceholderText("http://localhost:8090");
    QWidget* urlRow = modalRow(this, tr("URL"), urlEdit, true, LABEL_W);
    if (QLabel* l = urlRow->findChild<QLabel*>()) l->setToolTip(tr("Server URL, e.g. http://localhost:8090"));
    root->addWidget(urlRow);
    tokenEdit = new QLineEdit;
    tokenEdit->setPlaceholderText(tr("(optional)"));
    QWidget* tokenRow = modalRow(this, tr("Token"), tokenEdit, true, LABEL_W);
    if (QLabel* l = tokenRow->findChild<QLabel*>()) l->setToolTip(tr("Optional access token (issued otherwise)"));
    root->addWidget(tokenRow);

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
    root->addWidget(modalRow(this, QString(), actions));
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
          QPointer<QWidget> wp(w);
          if (DisintegrateOverlay::over(w, this, DisintegrateOverlay::Sweep::GATHER, 0, 0,
                                        CONN_ARRIVE_MS)) {
            w->setVisible(false);   // the slot stays; the motes are what the eye follows
            QTimer::singleShot(support::motionMs(CONN_ARRIVE_MS), this, [wp] { if (wp) wp->setVisible(true); });
          } else if (playModeGhost(list->viewport(), w->grab(), QRect(w->mapTo(list->viewport(), QPoint()), w->size()),
                                   list->viewport()->rect(), /*in=*/true, Qt::Horizontal, MODE_ROW_MS,
                                   [wp] { if (wp) wp->setVisible(true); })) {
            w->setVisible(false);   // Sliding / Transition: its picture slides in or grows where it will stand
          }
        }
      });
    }
  }

}  // namespace stencil::gui
