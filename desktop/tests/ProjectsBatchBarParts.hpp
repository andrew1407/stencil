#pragma once
// Shared ground for the Projects batch-bar headless TUs: the fixture helpers and the section
// each .cpp contributes. `failures` is inline, and a section returns 1 to abort as main() did.
#include "controlReveal.hpp"   // CONTROL_REVEAL_IN_MS: the batch group's slot timing
#include "DisintegrateOverlay.hpp"
#include "fileStore.hpp"
#include "filterFade.hpp"   // the light enter/exit transition the search/filter uses
#include "ProjectsDialog.hpp"
#include "ServerClient.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QToolButton>
#include <cstdio>
#include <functional>
#include <vector>

using stencil::gui::BatchDirections;
using stencil::gui::batchDirectionsFor;
using stencil::gui::DisintegrateOverlay;
using stencil::gui::filteredIn;
using stencil::gui::FILTER_DUST_OBJECT_NAME;
using stencil::gui::FILTER_DUST_ROLE;
using stencil::gui::FILTER_FADE_MS;
using stencil::gui::Project;
using stencil::gui::ProjectsDialog;
using stencil::net::ConnectionManager;

#include "support/check.hpp"
#include "support/connectNow.hpp"

inline void pumpFor(int ms) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

inline void pumpUntil(const std::function<bool()>& pred, int timeoutMs = 3000) {
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// The batch bar's buttons carry fixed labels — find each by its exact text.
inline QPushButton* btnByText(QWidget* root, const QString& text) {
  for (QPushButton* b : root->findChildren<QPushButton*>())
    if (b->text() == text) return b;
  return nullptr;
}

inline Project makeLocal(const QString& id, const QString& name, long long updatedAt) {
  Project pr;
  pr.meta.id = id.toStdString();
  pr.meta.name = name.toStdString();
  pr.meta.updatedAt = updatedAt;
  return pr;
}

inline QListWidgetItem* rowById(QListWidget* list, const QString& id, bool remote) {
  for (int i = 0; i < list->count(); ++i) {
    QListWidgetItem* it = list->item(i);
    if (it->data(Qt::UserRole).toString() == id &&
        it->data(Qt::UserRole + 1).toString().isEmpty() != remote)
      return it;
  }
  return nullptr;
}

int batchDirectionMatrix();
int pinnedRows(const std::vector<Project>& locals);
int batchBarVisibility(const std::vector<Project>& locals, ConnectionManager& mgr);
int batchRemoveAndCheckboxPress(const std::vector<Project>& locals);
int filterTransitions(const std::vector<Project>& locals);
