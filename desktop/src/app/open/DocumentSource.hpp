#pragma once
#include "fileStore.hpp"   // core::CropRect

#include <QByteArray>
#include <QString>

namespace stencil::gui {

  // How the Open-Image stage asked the next picture to be cropped.
  struct QuickCropOpts {
    enum class Mode { AUTO, PAGE, NONE };
    Mode mode = Mode::AUTO;
    bool album = false;
    QString page;
    // The Open-Image crop stage's rect in original-image px; width 0 = none dragged, so the page-aspect crop centres.
    core::CropRect rect;
    static QuickCropOpts none() { return {Mode::NONE, false, QString(), {}}; }
  };

  // Where the open picture came from — its provenance, its untouched bytes, a blank's fill — and
  // what the next load was asked to carry: provenance, a server target, a crop, a layout.
  struct DocumentSource {
    QString currentSource;
    QString currentResource;
    QByteArray sourceBytes;
    QString sourceExt;
    QString blankColor;
    QString pendingProvSource;
    QString pendingProvResource;
    QString pendingServerTarget;
    QuickCropOpts pendingCrop;
    QString pendingLaunchLayout;
    QString pendingLaunchLayoutJson;
    // The canvas picture generation the window's MediaLoader open and the boot restore began under;
    // while the canvas still stands at `sessionLoad`, the restore is decoding and holds the first show.
    quint64 mediaLoad = 0;
    quint64 sessionLoad = 0;
    bool revealHeld = false;

    void setBytes(const QByteArray& bytes, const QString& ext) {
      sourceBytes = bytes;
      sourceExt = ext.trimmed().toLower();
    }
  };

}  // namespace stencil::gui
