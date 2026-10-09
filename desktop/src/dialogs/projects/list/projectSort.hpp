#pragma once
// The projects list's sort modes and their order: the desktop twin of the browser's
// js/ui/projects/window/projectSort.js, pure and widget-free so a headless test can walk it.
#include <QHash>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace stencil::gui {

  // `date` is the last save, `opened` when this app last opened it (0 = never).
  struct ProjectSortItem {
    bool remote = false;
    int idx = 0;
    QString key;
    QString name;   // lowercased
    long long date = 0;
    long long opened = 0;
  };

  inline const QString PROJECT_SORT_DEFAULT = QStringLiteral("opened-desc");

  // The browser's SORT_MODES with their labels, in the dropdown's order.
  inline const std::vector<std::pair<QString, QString>>& projectSortModes() {
    static const std::vector<std::pair<QString, QString>> modes = {
        {QStringLiteral("opened-desc"), QStringLiteral("Recently opened")},
        {QStringLiteral("opened-asc"), QStringLiteral("Least recently opened")},
        {QStringLiteral("name"), QStringLiteral("Name")},
        {QStringLiteral("local"), QStringLiteral("Local first")},
        {QStringLiteral("server"), QStringLiteral("Server first")},
        {QStringLiteral("date-desc"), QStringLiteral("Recently edited")},
        {QStringLiteral("date-asc"), QStringLiteral("Least recently edited")},
        {QStringLiteral("manual"), QStringLiteral("Manual order")},
    };
    return modes;
  }

  // Stable tiebreak: name, then newest, then key. An unknown mode sorts by name.
  inline void sortProjectItems(std::vector<ProjectSortItem>& items, const QString& mode,
                               const QStringList& manualOrder = {}) {
    QHash<QString, int> pos;
    for (int i = 0; i < manualOrder.size(); ++i) pos.insert(manualOrder[i], i);
    const auto cmpName = [](const ProjectSortItem& a, const ProjectSortItem& b) {
      if (const int c = QString::localeAwareCompare(a.name, b.name)) return c < 0;
      if (a.date != b.date) return a.date > b.date;
      return QString::compare(a.key, b.key) < 0;
    };
    std::stable_sort(items.begin(), items.end(), [&](const ProjectSortItem& a, const ProjectSortItem& b) {
      if (mode == QLatin1String("local") && a.remote != b.remote) return !a.remote;
      if (mode == QLatin1String("server") && a.remote != b.remote) return a.remote;
      if (mode == QLatin1String("opened-desc") && a.opened != b.opened) return a.opened > b.opened;
      if (mode == QLatin1String("opened-asc") && a.opened != b.opened) return a.opened < b.opened;
      if (mode == QLatin1String("date-desc") && a.date != b.date) return a.date > b.date;
      if (mode == QLatin1String("date-asc") && a.date != b.date) return a.date < b.date;
      if (mode == QLatin1String("manual")) {
        const int pa = pos.value(a.key, std::numeric_limits<int>::max());
        const int pb = pos.value(b.key, std::numeric_limits<int>::max());
        if (pa != pb) return pa < pb;
      }
      return cmpName(a, b);
    });
  }

}  // namespace stencil::gui
