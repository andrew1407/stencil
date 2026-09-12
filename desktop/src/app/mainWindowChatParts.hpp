#pragma once
// The chat glue's image/edge-map encoders, tip rows and toast metrics (llm-contract §7), private to the mainWindowChat*.cpp TUs.
#include "mainWindowHelpers.hpp"
#include "../llm/llmClient.hpp"
#include "../support/theme.hpp"

#include <QLabel>
#include <QGridLayout>
#include <QByteArray>
#include <QCryptographicHash>
#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

namespace stencil::gui {

  inline constexpr int kChatImageMaxEdge = 1568; // long-edge downscale bound (§7)

  // Downscale to ≤1568 px on the long edge and re-encode as PNG base64
  // (contract §7) — QImage::scaled keeps the aspect.
  inline llm::ChatImage encodeChatImage(const QImage& img) {
    QImage scaled = img;
    if (std::max(img.width(), img.height()) > kChatImageMaxEdge)
      scaled = img.scaled(kChatImageMaxEdge, kChatImageMaxEdge, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
    llm::ChatImage out;
    out.mediaType = QStringLiteral("image/png");
    out.data = pngBytes(scaled).toBase64();
    return out;
  }

  // §7 edge map: the downscaled snapshot with the core contour filter applied
  // (the same Sobel pass the `filter` op's "contour" mode uses), encoded like
  // any attachment. RGBA8888 is the byte order applyContourRGBA expects.
  llm::ChatImage encodeEdgeMap(const QImage& img);

  // Appended to the system suffix when — and only when — the edge map is
  // actually attached (llm-contract.md §7, verbatim).
  constexpr char kEdgeMapSuffix[] =
      "The second attached image is an edge-map render of the working image at the "
      "same pixel coordinates: use it to place outline points on real edges.";

  // §7 auto-continuation user-text: a message to the MODEL, never to a user.
  // It rides in chatHistory_ (and the §12 doc built from it), so every
  // display path has to filter it out — no surface may ever show it.
  constexpr char kChatContinuationNote[] =
      "[The working image is now the picture those actions loaded — continue with it.]";

  // Content digest keying the encoded working-image cache (dims included so
  // equal byte runs with different geometry can't collide).
  inline QByteArray imageDigest(const QImage& img) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    const qint64 dims[2] = {img.width(), img.height()};
    h.addData(QByteArrayView(reinterpret_cast<const char*>(dims), sizeof(dims)));
    h.addData(QByteArrayView(reinterpret_cast<const char*>(img.constBits()),
                             static_cast<qsizetype>(img.sizeInBytes())));
    return h.result();
  }

  // The §7 image replay rule for the messages BEFORE the current turn: among
  // msgs[0..endExclusive) only the single most recent image-bearing message
  // keeps (just) its first image — the working snapshot when one rode along,
  // and never an edge map (those stay out of history entirely); every older
  // one is replayed text-only.
  inline void trimPriorImages(QVector<llm::ChatMessage>& msgs, qsizetype endExclusive) {
    bool newestPrior = true;
    for (qsizetype i = endExclusive - 1; i >= 0; --i) {
      auto& images = msgs[i].images;
      if (images.isEmpty()) continue;
      if (newestPrior) {
        if (images.size() > 1) {
          const llm::ChatImage keep = images.first();
          images = {keep};
        }
        newestPrior = false;
      } else {
        images.clear();
      }
    }
  }

  // completion toast (browser chatPanel closedToast parity)
  inline constexpr int kToastMaxChars = 90;  // shared truncation bound
  inline constexpr int kToastMs = 6000;      // auto-hide
  inline constexpr int kToastMargin = 18;    // bottom-left anchor inset

  // Bottom-left toast shown when an assistant turn finishes while the chat
  // dock is hidden: dark card, white text, success/danger accent edge.
  // Click = dismiss + run onClick (opens the chat); auto-hides after 6 s;
  // re-anchored on parent resizes via an event filter.
  class ChatToast : public QWidget {
   public:
    explicit ChatToast(QWidget* parent) : QWidget(parent) {
      setObjectName(QStringLiteral("chatToast"));
      setAttribute(Qt::WA_StyledBackground);
      setCursor(Qt::PointingHandCursor);
      auto* lay = new QHBoxLayout(this);
      lay->setContentsMargins(14, 10, 14, 10);
      label_ = new QLabel(this);
      lay->addWidget(label_);
      timer_ = new QTimer(this);
      timer_->setSingleShot(true);
      QObject::connect(timer_, &QTimer::timeout, this, &QWidget::hide);
      parent->installEventFilter(this);  // keep the bottom-left anchor on resize
      hide();
    }

    void showToast(const QString& text, bool success, std::function<void()> onClick) {
      onClick_ = std::move(onClick);
      setStyleSheet(
          QStringLiteral(
              "#chatToast{background:rgba(40,46,60,242);border:1px solid rgba(255,255,255,42);"
              "border-left:3px solid %1;border-radius:8px;}"
              "#chatToast QLabel{color:white;background:transparent;}")
              .arg(QLatin1String(success ? kChatStatusOkColor : kChatStatusBadColor)));
      label_->setText(text);
      reposition();
      show();
      raise();
      timer_->start(kToastMs);
    }

   protected:
    void mousePressEvent(QMouseEvent*) override {
      timer_->stop();
      hide();
      if (onClick_) onClick_();
    }
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) reposition();
      return QWidget::eventFilter(o, e);
    }

   private:
    void reposition() {
      adjustSize();
      if (QWidget* p = parentWidget())
        move(kToastMargin, p->height() - height() - kToastMargin);
    }

    QLabel* label_ = nullptr;
    QTimer* timer_ = nullptr;
    std::function<void()> onClick_;
  };

  // Every error card offers a Retry of the turn that failed: the most recent
  // USER message (pushed to the history before the request went out).
  inline QString lastUserTurn(const QVector<llm::ChatMessage>& history) {
    for (auto it = history.crbegin(); it != history.crend(); ++it)
      if (it->role == QLatin1String("user")) return it->text;
    return QString();
  }

  // Status-cell colours of the gear tooltip table — the browser
  // .chat-status-tip states (ok | error | connecting), readable on both the
  // light and dark QToolTip backgrounds.
  inline constexpr const char* kTipOkColor = "#28a745";
  inline constexpr const char* kTipErrorColor = "#dc3545";
  inline constexpr const char* kTipConnectingColor = "#e0a800";

  // Probe-cache TTL — the browser PROBE_TTL_MS (llm/chatSession.js).
  inline constexpr qint64 kLlmProbeTtlMs = 15000;

  inline QString tipColored(const char* color, const QString& text) {
    return QStringLiteral("<font color=\"%1\">%2</font>")
        .arg(QLatin1String(color), text.toHtmlEscaped());
  }

  // One .chat-status-tip table row (browser css/components.css): the label
  // cell muted, the value cell pre-rendered by the caller — tipValue()'s
  // accent shade, or the status cell's state colour.
  inline QString tipRow(const QString& label, const QString& valueHtml) {
    return QStringLiteral("<tr><td style=\"color:%1;\">%2&nbsp;&nbsp;&nbsp;</td><td>%3</td></tr>")
        .arg(currentPalette().textMuted.name(), label.toHtmlEscaped(), valueHtml);
  }
  inline QString tipValue(const QString& text) {
    return QStringLiteral("<span style=\"color:%1;\">%2</span>")
        .arg(currentPalette().textKey.name(), text.toHtmlEscaped());
  }

  // The whole tooltip: the rows table over the footer lines, the foot under a
  // hairline in smaller muted type — the browser .chat-status-tip /
  // .chat-status-tip-foot rendering (no heading; the table opens the tip).
  inline QString tipPanel(const QString& rows, const QStringList& foot) {
    const Palette pal = currentPalette();
    QString html =
        QStringLiteral("<table cellspacing=\"0\" cellpadding=\"1\">%1</table>").arg(rows);
    // style, not the color attribute: Qt ignores the attribute and otherwise
    // draws the rule in the TEXT colour — glaring on the dark theme (the
    // browser hairline is border-main). Verified: background-color is honoured.
    html += QStringLiteral("<hr style=\"background-color:%1;\">").arg(pal.borderMain.name());
    for (const QString& line : foot)
      html += QStringLiteral("<div style=\"color:%1; font-size:small;\">%2</div>")
                  .arg(pal.textMuted.name(), line.toHtmlEscaped());
    return html;
  }

}  // namespace stencil::gui
