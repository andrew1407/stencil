// MainWindow GUI e2e — The dock's toggles: every route that shows, hides and re-places the assistant.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The AI-Assistant chat dock: the checkable toolbar/View action opens and closes it; it docks
  // on all four sides, floats, defaults LEFT, splits transcript from input, takes pasted images.
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
    // The COMPOSER acts on a drop; the DOCK swallows the ones that miss it, so a gesture aimed at
    // the chat never reaches the window (browser chatPanel.js parity). Its editor declines drops.
    QVERIFY(dock->acceptDrops());
    auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
    QVERIFY(inputArea);
    QVERIFY(inputArea->acceptDrops());  // image/video drops become chat attachments
    auto* chatInput = dock->findChild<QPlainTextEdit*>("chatInput");
    QVERIFY(chatInput && !chatInput->acceptDrops() && !chatInput->viewport()->acceptDrops());
    // …cued by an animated icon over the composer, hidden until a drag arrives.
    auto* cue = dock->findChild<QWidget*>("chatDropCue");
    QVERIFY(cue && cue->isHidden());

    // The composer's resize grip is the SHARED pill (PillSplitter.hpp), not a stylesheet handle:
    // that one could only be narrowed symmetrically, so it stretched into a fat accent band.
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

    // Right-side docking must work even though the fixed-width selection panel owns that area —
    // nesting provides the drop slots.
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
    // The status dot is a BADGE on the … TRIGGER (browser .conn-status parity): a child of the
    // button with no layout slot. The gear lives in the menu now, so the dot rides what stays.
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

    // Card container: the dock content renders on the controls-panel colour, DISTINCT from the
    // canvas backdrop behind it, with the themed 1px border on the whole card.
    {
      const QImage bodyImg = dock->widget()->grab().toImage();
      const QColor cardBg = bodyImg.pixelColor(bodyImg.width() / 2, 4);
      const QImage centralImg = win.centralWidget()->grab().toImage();
      // Near the BOTTOM, not the exact vertical centre: the top bars grow with theme or content, so
      // a centre sample can drift onto one; the bottom stays inside the canvas area.
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

    // Cohesive-column chrome (browser parity): an in-content header title and a BORDERLESS
    // transcript — the card look is the dock's own background and border, not an inner frame.
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

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatDock.gui.moc"
