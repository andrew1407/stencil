#include "projectsRowChrome.hpp"

#include "modalChrome.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <utility>

namespace stencil::gui {

  bool isNewWindowMod(Qt::KeyboardModifiers m) {
    return m.testFlag(Qt::ControlModifier) || m.testFlag(Qt::MetaModifier);
  }

  const QCursor& zoomInCursor() {
    static const QCursor cursor = [] {
      const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
      constexpr int EDGE = 22;
      QPixmap pm(qRound(EDGE * dpr), qRound(EDGE * dpr));
      pm.setDevicePixelRatio(dpr);
      pm.fill(Qt::transparent);
      QPainter p(&pm);
      p.setRenderHint(QPainter::Antialiasing);
      const QPointF lens(9, 9);
      const double r = 5.5;
      const auto pass = [&](const QColor& c, double extra) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, 2.0 + extra, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(lens, r, r);
        p.drawLine(lens + QPointF(r, r) * 0.72, QPointF(19.0, 19.0));
        p.setPen(QPen(c, 1.4 + extra, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(lens - QPointF(2.6, 0), lens + QPointF(2.6, 0));
        p.drawLine(lens - QPointF(0, 2.6), lens + QPointF(0, 2.6));
      };
      pass(Qt::white, 2.2);
      pass(QColor(40, 40, 40), 0.0);
      return QCursor(pm, qRound(lens.x()), qRound(lens.y()));
    }();
    return cursor;
  }

  QString expiryText(const core::ProjectsStore& store,
                     const core::ProjectMeta& meta, long long now) {
    if (store.isExpired(meta, now)) return "EXPIRED";
    const auto at = store.expiresAt(meta);
    if (!at.has_value()) return QString();
    const long long day = 24LL * 60 * 60 * 1000;
    long long days = (*at - now + day - 1) / day;  // ceil
    if (days < 0) days = 0;
    return days <= 1 ? QString("expires in 1 day")
                     : QString("expires in %1 days").arg(days);
  }

  QString createdText(long long createdAt) {
    if (createdAt <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(createdAt).date();
    return QString("Created %1").arg(QLocale().toString(d, QLocale::ShortFormat));
  }

  QString expiresText(long long expiresAt) {
    if (expiresAt <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(expiresAt).date();
    return QString("Expires %1").arg(QLocale().toString(d, QLocale::ShortFormat));
  }

  std::shared_ptr<core::ProjectsStore> loadedNameStore(const std::vector<Project>& projects) {
    auto store = std::make_shared<core::ProjectsStore>();
    std::vector<core::ProjectMeta> metas;
    for (const auto& p : projects) metas.push_back(p.meta);
    store->load(metas);
    return store;
  }

  std::function<void()> makeNameValidator(std::shared_ptr<core::ProjectsStore> store,
                                          QLineEdit* edit, QAbstractButton* okBtn,
                                          const QString& exceptId, const QString& current) {
    return [store = std::move(store), edit, okBtn, exceptId, current] {
      const QString name = edit->text().trimmed();
      const bool unchanged = !current.isNull() && name == current;
      const auto res = store->validateName(name.toStdString(), exceptId.toStdString());
      const bool ok = res.ok && !unchanged;
      okBtn->setEnabled(ok);
      okBtn->setCursor(ok ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
      // Live: what the key is. Rejected: why. (The tooltip's trailing "(…)" is what the
      // rich tip turns into a keycap — tipContent's key vocabulary.)
      okBtn->setToolTip(ok ? QObject::tr("Save name (Enter)")
                           : (unchanged ? QObject::tr("No change")
                                        : QString::fromStdString(res.reason)));
    };
  }

  std::optional<QString> promptValidatedName(QWidget* parent, const QString& title,
                                             const QString& initial,
                                             const QString& exceptId,
                                             const std::vector<Project>& projects) {
    PromptSpec spec;
    spec.title = title;
    spec.titleIcon = QStringLiteral("plus-circle");   // a name to collect, not a warning
    spec.message = QObject::tr("Project name:");
    spec.defaultValue = initial;
    spec.validate = [store = loadedNameStore(projects), exceptId](const QString& name) {
      const auto res = store->validateName(name.toStdString(), exceptId.toStdString());
      return res.ok ? QString() : QString::fromStdString(res.reason);
    };
    return promptModal(parent, spec);
  }

  QString pickServer(QWidget* parent, const QStringList& urls, const QString& message,
                     const QString& title, const QString& confirmLabel,
                     const QString& confirmIcon) {
    if (urls.isEmpty()) return QString();
    if (urls.size() == 1) return urls.first();
    ChooseSpec spec;
    spec.title = title;
    spec.message = message;
    spec.confirmLabel = confirmLabel;
    spec.confirmIcon = confirmIcon;
    for (const QString& u : urls) spec.options.push_back({u, u});
    return chooseModal(parent, spec).value_or(QString());
  }

  QRect kebabZone(const QRect& rowRect) {
    const int w = 50;
    return QRect(rowRect.right() - w, rowRect.top(), w, rowRect.height());
  }

  QRect kebabChip(const QRect& rowRect) {
    const QRect zone = kebabZone(rowRect);
    const QSize chip(38, 30);
    return QRect(zone.center().x() - chip.width() / 2, zone.center().y() - chip.height() / 2,
                 chip.width(), chip.height());
  }

  QPixmap squareThumb(const QPixmap& src, int size) {
    if (src.isNull()) return src;
    const QPixmap scaled =
        src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width() - size) / 2;
    const int y = (scaled.height() - size) / 2;
    return scaled.copy(x, y, size, size);
  }

}  // namespace stencil::gui
