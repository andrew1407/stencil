#pragma once
// How GenieGhost paints genieWarp.hpp's bands on the raster engine, which pays per scanline span: runs one
// straight map covers go out as one draw, and a sideways flow's thin, full-height bands are drawn as rows
// into a transposed buffer that turns back once. Desktop only: the browser has a GPU.
#include "genieWarp.hpp"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QVector>

namespace stencil::support::genie {

  // A painted band may stray this far from the bands it stands for (px).
  inline constexpr double MERGE_TOL_PX = 0.5;
  // Past this many draws a sideways flow's spans cost more than the buffer's turn back (measured on cocoa @2x).
  inline constexpr int TRANSPOSE_MIN_DRAWS = 16;

  // `s` placed through the map that takes `from`'s source onto its target.
  QRectF mapped(const Strip& from, const QRectF& s);
  bool within(const QRectF& a, const QRectF& b, double tol);
  // `in`'s bands as the fewest draws that land each band within `tol` of its own target.
  void mergeStrips(const QVector<Strip>& in, QVector<Strip>& out, double tol = MERGE_TOL_PX);

  inline QRectF transposed(const QRectF& r) { return QRectF(r.y(), r.x(), r.height(), r.width()); }
  void transposeInto(const QVector<Strip>& in, QVector<Strip>& out);
  // x and y swapped: a quarter turn and a mirror, both on Qt's vectorised paths (a hand loop ran slower).
  QImage transposed(const QImage& img);

  // `strips` (targets in the coordinates `origin` is given in) from `shot` onto `p`, inside `clip` (p's coordinates).
  void drawStrips(QPainter& p, const QImage& shot, const QVector<Strip>& strips, QPointF origin, const QRectF& clip);
  void drawStrips(QPainter& p, const QPixmap& shot, const QVector<Strip>& strips, QPointF origin, const QRectF& clip);
  // `dirty` as the few x-runs it spans, each as tall as its rects: QRegion's y-bands cut a column footprint into slivers.
  QVector<QRect> columnsOf(const QRegion& dirty);
  // drawStrips for a sideways flow: each dirty rect's bands drawn as rows into a buffer, turned back onto `p`;
  // `scratch`, when given, keeps that buffer across frames.
  void drawStripsAcross(QPainter& p, const QImage& shotT, const QVector<Strip>& stripsT, QPointF origin,
                        const QRegion& dirty, qreal dpr, QImage* scratch = nullptr);

}  // namespace stencil::support::genie
