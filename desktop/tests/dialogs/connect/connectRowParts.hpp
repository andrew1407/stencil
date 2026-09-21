#pragma once
// The connect-row suite's sections, one TU each behind this header, called in this order from
// main(); each takes only the parts of the shared fixture it reads. checkRowLayout hands back
// the row widget it measured, which every later section works on.
#include "ConnectDialog.hpp"
#include "theme.hpp"   // the app stylesheet these metrics are measured under
#include "DisintegrateOverlay.hpp"
#include "DissolveEffect.hpp"   // the scroll-edge fade the rows carry
#include "filterFade.hpp"       // …and the lighter one a FILTER change plays
#include "ServerClient.hpp"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsOpacityEffect>
#include <QHostAddress>
#include <QCheckBox>
#include <QLabel>
#include "FlowLayout.hpp"
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <functional>

using stencil::gui::ConnectDialog;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::DissolveEffect;
using stencil::gui::FILTER_FADE_MS;
using stencil::gui::FILTER_FADE_PROPERTY;
using stencil::gui::FILTER_FULL_HEIGHT_ROLE;
using stencil::net::ConnectionManager;

#include "../../support/check.hpp"
#include "../../support/connectNow.hpp"

static void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

namespace connectrow {

  QWidget* checkRowLayout(const QString& longUrl, QListWidget* list);
  void checkRowMotion(QTcpServer& server, const QString& longUrl, const QString& shortUrl, ConnectionManager& mgr, ConnectDialog& dlg, QListWidget* list, QWidget* row);
  void checkScrollEdges(quint16 port, QListWidget* list, QWidget* row);
  void checkConnectGestures(QTcpServer& server, quint16 port, QListWidget* list, QWidget* row);
  void checkToastAndHint(QTcpServer& server, quint16 port, QWidget* row);

}  // namespace connectrow
