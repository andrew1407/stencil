// MainWindow GUI e2e — The clear button in the composer, and the drag that attaches to it.
// Shared ground (helpers, the loaded window, the motion pins) is in MainWindow.gui.hpp.
#include "../../MainWindow.gui.hpp"

class MainWindowGuiTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { prepareGuiTestCase(); }

  // The clear button lives in the COMPOSER row (browser #chat-clear parity), and a drag onto that
  // row attaches — the cue showing while it hovers, what was attached shown in the user's bubble.
  void chatClearButtonAndAttachmentCue() {
    MainWindow win(nullptr, false);
    win.resize(1200, 800);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    auto* chat = win.findChild<QAction*>("actChat");
    auto* dock = qobject_cast<stencil::gui::ChatDock*>(
        win.findChild<QDockWidget*>("llmChatDock"));
    QVERIFY(chat && dock);
    chat->setChecked(true);
    QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY(dock->width() > 200);  // let the open slide settle before geometry

    // The button lives in the COMPOSER row (browser #chat-clear parity), after
    // the settings gear — NOT in the title bar with the placement chevrons.
    QWidget* title = dock->titleBarWidget();
    QVERIFY(title);
    QVERIFY(!title->findChild<QToolButton*>("chatClear"));
    auto* clearBtn = dock->findChild<QToolButton*>("chatClear");
    QVERIFY(clearBtn);
    QVERIFY(clearBtn->toolTip().isEmpty());  // no tooltip — the menu label says it (user decision)
    // Both live in the … menu now — hidden buttons have no meaningful geometry,
    // so their identity is what matters, not their x order.
    auto* gearBtn = dock->findChild<QToolButton*>("chatGear");
    QVERIFY(gearBtn);
    QVERIFY(gearBtn->isHidden() && clearBtn->isHidden());

    // A conversation: two cards, an attachment, and the model-side state a
    // turn leaves behind.
    auto* scrollArea = dock->findChild<QScrollArea*>();
    QVERIFY(scrollArea && scrollArea->widget());
    QWidget* transcript = scrollArea->widget();
    const auto cardCount = [transcript] {
      // Cards are the only direct QFrame children of the transcript column
      // (the suggestion block is a plain QWidget).
      return transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly)
          .size();
    };
    dock->appendUser("make it sepia");
    dock->appendAssistant("done");
    QCOMPARE(cardCount(), 2);
    QImage att(8, 8, QImage::Format_RGB32);
    att.fill(Qt::blue);
    dock->addAttachmentImage(att);
    QCOMPARE(dock->attachedImages().size(), 1);


    // A drag onto the COMPOSER attaches; the cue shows while it hovers and goes on drop. Driven
    // through the composer's own event filter, where the whole gesture lives.
    {
      auto* inputArea = dock->findChild<QWidget*>("chatInputArea");
      auto* cue = dock->findChild<QWidget*>("chatDropCue");
      auto* inputBox = dock->findChild<QPlainTextEdit*>("chatInput");
      QVERIFY(inputArea && cue && inputBox);
      // Aim at the INPUT BOX: the cue (and the attach) belong to it alone — a drag
      // over the composer's buttons or the chip tray neither lights nor attaches.
      const QPoint at = inputBox->mapTo(inputArea, inputBox->rect().center());
      QImage dragged(12, 12, QImage::Format_RGB32);
      dragged.fill(Qt::red);
      QMimeData mime;
      mime.setImageData(dragged);
      const int before = dock->attachedImages().size();
      QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &enter));
      QVERIFY2(enter.isAccepted(), "the composer refused an image drag");
      QVERIFY2(!cue->isHidden(), "no drop cue while a drag hovers the composer");
      if (qEnvironmentVariableIsSet("STENCIL_GUI_SHOTS")) {
        QTest::qWait(50);
        inputArea->grab().save(QString::fromLocal8Bit(qgetenv("STENCIL_GUI_SHOTS")) + "/dock-dropcue.png");
      }
      // Off the box (the buttons' corner) the cue goes out mid-drag…
      const QPoint offBox(inputArea->width() - 3, inputArea->height() - 3);
      QDragMoveEvent wander(offBox, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &wander));
      QVERIFY2(cue->isHidden(), "the cue lit over the composer's buttons");
      // …and back over it the cue returns; the drop there attaches.
      QDragMoveEvent back(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &back));
      QVERIFY2(!cue->isHidden(), "the cue did not come back over the input box");
      QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QVERIFY(qApp->sendEvent(inputArea, &drop));
      QVERIFY2(drop.isAccepted(), "the composer refused an image drop");
      QCOMPARE(dock->attachedImages().size(), before + 1);
      QVERIFY2(cue->isHidden(), "the cue outlived the drop");
      // …and the queued chip's thumbnail carries a hover preview (28px tells you
      // nothing about which screenshot it is).
      auto* tray = dock->findChild<QWidget*>("chatAttachTray");
      QVERIFY(tray);
      bool previewed = false;
      for (QLabel* pic : tray->findChildren<QLabel*>())
        if (!pic->pixmap().isNull() && !pic->findChildren<QObject*>().isEmpty()) previewed = true;
      QVERIFY2(previewed, "no hover preview installed on the attachment chip");
      // A NAMED attachment says its name; only an unnamed one falls back to the dimensions. The
      // thumbnail carries no tooltip — it opens the hover preview, and the two would overlap.
      dock->clearAttachments();
      dock->addAttachmentImage(dragged, QStringLiteral("cat.png"));
      QStringList chipTexts;
      bool thumbHasTooltip = false;
      for (QLabel* l : tray->findChildren<QLabel*>()) {
        if (l->pixmap().isNull()) chipTexts << l->text();
        else if (!l->toolTip().isEmpty()) thumbHasTooltip = true;
      }
      QVERIFY2(chipTexts.contains("cat.png"), qPrintable("chip shows: " + chipTexts.join('|')));
      QVERIFY2(!thumbHasTooltip, "the thumbnail must not duplicate the hover preview in a tooltip");
      // Put the queue back the way the surrounding case staged it (one image).
      dock->clearAttachments();
      dock->addAttachmentImage(att);
    }

    // What the user attached is SHOWN, inside the user's own bubble: the card carries a pixmap
    // label. The browser and extension render the same strip.
    {
      const auto before =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      dock->appendUser("and this one", dock->attachedImages());
      const auto after =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(after.size(), before.size() + 1);
      QFrame* userCard = after.last();
      QCOMPARE(userCard->objectName(), QStringLiteral("chatCardUser"));
      int thumbs = 0;
      QString body;
      for (QLabel* l : userCard->findChildren<QLabel*>()) {
        if (!l->pixmap().isNull()) thumbs++;
        else body += l->text();
      }
      QCOMPARE(thumbs, 1);
      QVERIFY(body.contains("and this one"));
      QVERIFY(!body.contains("image(s) attached"));   // the count-only text is gone
      // A turn with nothing attached stays a plain bubble.
      dock->appendUser("no images here");
      QFrame* plain =
          transcript->findChildren<QFrame*>(QString(), Qt::FindDirectChildrenOnly).last();
      int plainThumbs = 0;
      for (QLabel* l : plain->findChildren<QLabel*>())
        if (!l->pixmap().isNull()) plainThumbs++;
      QCOMPARE(plainThumbs, 0);
    }
    stencil::llm::ChatMessage m;
    m.role = "user";
    m.text = "make it sepia";
    win.pushChatHistory(m);
    win.chatVideoPath = "/tmp/clip.mp4";
    win.chatVideoFrames = 42;
    win.chatImageDigest = QByteArray("digest");
    win.chatImageEncoded.data = QByteArray("cached");
    QVERIFY(!win.chatHistory.isEmpty());
    auto* suggest = dock->findChild<QWidget*>("chatSuggest");
    QVERIFY(suggest && !suggest->isVisible());  // hidden by the first card

  }

};

QTEST_MAIN(MainWindowGuiTest)
#include "MainWindow.chatTurnsClear.gui.moc"
