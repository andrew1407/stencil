#pragma once
// Chat-side helpers for the MainWindow GUI suites: the injected LLM transports, the
// transcript readers, and the two waits every chat case opens with.
#include "mainWindow.hpp"
#include "chatDock.hpp"
#include "../src/llm/llmClient.hpp"
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QtTest>

namespace stencil::guitest {
  using stencil::gui::ChatDock;

  // Injected LLM transport (the seam llmClient.headless.cpp uses): captures the
  // POST and answers synchronously with a canned response, so a chat send in
  // the GUI test never touches the network.
  struct MockChatTransport : stencil::llm::LlmTransport {
    QJsonObject body;             // last POSTed payload
    QList<QJsonObject> allBodies; // every POSTed payload, in order
    QByteArray response;          // canned provider reply
    QList<QByteArray> queue;      // per-POST replies, popped in order (else `response`)
    int status = 200;             // 0 + netError simulates a network failure
    QString netError;
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      body = QJsonDocument::fromJson(b).object();
      allBodies.append(body);
      cb(status, queue.isEmpty() ? response : queue.takeFirst(), netError);
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
  };

  // Asynchronous LLM transport: every POST is PARKED with its callback so the test
  // decides when answers land. MockChatTransport answers inline, which makes every
  // fan-out look like one request at a time — the only way to see how many requests
  // are really in flight at once is to hold them.
  struct DeferredChatTransport : stencil::llm::LlmTransport {
    struct Call {
      QJsonObject body;
      std::function<void(int, QByteArray, QString)> cb;
    };
    QList<Call> parked;
    int started = 0;        // POSTs ever made
    int peakInFlight = 0;   // the most ever outstanding at once
    QByteArray response;    // canned reply body
    int status = 200;
    QString netError;
    void postJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                  const QByteArray& b,
                  std::function<void(int, QByteArray, QString)> cb) override {
      parked.append(Call{QJsonDocument::fromJson(b).object(), std::move(cb)});
      ++started;
      peakInFlight = std::max(peakInFlight, static_cast<int>(parked.size()));
    }
    void getJson(const QUrl&, const QList<QPair<QByteArray, QByteArray>>&,
                 std::function<void(int, QByteArray, QString)> cb) override {
      cb(200, QByteArrayLiteral("{}"), QString());
    }
    void answerNext() {   // settle the oldest outstanding call
      if (parked.isEmpty()) return;
      const Call c = parked.takeFirst();
      c.cb(status, response, netError);
    }
  };

  // The first inline <img src="data:image/png;base64,…"> in a rich-text label, decoded.
  inline QImage pngOf(const QString& html) {
    const QString mark = QStringLiteral("base64,");
    const int at = html.indexOf(mark);
    if (at < 0) return QImage();
    const int end = html.indexOf(QLatin1Char('"'), at);
    if (end < 0) return QImage();
    return QImage::fromData(
        QByteArray::fromBase64(html.mid(at + mark.size(), end - at - mark.size()).toLatin1()),
        "PNG");
  }

  // Did anything actually draw? (a glyph rasterised into a fully transparent PNG
  // would satisfy every size assertion and show nothing).
  inline bool hasInk(const QImage& img) {
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x)
        if (qAlpha(img.pixel(x, y)) > 8) return true;
    return false;
  }

  // Every note/label text in the chat dock, joined — the notes a turn leaves.
  inline QString dockText(const QWidget* dock) {
    QString all;
    for (const QLabel* l : dock->findChildren<QLabel*>()) all += l->text() + QLatin1Char('\n');
    return all;
  }

  // Does any card in the chat transcript carry this text? Cards keep their
  // rendered text on the body label's "chatBody" property (notes riding inside
  // an assistant bubble use "chatNote").
  inline bool chatTranscriptHas(const stencil::gui::ChatDock* dock, const QString& needle) {
    for (const QLabel* l : dock->findChildren<QLabel*>())
      if (l->property("chatBody").toString().contains(needle) ||
          l->property("chatNote").toString().contains(needle))
        return true;
    return false;
  }

  // The texts of the assistant reply bubbles in the transcript, in order. The
  // pending "…" card (body "…") and cards already handed to deleteLater are
  // excluded by flushing deferred deletes first.
  inline QStringList assistantBubbleTexts(const stencil::gui::ChatDock* dock) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QStringList out;
    for (const QFrame* f : dock->findChildren<QFrame*>()) {
      if (f->objectName() != QLatin1String("chatCardAssistant")) continue;
      for (const QLabel* l : f->findChildren<QLabel*>()) {
        const QString body = l->property("chatBody").toString();
        if (!body.isEmpty() && body != QStringLiteral("…")) out << body;
      }
    }
    return out;
  }

  // The chat dock slides open, so isVisible() is true while it is still a zero-width
  // sliver, and anything appended into that lays out at the wrong width. Wait for the
  // transcript's real width, and hand back the scroll area every chat test drives.
  inline QScrollArea* openTranscript(stencil::gui::MainWindow& win) {
    QScrollArea* scroll = nullptr;
    ChatDock* dock = win.findChild<ChatDock*>();
    [&] {
      QVERIFY(dock);
      QTRY_VERIFY(dock->isVisible());
      scroll = dock->findChild<QScrollArea*>();
      QVERIFY(scroll);
      QTRY_VERIFY(scroll->viewport()->width() > 100);
    }();
    return scroll;
  }

  // Pad the transcript until it really scrolls past `minMax`. A fixed bubble count is a
  // guess about the dock's height, which is whatever the window allows it to be.
  inline QScrollBar* fillUntilScrollable(stencil::gui::MainWindow& win, QScrollArea* scroll, int minMax = 0) {
    QScrollBar* bar = scroll->verticalScrollBar();
    ChatDock* dock = win.findChild<ChatDock*>();
    for (int i = 0; i < 60 && bar->maximum() <= minMax; ++i) {
      dock->appendAssistant(
          QStringLiteral("Filler bubble %1, long enough to take real height in the column.").arg(i));
      QTest::qWait(20);
    }
    return bar;
  }
}  // namespace stencil::guitest

