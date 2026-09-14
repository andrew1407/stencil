// MainWindow GUI e2e — The assistant dock and its mirror panel: showing, placing, dragging,
// swapping sides, the compact popover, toasts and the menu-panel twin.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }






  // The chat dock's resize edge (browser .chat-resizer). The strip is QMainWindow chrome
  // with no widget of its own, so a mouse-transparent band is painted over it, in whichever
  // area the dock sits and only where Qt would actually start the resize.
  void chatResizeEdgeFollowsTheDockInEveryArea() {
    MainWindow win(nullptr, false);
    win.resize(1000, 700);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.chatDock_);
    win.chatDock_->show();
    settleLayout(&win, 120);
    auto* edge = win.chatEdge_;
    QVERIFY(edge);
    const struct { Qt::DockWidgetArea area; Qt::Orientation split; const char* name; } AREAS[] = {
        {Qt::LeftDockWidgetArea, Qt::Horizontal, "left"},
        {Qt::RightDockWidgetArea, Qt::Horizontal, "right"},
        {Qt::TopDockWidgetArea, Qt::Vertical, "top"},
        {Qt::BottomDockWidgetArea, Qt::Vertical, "bottom"},
    };
    for (const auto& a : AREAS) {
      win.addDockWidget(a.area, win.chatDock_, a.split);
      settleLayout(&win, 120);
      const QRect dock = win.chatDock_->geometry();
      const QRect hit = win.chatEdgeHit_;
      const QRect band = edge->geometry();
      QVERIFY2(edge->isVisible(), a.name);
      QVERIFY2(!hit.isEmpty(), a.name);
      // The strip sits OUTSIDE the panel, against the edge it is docked by.
      QVERIFY2(!hit.intersects(dock), a.name);
      if (a.area == Qt::LeftDockWidgetArea) QCOMPARE(hit.left(), dock.right() + 1);
      if (a.area == Qt::RightDockWidgetArea) QCOMPARE(hit.right(), dock.left() - 1);
      if (a.area == Qt::TopDockWidgetArea) QCOMPARE(hit.top(), dock.bottom() + 1);
      if (a.area == Qt::BottomDockWidgetArea) QCOMPARE(hit.bottom(), dock.top() - 1);
      // …and the band is drawn AROUND that strip, never thinner than an affordance can
      // be seen at (the horizontal separators are a hairline by design, theme.cpp).
      QVERIFY2(band.contains(hit), a.name);
      QVERIFY2(qMin(band.width(), band.height()) >= stencil::gui::DockEdgeOverlay::MIN_THICKNESS, a.name);
    }
    // Nothing to grab while it floats — the window frame owns that resize.
    win.chatDock_->setFloating(true);
    QTRY_VERIFY(!edge->isVisible());
  }

  // The AI-Assistant chat dock: the checkable toolbar/View action opens and closes it;
  // unlike the deliberately pinned selection panel it is dockable on all four sides and
  // floatable/closable, defaults LEFT on a fresh run, hosts the transcript/input in a
  // user-resizable splitter, gates Send on input/busy state, and turns clipboard-pasted
  // images into attachments (plain text pastes normally).
  void chatDockToggles() {
    // A saved dock layout would restore whatever area the last run used; clear it so
    // this asserts the FIRST-RUN default (left, matching the browser).
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(dock);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // first-run default
    // The COMPOSER acts on a drop; the DOCK swallows the ones that miss it, so a
    // gesture aimed at the chat can never reach the window and offer to open the
    // image as a project (browser chatPanel.js parity). The composer's QPlainTextEdit
    // declines drops too, or it would swallow one and paste the path as text.
    QVERIFY(dock->acceptDrops());
    auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
    QVERIFY(inputArea);
    QVERIFY(inputArea->acceptDrops());  // image/video drops become chat attachments
    auto* chatInput = dock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(chatInput && !chatInput->acceptDrops() && !chatInput->viewport()->acceptDrops());
    // …cued by an animated icon over the composer, hidden until a drag arrives.
    auto* cue = dock->findChild<QWidget*>("chatDropCue");
    QVERIFY(cue && cue->isHidden());

    // The composer's resize grip is the SHARED pill (PillSplitter.hpp), not a
    // stylesheet handle: the stylesheet one could only be narrowed by a symmetric
    // margin, so it stretched with the panel into a fat accent band across the dock.
    auto* chatSplitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(chatSplitter);
    QVERIFY2(dynamic_cast<stencil::gui::PillSplitterHandle*>(chatSplitter->handle(1)) != nullptr,
             "the dock's grip is not the shared pill handle");
    // A grab of the empty dock, for eyeballing the composer end-to-end.
    if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
      dock->resize(360, 520);
      QTest::qWait(50);
      dock->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-empty.png");
    }

    // Resizable input area: transcript over input in a vertical, non-collapsible splitter.
    auto* splitter = dock->findChild<QSplitter*>("chatSplitter");
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QCOMPARE(splitter->count(), 2);
    QVERIFY(!splitter->childrenCollapsible());

    // By objectName, NOT by text: the dock's internal toggleViewAction shares
    // the "AI Assistant" label.
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(chat);
    QVERIFY(chat->isCheckable());
    chat->setChecked(false);         // normalize (a restored layout may have opened it)
    QTRY_VERIFY(!dock->isVisible());
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());

    // The sparkle toolbar button mirrors actChat (setDefaultAction): same
    // toggle, checked while the dock is open (browser sparkle-button parity).
    QToolButton* chatBtn = nullptr;
    for (QToolButton* b : win.findChildren<QToolButton*>())
      if (b->defaultAction() == chat) { chatBtn = b; break; }
    QVERIFY(chatBtn);
    QVERIFY(chatBtn->isChecked());
    chatBtn->click();
    QTRY_VERIFY(!dock->isVisible());
    chatBtn->click();
    QTRY_VERIFY(dock->isVisible());

    // Right-side docking must work even though the fixed-width selection panel owns
    // that area — nesting provides the drop slots (regression: the chat dock could
    // not be pinned to the right at all).
    QVERIFY(win.isDockNestingEnabled());
    auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
    QVERIFY(selPanel && win.dockWidgetArea(selPanel) == Qt::RightDockWidgetArea);
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(dock->isVisible());
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);  // restore for the rest of the slot
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // First tear-off adopts the compact default size (not the docked span that
    // used to stretch the floating panel across the whole window).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);

    // Send gating: disabled while the input is empty, enabled once text lands.
    auto* input = dock->findChild<QPlainTextEdit*>("chatInput");
    auto* send = dock->findChild<QToolButton*>("chatSend");
    QVERIFY(input && send);
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");  // settings target
    auto* moreBtn = dock->findChild<QToolButton*>("chatMore");   // the … overflow
    QVERIFY(gearBtn && moreBtn);
    // The status dot is a BADGE on the … TRIGGER (browser .conn-status parity):
    // child of the button, no layout slot of its own. The gear itself now lives
    // in the menu, so it is hidden — the dot has to ride what stays visible.
    auto* dot = dock->findChild<QLabel*>("chatStatusDot");
    QVERIFY(dot && dot->parentWidget() == moreBtn);
    // Composer = send + …, in that order, both the filled-accent buttons.
    QVERIFY(send->x() < moreBtn->x());
    QVERIFY(send->property("chatAccent").toBool());
    QVERIFY(moreBtn->property("chatAccent").toBool());
    // Attach / clear / settings are reachable ONLY through the … menu now.
    auto* attachBtn = dock->findChild<QToolButton*>("chatAttach");
    auto* clearInRow = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(attachBtn && clearInRow);
    QVERIFY(attachBtn->isHidden() && gearBtn->isHidden());
    QVERIFY(moreBtn->menu());
    QStringList items;
    for (QAction* a : moreBtn->menu()->actions())
      if (!a->isSeparator()) items << a->text();
    QCOMPARE(items, (QStringList{"Add image", "Clear history", "Swap message sides", "Settings"}));
    // The branded header IS the title bar (no double header).
    QVERIFY(dock->titleBarWidget());
    QVERIFY(dock->titleBarWidget()->findChild<QLabel*>("chatHeaderTitle"));

    // Card container: the dock content renders on the controls-panel colour —
    // DISTINCT from the canvas backdrop behind it — with the themed 1px
    // border styling applied to the whole card.
    {
      const QImage bodyImg = dock->widget()->grab().toImage();
      const QColor cardBg = bodyImg.pixelColor(bodyImg.width() / 2, 4);
      const QImage centralImg = win.centralWidget()->grab().toImage();
      // Near the BOTTOM, not the exact vertical center: the top bars (toolbars,
      // image-info strip) are a few rows tall and grow/shrink with theme/content
      // changes — a center sample can drift onto one of them by coincidence. The
      // bottom stays safely inside the canvas/page area regardless.
      const QColor canvasBg =
          centralImg.pixelColor(centralImg.width() / 2, centralImg.height() - 10);
      QVERIFY(cardBg != canvasBg);
      QVERIFY(dock->styleSheet().contains("border:1px solid"));
    }
    QVERIFY(!send->isEnabled());
    input->setPlainText("make it sepia");
    QVERIFY(send->isEnabled());
    input->clear();
    QVERIFY(!send->isEnabled());

    // Clipboard paste: an image on the clipboard becomes an attachment…
    auto* chatDock = qobject_cast<stencil::gui::ChatDock*>(dock);
    QVERIFY(chatDock);
    QCOMPARE(chatDock->attachedImages().size(), 0);
    QImage clip(20, 10, QImage::Format_RGB32);
    clip.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(clip);
    input->setFocus();
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(chatDock->attachedImages().size(), 1);
    QVERIFY(input->toPlainText().isEmpty());  // consumed as an attachment, not text
    // …while plain text still pastes normally (no extra attachment).
    QGuiApplication::clipboard()->setText("hello there");
    QTest::keySequence(input, QKeySequence::Paste);
    QCOMPARE(input->toPlainText(), QString("hello there"));
    QCOMPARE(chatDock->attachedImages().size(), 1);
    chatDock->clearAttachments();
    input->clear();

    // Cohesive-column chrome (browser parity): in-content header title and a
    // BORDERLESS transcript (browser .chat-transcript parity — the card look
    // comes from the dock's own panel background + border, not an inner frame).
    QVERIFY(dock->findChild<QLabel*>("chatHeaderTitle"));
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->frameShape() == QFrame::NoFrame);

    // Empty-state suggestion chips: shown while the transcript is empty; a
    // click PREFILLS the composer (never sends); gone once the first card lands.
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && suggest->isVisible());
    // Chips lay out INLINE with wrapping (flow layout), not one per row.
    QVERIFY(suggest->layout());
    QVERIFY(!qobject_cast<QVBoxLayout*>(suggest->layout()));
    QVERIFY(suggest->layout()->hasHeightForWidth());
    const auto chips = dock->findChildren<QPushButton*>("chatSuggestChip");
    QCOMPARE(chips.size(), 4);
    chips.first()->click();
    QCOMPARE(input->toPlainText(), QString("Make it sepia"));
    QVERIFY(send->isEnabled());  // prefilled, not sent
    QVERIFY(suggest->isVisible());
    input->clear();
    chatDock->appendAssistant("done");  // first transcript card
    QVERIFY(!suggest->isVisible());

    // The attach-routing sniffers the paste/drop flows share (mediaLoader).
    QVERIFY(stencil::gui::isVideoFileName("clip.MP4"));
    QVERIFY(stencil::gui::isVideoFileName("/tmp/a.webm"));
    QVERIFY(!stencil::gui::isVideoFileName("photo.png"));
    QVERIFY(stencil::gui::isImageFileName("photo.JPEG"));
    QVERIFY(!stencil::gui::isImageFileName("clip.mp4"));

    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // The chat dock is session-transient (browser full-reset-on-reload parity): a
  // saved window layout must NOT resurrect it — every launch starts hidden at
  // the default left placement. The selection panel DOES restore, and its
  // toggle action must track the restored visibility so the header chevron
  // ("Hide panel", Alt+X) still works after a restart (regression: the pre-show
  // isVisible() sync left the action unchecked, so the chevron no-opped).
  void chatDockSessionTransientAndPanelToggleAfterRestore() {
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 1: open + float the chat dock, then persist the layout the way
    // closeEvent does.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(dock && chat);
      chat->setChecked(true);
      QTRY_VERIFY(dock->isVisible());
      dock->setFloating(true);
      QTRY_VERIFY(dock->isFloating());
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState = QString::fromLatin1(win.saveState().toBase64());
      stencil::gui::fileStore::saveSettings(s);
    }
    // Session 2: chat dock reset to hidden/docked-left/unchecked; the selection
    // panel's toggle is synced to the RESTORED visibility and the chevron works.
    {
      MainWindow win(nullptr, false);
      win.resize(1200, 800);
      win.show();
      QVERIFY(QTest::qWaitForWindowExposed(&win));
      auto* dock = win.findChild<QDockWidget*>("llmChatDock");
      QVERIFY(dock);
      QVERIFY(dock->isHidden());
      QVERIFY(!dock->isFloating());
      QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
      auto* chat = win.findChild<QAction*>("actChat");
      QVERIFY(chat && !chat->isChecked());

      auto* selPanel = win.findChild<QDockWidget*>("selectionPanelDock");
      QVERIFY(selPanel && !selPanel->isHidden());
      QAction* panelAct = nullptr;
      for (QAction* a : win.findChildren<QAction*>())
        if (a->text() == "Selection Panel") { panelAct = a; break; }
      QVERIFY(panelAct);
      QVERIFY(panelAct->isChecked());  // synced to the restored visibility
      // Header chevron path: collapseRequested → actPanel_ → animated hide.
      QVERIFY(QMetaObject::invokeMethod(selPanel, "collapseRequested"));
      QTRY_VERIFY(selPanel->isHidden());
      QVERIFY(!panelAct->isChecked());
    }
    {
      stencil::gui::Settings s = stencil::gui::fileStore::loadSettings();
      s.windowState.clear();
      stencil::gui::fileStore::saveSettings(s);
    }
    beat();
  }

  // The chat dock SLIDES in and out (browser chat-panel parity) instead of
  // popping: docked left, its WIDTH animates 0 → natural on open and back to 0
  // on close (then it hides). Sampled like fullscreenRevealAnimatesSmoothly.
  // Afterwards the size constraints must be released again (the min==max
  // pinning is animation-only) so the dock stays user-resizable, and a reopen
  // returns to the extent it had before the dismissal. Floating docks are their
  // own windows, so they just show/hide.
  void chatDockSlideAnimation() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    QCOMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);  // width axis

    // Sample the docked extent every ~16 ms across the slide (a hidden dock
    // counts as 0 — that IS its contribution to the layout).
    auto sample = [&] {
      QList<int> s;
      QElapsedTimer t;
      t.start();
      do {
        s.append(dock->isVisible() ? dock->width() : 0);
        QTest::qWait(16);
      } while (win.chatAnim_ && t.elapsed() < 1200);
      return s;
    };
    auto hasIntermediate = [](const QList<int>& s, int full) {
      for (int v : s) if (v > 2 && v < full - 2) return true;
      return false;
    };

    // ── open: 0 → natural ──
    chat->setChecked(true);
    QVERIFY(dock->isVisible());  // shown immediately; it grows into place
    const QList<int> up = sample();
    QTRY_VERIFY(dock->width() > 200);
    const int full = dock->width();
    QVERIFY2(hasIntermediate(up, full),
             "chat dock popped open (no intermediate widths)");
    // The pinning is released at the end: still resizable, and the dock's own
    // 260 px floor is back (not clamped to the animation's last frame).
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(dock->minimumWidth(), 260);

    // ── close: natural → 0, then hidden ──
    chat->setChecked(false);
    QVERIFY(dock->isVisible());  // still on screen while it slides out
    const QList<int> down = sample();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY2(hasIntermediate(down, full),
             "chat dock popped shut (no intermediate widths)");
    for (int i = 1; i < down.size(); ++i)
      QVERIFY2(down[i] <= down[i - 1] + 1, "chat dock hide width oscillated");
    QTRY_COMPARE(dock->maximumWidth(), QWIDGETSIZE_MAX);

    // Reopening restores the extent it was dismissed at.
    chat->setChecked(true);
    QTRY_VERIFY(dock->width() > 200);
    awaitAnim(win.chatAnim_);
    QVERIFY2(qAbs(dock->width() - full) <= 8, "reopen lost the remembered width");

    // Floating: no edge to slide from — plain show/hide, and the tear-off size
    // is never clamped by a leftover animation constraint.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    chat->setChecked(false);
    QVERIFY(!dock->isVisible());  // immediate, no slide
    chat->setChecked(true);
    QVERIFY(dock->isVisible());
    QTRY_COMPARE(dock->size(), QSize(385, 480));   // +5px of room for the row "…"
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }

  // Moving the chat between sides animates: it slides out of the old edge and back in at
  // the new one (the same extent slide the icon's open/close uses). It used to jump.
  void chatPlacementChangeAnimates() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && !dock->isFloating());
    awaitAnim(win.chatAnim_);
    const int settled = dock->width();
    QVERIFY2(settled > 80, "the dock never reached its full width");
    // Ask for the opposite side and watch the extent actually move mid-flight.
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::RightDockWidgetArea);
    bool sawCollapse = false;
    for (int i = 0; i < 20 && !sawCollapse; ++i) {
      QTest::qWait(20);
      const int e = dock->isFloating() ? settled
                                       : (win.dockWidgetArea(dock) == Qt::TopDockWidgetArea
                                          || win.dockWidgetArea(dock) == Qt::BottomDockWidgetArea)
                                             ? dock->height() : dock->width();
      if (e < settled / 2) sawCollapse = true;
    }
    QVERIFY2(sawCollapse, "the dock jumped to the new side without sliding out");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew back at the new edge");
    awaitAnim(win.chatAnim_);

    // …and coming back from FLOAT slides in at the side you picked, rather than
    // appearing at full width (the floating branch used to skip the animation).
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    awaitAnim(win.chatAnim_);
    emit static_cast<stencil::gui::ChatDock*>(dock)->dockRequested(Qt::LeftDockWidgetArea);
    bool sawNarrow = false;
    for (int i = 0; i < 20 && !sawNarrow; ++i) {
      QTest::qWait(15);
      if (!dock->isFloating() && dock->width() < settled / 2) sawNarrow = true;
    }
    QVERIFY2(sawNarrow, "docking from float snapped straight to full width");
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QTRY_VERIFY2(dock->width() > settled / 2, "it never grew in from the float");
  }

  // Docking the chat onto the SAME side as the points panel used to let the two fight
  // over width: the panel would balloon or collapse mid-slide (Qt's dock layout freely
  // redistributing space between two flexible siblings), and that bad width then got
  // captured as the "restore" size, reappearing very wide on the next reopen. Browser
  // parity (layout.css .main-content flex row): the chat overlay only ever eats into the
  // canvas column — the fixed-width panel beside it never moves. Regression for that bug.
  void chatSharingPanelSideKeepsPanelWidthStable() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Chat starts docked LEFT by default — open it (an UNRELATED area, so this alone
    // settles QMainWindow's dock layout onto the panel's real natural width, not
    // whatever incidental size it had straight out of construction) and let it settle.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    awaitAnim(win.chatAnim_);
    const int panelBefore = win.selPanel_->width();
    QVERIFY2(panelBefore > 120, "the points panel never reached its natural width");

    // Now place the chat RIGHT, alongside the points panel, and watch the panel's
    // width through the whole flight.
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    int maxSeen = 0, minSeen = win.width();
    QVERIFY2(win.chatAnim_, "the placement change did not animate");
    QElapsedTimer flightClock;
    flightClock.start();
    while (win.chatAnim_ && flightClock.elapsed() < 1500) {   // the flight's own length
      QTest::qWait(15);
      if (win.selPanel_->isHidden()) continue;
      const int w = win.selPanel_->width();
      maxSeen = std::max(maxSeen, w);
      minSeen = std::min(minSeen, w);
    }
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);
    // They must land SIDE BY SIDE (same row, chat to the right of the panel) —
    // never stacked vertically (Qt's plain, unsplit addDockWidget default).
    QCOMPARE(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(), win.chatDock_->mapTo(&win, QPoint(0, 0)).y());
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "chat did not land to the right of the points panel");
    // The panel must never balloon past its pre-share width, nor get squeezed away —
    // the chat's own slide is what should move, not the panel sitting beside it.
    QVERIFY2(maxSeen <= panelBefore + 8,
             qPrintable(QString("points panel widened to %1 (was %2)").arg(maxSeen).arg(panelBefore)));
    QVERIFY2(minSeen >= 100,
             qPrintable(QString("points panel collapsed to %1 mid-slide").arg(minSeen)));

    // Close the chat: the panel should hand its width right back...
    win.actChat_->setChecked(false);
    QTRY_VERIFY(!win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() >= panelBefore - 8,
             qPrintable(QString("panel stayed narrow after chat closed: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));

    // ...and reopening the chat (sharing again) must not have baked a bad "restore"
    // width into the panel from the earlier fight — it settles back near its own size,
    // never "very wide".
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible());
    awaitAnim(win.chatAnim_);
    QVERIFY2(win.selPanel_->width() <= panelBefore + 8,
             qPrintable(QString("panel reopened very wide: %1 (was %2)")
                            .arg(win.selPanel_->width()).arg(panelBefore)));
  }

  // The points panel can be HIDDEN (no image loaded, or collapsed by the user) at the
  // moment the chat gets placed onto its side — dockChatTo used to gate its split on the
  // panel being visible right then, so the two were left plain-stacked (Qt's unsplit
  // addDockWidget default: one squashed row above the other). Showing the panel again
  // later never re-split them — it just reappeared squashed under the chat. Regression
  // for that; ensurePanelChatSplit must repair it wherever either dock's visibility flips.
  void chatPlacedWhilePanelHiddenStillSplitsSideBySide() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    QVERIFY(win.selPanel_ && win.chatDock_ && win.actPanel_ && win.actChat_);
    QVERIFY(win.dockWidgetArea(win.selPanel_) == Qt::RightDockWidgetArea);
    QTRY_VERIFY(!win.selPanel_->isHidden());

    // Hide the points panel FIRST...
    win.actPanel_->setChecked(false);
    QTRY_VERIFY(win.selPanel_->isHidden());

    // ...then place the (unrelated-side) chat onto the panel's side while it's hidden.
    win.actChat_->setChecked(true);
    QTRY_VERIFY(win.chatDock_->isVisible() && !win.chatDock_->isFloating());
    emit static_cast<stencil::gui::ChatDock*>(win.chatDock_)->dockRequested(Qt::RightDockWidgetArea);
    QTRY_COMPARE(win.dockWidgetArea(win.chatDock_), Qt::RightDockWidgetArea);
    awaitAnim(win.chatAnim_);

    // Now reveal the panel again — it must come back BESIDE the chat, not squashed
    // underneath it.
    win.actPanel_->setChecked(true);
    QTRY_VERIFY(!win.selPanel_->isHidden());
    // The re-laid-out row, not a guess at how long it takes to arrive.
    QTRY_COMPARE_WITH_TIMEOUT(win.selPanel_->mapTo(&win, QPoint(0, 0)).y(),
                              win.chatDock_->mapTo(&win, QPoint(0, 0)).y(), 1000);
    QVERIFY2(win.chatDock_->mapTo(&win, QPoint(0, 0)).x() > win.selPanel_->mapTo(&win, QPoint(0, 0)).x(),
             "the panel reappeared stacked under the chat instead of beside it");
    // Squashed means SHARING a vertical row with the chat — not "shorter than half
    // the window": the stacked toolbars and the top info dock can leave the whole
    // dock row well under half of it. Side by side, both fill that row.
    QCOMPARE(win.selPanel_->height(), win.chatDock_->height());
    QVERIFY(win.centralWidget());
    QVERIFY2(win.selPanel_->height() == win.centralWidget()->height(),
             "the panel came back with a squashed, shared-row height");
  }

  // The title-bar X leaves the SAME way the toolbar toggle does — a docked chat
  // slides into whichever edge it is docked to, a float flies into the icon —
  // for all four dock areas plus floating. It used to call QWidget::close() and
  // simply blink out. Reopening afterwards still works, with the transcript kept.
  void chatCloseButtonAnimatesFromEveryDockArea() {
    const QByteArray noAnim = qgetenv("STENCIL_NO_ANIM");
    qunsetenv("STENCIL_NO_ANIM");
    const auto restoreAnim = qScopeGuard([&] { if (!noAnim.isEmpty()) qputenv("STENCIL_NO_ANIM", noAnim); });
    MainWindow win(nullptr, false);
    win.resize(1200, 820);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;
    QVERIFY(dock);
    QToolButton* closeBtn = dock->findChild<QToolButton*>();
    // The X is the last ghost in the title bar; find it by its tooltip.
    closeBtn = nullptr;
    for (QToolButton* b : dock->findChildren<QToolButton*>())
      if (b->toolTip() == QLatin1String("Close assistant")) closeBtn = b;
    QVERIFY2(closeBtn, "no X in the chat title bar");
    win.chatDock_->appendUser(QStringLiteral("kept across the close"));

    // The float's exit is a cloud of its own pixels flown inside the main window, every
    // mote pouring back into the icon.
    const auto flight = [&win] { return surfaceFlight(&win); };
    const auto settle = [&win] { awaitAnim(win.chatAnim_); awaitFlights(&win); };

    struct Area { Qt::DockWidgetArea area; const char* name; };
    const QVector<Area> areas{{Qt::LeftDockWidgetArea, "left"},
                              {Qt::RightDockWidgetArea, "right"},
                              {Qt::TopDockWidgetArea, "top"},
                              {Qt::BottomDockWidgetArea, "bottom"}};
    for (const Area& a : areas) {
      win.addDockWidget(a.area, dock);
      dock->setFloating(false);
      win.actChat_->setChecked(true);
      win.setChatShown(true, false);
      QTRY_VERIFY(dock->isVisible());
      settle();
      QCOMPARE(win.dockWidgetArea(dock), a.area);

      closeBtn->click();
      // Mid-slide: the extent animation is running and it has NOT blinked out.
      QVERIFY2(win.chatAnim_ != nullptr,
               qPrintable(QString("%1: the X closed with no animation").arg(a.name)));
      QVERIFY2(dock->isVisible(),
               qPrintable(QString("%1: the dock vanished before the slide").arg(a.name)));
      // …the slide runs toward that edge: width for left/right, height for top/bottom.
      const bool horiz = a.area == Qt::LeftDockWidgetArea || a.area == Qt::RightDockWidgetArea;
      const auto extent = [&] { return horiz ? dock->width() : dock->height(); };
      const int before = extent();
      // Caught while it is STILL on screen: a blink-out leaves nothing to shrink.
      QTRY_VERIFY2_WITH_TIMEOUT(dock->isVisible() && extent() < before,
                                qPrintable(QString("%1: the %2 never shrank from %3")
                                               .arg(a.name, horiz ? "width" : "height")
                                               .arg(before)), 1500);
      QTRY_VERIFY2_WITH_TIMEOUT(!dock->isVisible(),
                                qPrintable(QString("%1: it never finished closing").arg(a.name)), 3000);
      QVERIFY2(!win.actChat_->isChecked(),
               qPrintable(QString("%1: the toolbar toggle stayed lit").arg(a.name)));
      settle();

      // …and it reopens cleanly, transcript intact.
      win.actChat_->setChecked(true);
      QTRY_VERIFY2(dock->isVisible(), qPrintable(QString("%1: it would not reopen").arg(a.name)));
      settle();
      bool kept = false;
      for (QLabel* l : dock->findChildren<QLabel*>())
        if (l->property("chatBody").toString() == QLatin1String("kept across the close")) kept = true;
      QVERIFY2(kept, qPrintable(QString("%1: the close lost the transcript").arg(a.name)));
    }

    // Floating: the X flies the window into the icon.
    dock->setFloating(true);
    win.actChat_->setChecked(true);
    QTRY_VERIFY(dock->isVisible() && dock->isFloating());
    settle();
    const QRect windowBox(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    closeBtn->click();
    stencil::gui::DisintegrateOverlay* from = nullptr;
    QTRY_VERIFY2((from = flight()) != nullptr, "floating: the X closed with no flight");
    QWidget* icon = win.buttonForAction(win.actChat_);
    QVERIFY(icon);
    QVERIFY2(!from->gathering(), "floating: the X must scatter the window INTO the icon");
    QCOMPARE(from->surfaceTarget(), flightPointOf(icon, &win));
    QTRY_VERIFY2(!dock->isVisible(), "floating: it never finished closing");
    settle();
    win.actChat_->setChecked(true);
    QTRY_VERIFY2(dock->isVisible(), "floating: it would not reopen");
    Q_UNUSED(windowBox);
    beat();
  }

  // The REAL input path (no probes): the floating dock's title bar consumes
  // press/move/release itself, so a drag works even where Qt hands the window to the
  // window server (macOS) and never delivers the release.
  void chatDockDragViaMouseEvents() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    settleLayout(&win, 60);

    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    const auto sendMouse = [&](QEvent::Type t, const QPoint& global) {
      QMouseEvent e(t, title->mapFromGlobal(global), QPointF(global),
                    t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                    Qt::NoModifier);
      QApplication::sendEvent(title, &e);
    };
    const QPoint start = title->mapToGlobal(QPoint(30, 8));
    sendMouse(QEvent::MouseButtonPress, start);
    sendMouse(QEvent::MouseMove, start + QPoint(40, 40));   // past the threshold
    QTRY_VERIFY2(win.findChild<QWidget*>("chatDockZones"), "a real drag never showed the zones");
    QWidget* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY2(zones && zones->isVisible(), "zones show for a real (event-driven) drag");

    // Release inside the RIGHT band → docked right, zones gone.
    const QRect zr = zones->geometry();   // bands span the dock region, not `central`
    const QPoint rightBand = win.mapToGlobal(QPoint(zr.right() - 20, zr.center().y()));
    sendMouse(QEvent::MouseMove, rightBand);
    sendMouse(QEvent::MouseButtonRelease, rightBand);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QTRY_VERIFY(!zones->isVisible());
  }

  // The placement button matching the current state is accent-marked and inert.
  void chatDockPlacementState() {
    MainWindow win;
    win.resize(1000, 720);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    auto* chat = win.findChild<QAction*>("actChat");
    QVERIFY(dock && chat);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    const QList<QToolButton*> btns = title->findChildren<QToolButton*>();
    QVERIFY(btns.size() >= 6);   // 4 placements + float + close
    // Docked LEFT by default: exactly one placement button wears the active chip. (It is
    // marked by its stylesheet, not by being disabled — a disabled button would be
    // repainted by QToolButton:disabled as a dead bordered square.)
    const auto activeCount = [&] {
      int n = 0;
      for (QToolButton* b : btns)
        if (b->styleSheet().contains("background:")) ++n;
      return n;
    };
    QTRY_COMPARE(activeCount(), 1);
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QTRY_COMPARE(activeCount(), 1);   // now it's the float button
  }

  // Browser parity (ui/chatDock.js): compact, the dock sits beside its icon but its title bar
  // still DRAGS — and the drag adopts the layout, so what moves is a float the user chose,
  // never a popover still pinned to an icon. Only the bar's double-click toggle stays dead:
  // the browser's header has none.
  void chatCompactPopoverDragsAndAdoptsTheLayout() {
    MainWindow win;
    win.resize(1100, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* dock = win.chatDock_;   // the concrete dock: the drag state is ChatDock's own
    QVERIFY(dock);
    win.openChatCompact(&win);   // any anchor: the popover only needs a rect to sit beside
    QTRY_VERIFY(win.chatCompactShowing());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    settleLayout(&win, 60);

    const QRect before = dock->geometry();
    const auto pressTitle = [&](QEvent::Type type) {
      QMouseEvent ev(type, QPointF(8, 8), QPointF(8, 8), QPointF(title->mapToGlobal(QPoint(8, 8))),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &ev);
    };
    // A press ARMS the drag; only a move past the start distance makes it live, so the
    // gesture has to be driven all the way through to prove anything either way.
    const auto moveTitle = [&](const QPoint& to) {
      QMouseEvent mv(QEvent::MouseMove, QPointF(to), QPointF(to), QPointF(title->mapToGlobal(to)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &mv);
    };
    const auto releaseTitle = [&] {
      QMouseEvent up(QEvent::MouseButtonRelease, QPointF(8, 8), QPointF(8, 8),
                     QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                     Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(title, &up);
    };

    // The double-click toggle first, while the shape is still compact: it moves nothing.
    pressTitle(QEvent::MouseButtonDblClick);
    QTest::qWait(40);
    QCOMPARE(dock->geometry(), before);
    QVERIFY2(win.chatCompactShowing(), "a dblclick on the bar neither floats nor docks it");

    // The drag: it goes live from the compact shape, and starting it adopts the layout.
    pressTitle(QEvent::MouseButtonPress);
    moveTitle(QPoint(240, 180));
    QTest::qWait(40);
    QVERIFY2(dock->dragActive(), "the compact title bar drags, like the browser's header");
    QVERIFY2(!win.chatCompactShowing(),
             "and the drag adopts: what moves is a float the user chose, not a pinned popover");
    releaseTitle();
  }

  void chatDockDragZones() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = win.findChild<QDockWidget*>("llmChatDock");
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);

    settleLayout(&win, 60);   // let the layout reclaim the floated dock's slot
    const QRect central(win.centralWidget()->mapTo(&win, QPoint(0, 0)),
                        win.centralWidget()->size());
    // Offscreen has no movable cursor / synthetic global button state, so the
    // poll reads STUBBED probes — exactly the delivery-free situation of a
    // native macOS drag, where moves/releases never reach the widget.
    QPoint stubCursor = win.mapToGlobal(central.topLeft());  // ≠ the first drag target
    bool stubDown = false;
    win.chatDock_->setDragProbesForTest([&stubCursor] { return stubCursor; },
                                        [&stubDown] { return stubDown; });
    const auto dragTo = [&](const QPoint& globalPos) {
      stubDown = true;
      // Synthesized press on the title bar starts the poll (delivery of the
      // PRESS is all the real flow needs — moves/releases are polled).
      QMouseEvent press(QEvent::MouseButtonPress, QPointF(8, 8), QPointF(8, 8),
                        QPointF(title->mapToGlobal(QPoint(8, 8))), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(title, &press);
      stubCursor = globalPos;  // observed by the poll loop
    };
    const auto releaseAt = [&](const QPoint& globalPos) {
      stubCursor = globalPos;
      QTest::qWait(40);  // a couple of poll ticks at the drop spot
      stubDown = false;  // "button up" → the poll finishes at stubCursor
      QTest::qWait(40);
    };

    // The overlay appears for the whole drag and spans the DOCK REGION: full
    // window width, below the toolbars, above the status bar — NOT the central
    // widget (which shrinks by whatever is docked, drifting the bands inward).
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(win.findChild<QWidget*>("chatDockZones"));
    auto* zones = win.findChild<QWidget*>("chatDockZones");
    QVERIFY(zones);
    QTRY_VERIFY(zones->isVisible());
    const QRect zr = zones->geometry();
    QCOMPARE(zr.left(), 0);
    QCOMPARE(zr.width(), win.width());
    QVERIFY2(zr.top() > 0 && zr.top() <= central.top(), "starts below the toolbars");
    QVERIFY2(zr.bottom() >= central.bottom(), "reaches past the central area's bottom");
    // Native docking is locked out for the whole drag: the zones are the ONLY
    // docking mechanism (Qt can't show its placeholder or hover-dock).
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);

    // LEFT band → docks left; areas restored on release.
    releaseAt(win.mapToGlobal(QPoint(zr.left() + 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // Tear-off-from-DOCKED: the drag starts docked, the poll forces the float
    // past the drag threshold (native docking suppressed throughout), the
    // zones appear, and the RIGHT release band decides.
    QVERIFY(!dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));  // press on the DOCKED title
    QTRY_VERIFY(dock->isFloating());            // forced into the zone flow
    QTRY_VERIFY(zones->isVisible());
    QCOMPARE(dock->allowedAreas(), Qt::NoDockWidgetArea);
    releaseAt(win.mapToGlobal(
        QPoint(zr.right() - 30, zr.center().y())));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);

    // BOTTOM band → docks bottom.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center()));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(
        QPoint(zr.center().x(), zr.bottom() - 30)));
    QTRY_VERIFY(!zones->isVisible());
    QTRY_VERIFY(!dock->isFloating());
    QTRY_COMPARE(win.dockWidgetArea(dock), Qt::BottomDockWidgetArea);

    // Mid-area release → stays floating; the overlay is gone either way.
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    dragTo(win.mapToGlobal(central.center() + QPoint(40, 0)));
    QTRY_VERIFY(zones->isVisible());
    releaseAt(win.mapToGlobal(central.center()));
    QTRY_VERIFY(!zones->isVisible());
    QVERIFY(dock->isFloating());

    // Restore the default placement for later slots.
    win.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->setFloating(false);
    chat->setChecked(false);
    QTRY_VERIFY(!dock->isVisible());
    beat();
  }
};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDock.gui.moc"
