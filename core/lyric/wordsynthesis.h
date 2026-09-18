#pragma once

#include "lyricmodel.h"

namespace PlasmaLyrics {

/// Builds a fake word-by-word timeline for a line whose lyric source never
/// carries one, so the existing word renderer can be reused untouched. See
/// DESIGN.md's "外观" decision on synthetic word-by-word for the data this is
/// based on (per-character p50 288 ms, prefix-sum timing to avoid drift).
///
/// Splitting: a CJK character (Han/Hiragana/Katakana/Hangul/Bopomofo) is its
/// own token; any other non-whitespace run (Latin letters, digits,
/// punctuation) accumulates into one token until whitespace or a CJK
/// character closes it. Whitespace never becomes its own token -- it is
/// always folded onto the tail of the token before it (LyricLine.qml's
/// `wordRow` is a Row with no spacing, so a dropped space would visibly
/// glue two words together). Every character's weight is 1, whitespace
/// included: the existing renderer already ignores glyph width (a Latin
/// 'i' and a full-width Han character both count as 1 there), so giving
/// whitespace a different weight would be the one place this stopped being
/// true.
///
/// Returns an empty list when the line has no non-whitespace text or its
/// [startMs, endMs) span is empty or inverted -- both are treated as "cannot
/// synthesize", not an error.
///
/// Two known boundary gaps, both from scanning line.text one QChar (one
/// UTF-16 code unit) at a time rather than one Unicode code point at a
/// time. An astral-plane character -- one outside the Basic Multilingual
/// Plane, e.g. a CJK Extension B+ ideograph or most emoji -- is encoded as
/// a surrogate pair, i.e. two QChars. QChar::script() on a lone surrogate
/// never resolves to a CJK script, so isCjkChar() classifies both halves as
/// "non-whitespace, non-CJK": they take the accumulating-run branch rather
/// than the CJK per-character branch a BMP CJK character would. Concretely:
/// - No garbling: both halves take the *same* branch and that branch always
///   appends rather than splitting mid-character, so a surrogate pair is
///   never torn apart across two tokens.
/// - Two skews instead: (1) a run of several astral CJK characters comes
///   out as one token, not one per character, unlike the same run in BMP
///   CJK; (2) an astral character counts as weight 2 (two QChars) rather
///   than 1, so it is allotted roughly twice the duration of an equivalent
///   BMP character -- a timing skew, not a correctness bug.
/// Both are accepted rather than fixed: the corpus behind DESIGN.md decision
/// 74 (204 worded documents, 1022+ lines) contains no astral-plane lyrics,
/// so neither has been observed to matter. Revisit by iterating line.text by
/// Unicode code point (surrogate pairs joined before classifying) instead of
/// by QChar, the day a real lyric line is found to contain one.
///
/// A third gap has no corpus behind it in either direction: Hangul is in the
/// CJK set purely by extension of "CJK" in DESIGN.md decisions 8 and 74 --
/// every other rule here (CJK is single-character, Latin is space-delimited,
/// punctuation attaches to the preceding word) is backed by a percentage
/// from the verified corpus, and that corpus has zero Korean lines to check
/// per-character splitting against for Hangul specifically. Kept as the most
/// consistent reading of "CJK" until a Korean sample can confirm or refute
/// it.
QList<LyricWord> synthesizeWords(const LyricLine &line);

} // namespace PlasmaLyrics
