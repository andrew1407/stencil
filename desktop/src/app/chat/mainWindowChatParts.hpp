#pragma once
// The chat glue's image/edge-map encoders, tip rows and toast metrics (llm-contract §7), private to the MainWindowChat*.cpp TUs.
#include "mainWindowHelpers.hpp"
#include "../../llm/client/LlmClient.hpp"
#include "../../support/theme/theme.hpp"

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

  inline constexpr int CHAT_IMAGE_MAX_EDGE = 1568; // long-edge downscale bound (§7)

  // ≤1568 px on the long edge, PNG base64 (contract §7).
  inline llm::ChatImage encodeChatImage(const QImage& img) {
    QImage scaled = img;
    if (std::max(img.width(), img.height()) > CHAT_IMAGE_MAX_EDGE)
      scaled = img.scaled(CHAT_IMAGE_MAX_EDGE, CHAT_IMAGE_MAX_EDGE, Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
    llm::ChatImage out;
    out.mediaType = QStringLiteral("image/png");
    out.data = pngBytes(scaled).toBase64();
    return out;
  }

  // §7 edge map: the core contour filter (the `filter` op's Sobel pass). RGBA8888 is the byte order applyContourRGBA expects.
  llm::ChatImage encodeEdgeMap(const QImage& img);

  // Appended only when the edge map is attached (llm-contract.md §7, verbatim).
  constexpr char EDGE_MAP_SUFFIX[] =
      "The second attached image is an edge-map render of the working image at the "
      "same pixel coordinates: use it to place outline points on real edges.";

  // §7 auto-continuation user-text: a message to the MODEL. It rides in chatHistory, so every display path filters it out.
  constexpr char CHAT_CONTINUATION_NOTE[] =
      "[The working image is now the picture those actions loaded — continue with it.]";

  // Dims included so equal byte runs with different geometry can't collide.
  inline QByteArray imageDigest(const QImage& img) {
    QCryptographicHash h(QCryptographicHash::Sha1);
    const qint64 dims[2] = {img.width(), img.height()};
    h.addData(QByteArrayView(reinterpret_cast<const char*>(dims), sizeof(dims)));
    h.addData(QByteArrayView(reinterpret_cast<const char*>(img.constBits()),
                             static_cast<qsizetype>(img.sizeInBytes())));
    return h.result();
  }

  // §7 image replay: before the current turn only the most recent image-bearing message keeps its first image; never an edge map.
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

  inline constexpr int TOAST_MAX_CHARS = 90;  // shared truncation bound
  inline constexpr int TOAST_MS = 6000;      // auto-hide
  inline constexpr int TOAST_MARGIN = 18;    // bottom-left anchor inset

  // Completion toast while the chat dock is hidden (browser chatPanel closedToast parity); auto-hides after 6 s.
  class ChatToast : public QWidget {
   public:
    explicit ChatToast(QWidget* parent) : QWidget(parent) {
      setObjectName(QStringLiteral("chatToast"));
      setAttribute(Qt::WA_StyledBackground);
      setCursor(Qt::PointingHandCursor);
      auto* lay = new QHBoxLayout(this);
      lay->setContentsMargins(14, 10, 14, 10);
      label = new QLabel(this);
      lay->addWidget(label);
      timer = new QTimer(this);
      timer->setSingleShot(true);
      QObject::connect(timer, &QTimer::timeout, this, &QWidget::hide);
      parent->installEventFilter(this);  // keep the bottom-left anchor on resize
      hide();
    }

    void showToast(const QString& text, bool success, std::function<void()> onClick) {
      this->onClick = std::move(onClick);
      setStyleSheet(
          QStringLiteral(
              "#chatToast{background:rgba(40,46,60,242);border:1px solid rgba(255,255,255,42);"
              "border-left:3px solid %1;border-radius:8px;}"
              "#chatToast QLabel{color:white;background:transparent;}")
              .arg(QLatin1String(success ? CHAT_STATUS_OK_COLOR : CHAT_STATUS_BAD_COLOR)));
      label->setText(text);
      reposition();
      show();
      raise();
      timer->start(TOAST_MS);
    }

   protected:
    void mousePressEvent(QMouseEvent*) override {
      timer->stop();
      hide();
      if (onClick) onClick();
    }
    bool eventFilter(QObject* o, QEvent* e) override {
      if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) reposition();
      return QWidget::eventFilter(o, e);
    }

   private:
    void reposition() {
      adjustSize();
      if (QWidget* p = parentWidget())
        move(TOAST_MARGIN, p->height() - height() - TOAST_MARGIN);
    }

    QLabel* label = nullptr;
    QTimer* timer = nullptr;
    std::function<void()> onClick;
  };

  // Retry re-sends the most recent USER message (pushed before the request went out).
  inline QString lastUserTurn(const QVector<llm::ChatMessage>& history) {
    for (auto it = history.crbegin(); it != history.crend(); ++it)
      if (it->role == QLatin1String("user")) return it->text;
    return QString();
  }

  // Browser .chat-status-tip state colours, readable on both QToolTip backgrounds.
  inline constexpr const char* TIP_OK_COLOR = "#28a745";
  inline constexpr const char* TIP_ERROR_COLOR = "#dc3545";
  inline constexpr const char* TIP_CONNECTING_COLOR = "#e0a800";

  // Browser PROBE_TTL_MS (llm/chatSession.js).
  inline constexpr qint64 LLM_PROBE_TTL_MS = 15000;

  inline QString tipColored(const char* color, const QString& text) {
    return QStringLiteral("<font color=\"%1\">%2</font>")
        .arg(QLatin1String(color), text.toHtmlEscaped());
  }

  // One .chat-status-tip table row (browser css/components.css); the value cell is pre-rendered by the caller.
  inline QString tipRow(const QString& label, const QString& valueHtml) {
    return QStringLiteral("<tr><td style=\"color:%1;\">%2&nbsp;&nbsp;&nbsp;</td><td>%3</td></tr>")
        .arg(currentPalette().textMuted.name(), label.toHtmlEscaped(), valueHtml);
  }
  inline QString tipValue(const QString& text) {
    return QStringLiteral("<span style=\"color:%1;\">%2</span>")
        .arg(currentPalette().textKey.name(), text.toHtmlEscaped());
  }

  // The browser .chat-status-tip / .chat-status-tip-foot rendering.
  inline QString tipPanel(const QString& rows, const QStringList& foot) {
    const Palette pal = currentPalette();
    QString html =
        QStringLiteral("<table cellspacing=\"0\" cellpadding=\"1\">%1</table>").arg(rows);
    // style, not the color attribute: Qt ignores the attribute and draws the rule in the TEXT colour.
    html += QStringLiteral("<hr style=\"background-color:%1;\">").arg(pal.borderMain.name());
    for (const QString& line : foot)
      html += QStringLiteral("<div style=\"color:%1; font-size:small;\">%2</div>")
                  .arg(pal.textMuted.name(), line.toHtmlEscaped());
    return html;
  }

}  // namespace stencil::gui
