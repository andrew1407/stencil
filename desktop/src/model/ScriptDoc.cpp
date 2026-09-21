#include "ScriptDoc.hpp"

#include "parser.hpp"   // core/script DIRECTIVE_WORDS — never mirrored above this seam
#include "scriptProgram.hpp"  // core/script — this file is the seam that may include it

#include <QHash>
#include <QSet>
#include <QList>
#include <QStringView>

namespace stencil::model {

  namespace {

    namespace cs = stencil::core::script;

    // A reorder in core would silently mis-colour or mis-dispatch: the casts below are
    // ordinal, so pin the ends of every enum this file maps.
    static_assert(static_cast<int>(cs::TokenKind::COMMENT) ==
                      static_cast<int>(ScriptTokenKind::COMMENT) &&
                  static_cast<int>(cs::TokenKind::ERROR) ==
                      static_cast<int>(ScriptTokenKind::ERROR));
    static_assert(static_cast<int>(cs::OpKind::OPEN) == static_cast<int>(ScriptOpKind::OPEN) &&
                  static_cast<int>(cs::OpKind::REDO) == static_cast<int>(ScriptOpKind::REDO));
    static_assert(static_cast<int>(cs::SourceKind::PROJECT) ==
                      static_cast<int>(ScriptSourceKind::PROJECT) &&
                  static_cast<int>(cs::SourceKind::GLOB) ==
                      static_cast<int>(ScriptSourceKind::GLOB));

    QString qstr(const std::string& s) { return QString::fromStdString(s); }

    /* Byte columns -> QChar columns, per line. The core lexes UTF-8, so every col and len it
     * reports counts bytes; QTextDocument counts QChars. An all-ASCII line maps to itself
     * and is never tabulated, which is the overwhelmingly common case. */
    class ByteColumns {
     public:
      explicit ByteColumns(const QString& text);
      int col(int line, int byteCol) const;
      int len(int line, int byteCol, int byteLen) const;

     private:
      QHash<int, QVector<int>> maps;   // 1-based line -> byte offset -> QChar offset
    };

    ByteColumns::ByteColumns(const QString& text) {
      const QList<QStringView> lines = QStringView(text).split(QLatin1Char('\n'));
      for (int i = 0; i < lines.size(); ++i) {
        const QStringView line = lines.at(i);
        bool wide = false;
        for (const QChar c : line)
          if (c.unicode() > 0x7F) { wide = true; break; }
        if (!wide) continue;
        QVector<int> map;
        map.reserve(line.size() * 2 + 1);
        for (int j = 0; j < line.size();) {
          const bool pair = line.at(j).isHighSurrogate() && j + 1 < line.size() &&
                            line.at(j + 1).isLowSurrogate();
          const char32_t cp = pair ? QChar::surrogateToUcs4(line.at(j), line.at(j + 1))
                                   : line.at(j).unicode();
          const int bytes = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
          for (int b = 0; b < bytes; ++b) map.push_back(j);
          j += pair ? 2 : 1;
        }
        map.push_back(static_cast<int>(line.size()));   // one past the end
        maps.insert(i + 1, map);
      }
    }

    int ByteColumns::col(int line, int byteCol) const {
      const auto it = maps.constFind(line);
      if (it == maps.constEnd()) return byteCol;
      return it->at(qBound(0, byteCol - 1, static_cast<int>(it->size()) - 1)) + 1;
    }

    int ByteColumns::len(int line, int byteCol, int byteLen) const {
      if (!maps.contains(line) || byteLen <= 0) return byteLen;
      return col(line, byteCol + byteLen) - col(line, byteCol);
    }

    ScriptToken toToken(const cs::Token& t, const ByteColumns& cols) {
      ScriptToken out;
      out.line = t.line;
      out.col = cols.col(t.line, t.col);
      out.len = cols.len(t.line, t.col, t.len);
      out.kind = static_cast<ScriptTokenKind>(static_cast<int>(t.kind));
      return out;
    }

    ScriptDiagnostic toDiagnostic(const cs::Diagnostic& d, const ByteColumns& cols) {
      ScriptDiagnostic out;
      out.isError = d.severity == cs::Severity::ERROR;
      out.code = qstr(d.code);
      out.line = d.line;
      out.col = cols.col(d.line, d.col);
      out.len = cols.len(d.line, d.col, d.len);
      out.message = qstr(d.message);
      return out;
    }

    ScriptOp toOp(const cs::Op& op, const ByteColumns& cols) {
      ScriptOp out;
      out.kind = static_cast<ScriptOpKind>(static_cast<int>(op.kind));
      out.block = op.block;
      out.line = op.line;
      out.col = cols.col(op.line, op.col);
      out.editIndex = op.editIndex;
      for (const std::string& s : op.strs) out.strs.push_back(qstr(s));
      for (const std::string& s : op.toks) out.toks.push_back(qstr(s));
      for (double n : op.nums) out.nums.push_back(n);
      return out;
    }

    // Rebuilds the core op a resolve needs, from the Qt copy the GUI carries.
    cs::Op fromOp(const ScriptOp& op) {
      cs::Op out;
      out.kind = static_cast<cs::OpKind>(static_cast<int>(op.kind));
      for (const QString& s : op.strs) out.strs.push_back(s.toStdString());
      for (const QString& s : op.toks) out.toks.push_back(s.toStdString());
      for (double n : op.nums) out.nums.push_back(n);
      return out;
    }

    // CSS pixels per cm at 96 dpi — the basis the crop parser and the browser share.
    constexpr double PX_PER_CM = 96.0 / 2.54;

  }  // namespace

  ScriptDoc ScriptDoc::parse(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    const cs::ScriptProgram p = cs::ScriptProgram::parse(utf8.constData(), utf8.size());
    const ByteColumns cols(text);

    ScriptDoc out;
    for (const cs::Token& t : p.getTokens()) out.tokens.push_back(toToken(t, cols));
    for (const cs::Diagnostic& d : p.getDiagnostics()) out.diagnostics.push_back(toDiagnostic(d, cols));
    for (const cs::Op& op : p.getOps()) out.ops.push_back(toOp(op, cols));
    for (const cs::Block& b : p.getBlocks()) {
      ScriptBlock block;
      block.source = qstr(b.source);
      block.kind = static_cast<ScriptSourceKind>(static_cast<int>(b.kind));
      block.frame = b.frame;
      block.opStart = b.opStart;
      block.opCount = b.opCount;
      out.blocks.push_back(block);
    }
    return out;
  }

  bool ScriptDoc::isDirectiveWord(QStringView word) {
    static const QSet<QString> words = [] {
      QSet<QString> set;
      for (const cs::DirectiveWord& d : cs::DIRECTIVE_WORDS)
        set.insert(QString::fromUtf8(d.word.data(), qsizetype(d.word.size())));
      return set;
    }();
    return words.contains(word.toString().toLower());
  }

  bool ScriptDoc::hasErrors() const {
    for (const ScriptDiagnostic& d : diagnostics)
      if (d.isError) return true;
    return false;
  }

  QVector<double> ScriptDoc::resolve(const ScriptOp& op, QSize imageSize) {
    const cs::Op core = fromOp(op);
    double buf[2 * (cs::MAX_POINTS_PER_LINE + 1)];
    const int n = cs::resolveOp(core, static_cast<double>(imageSize.width()),
                                static_cast<double>(imageSize.height()), PX_PER_CM, PX_PER_CM,
                                buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    QVector<double> out;
    for (int i = 0; i < n; ++i) out.push_back(buf[i]);
    return out;
  }

  core::CropRect ScriptDoc::cropRect(const ScriptOp& op, QSize imageSize, bool* ok) {
    const QVector<double> r = resolve(op, imageSize);
    if (ok) *ok = r.size() >= 4;
    core::CropRect rect;
    if (r.size() < 4) return rect;
    rect.x = r[0];
    rect.y = r[1];
    rect.width = r[2];
    rect.height = r[3];
    return rect;
  }

  /* A shape op becomes one Line: the resolved points, then thickness and pointSize.
   * `locked` is what closes it and enables the fill. */
  void ScriptDoc::appendLine(core::Lines& lines, const ScriptOp& op, QSize imageSize, bool* ok) {
    const QVector<double> r = resolve(op, imageSize);
    if (ok) *ok = r.size() >= 6;
    if (r.size() < 6) return;

    core::Line line;
    for (int i = 0; i + 3 < r.size(); i += 2) line.points.push_back({r[i], r[i + 1]});
    auto str = [&](int i) { return i < op.strs.size() ? op.strs[i].toStdString() : std::string(); };
    line.color = str(0);
    line.style = str(1);
    line.fillColor = str(2);
    line.pointColor = str(3);
    line.thickness = r[r.size() - 2];
    line.pointSize = r[r.size() - 1];
    line.locked = op.kind == ScriptOpKind::RECT;
    lines.push_back(line);
  }

}  // namespace stencil::model
