// The browser app's own markup, read straight from its source, so the desktop can pin
// the hover copy the two front-ends share (mainWindow.tooltips.gui.cpp toolbarTooltipsMatchThe-
// Browser). Copy that lives in the shared canon (config/uiStrings.json) reaches the
// markup as a `${UI_STRINGS.a.b}` template, so resolving those is part of reading it.
#pragma once

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>

namespace stencil::test {

  // The repo root, from this header's own path (desktop/tests/support/…).
  inline QString repoRoot() { return QStringLiteral(__FILE__).section('/', 0, -5); }

  struct BrowserMarkup {
    QString js;
    QJsonObject strings;

    // jsPath is repo-relative, e.g. "browser/js/ui/toolbar.js".
    bool load(const QString& jsPath) {
      QFile f(repoRoot() + '/' + jsPath);
      if (f.open(QIODevice::ReadOnly)) js = QString::fromUtf8(f.readAll());
      QFile s(repoRoot() + "/browser/js/config/uiStrings.json");
      if (s.open(QIODevice::ReadOnly))
        strings = QJsonDocument::fromJson(s.readAll()).object();
      return !js.isEmpty() && !strings.isEmpty();
    }

    // The markup for one control id.
    QString tag(const QString& id) const {
      return QRegularExpression("<[a-zA-Z]+[^>]*\\bid=\"" + id + "\"[^>]*>").match(js).captured(0);
    }

    // One attribute of it: canon templates resolved, HTML entities back to characters.
    QString attr(const QString& tag, const QString& name) const {
      QString v = QRegularExpression(name + "=\"([^\"]*)\"").match(tag).captured(1);
      static const QRegularExpression ref("\\$\\{UI_STRINGS\\.([A-Za-z0-9_.]+)\\}");
      for (QRegularExpressionMatch m = ref.match(v); m.hasMatch(); m = ref.match(v)) {
        QJsonValue node = strings;
        for (const QString& key : m.captured(1).split('.')) node = node.toObject().value(key);
        v.replace(m.capturedStart(), m.capturedLength(), node.toString());
      }
      return v.replace("&amp;", "&").replace("&#10;", "\n");
    }

    // Its hover text: data-title when it has one (the rich tooltip), else the plain title.
    QString tip(const QString& id) const {
      const QString t = tag(id);
      const QString v = attr(t, "data-title");
      return v.isEmpty() ? attr(t, "\\stitle") : v;
    }
  };

}  // namespace stencil::test
