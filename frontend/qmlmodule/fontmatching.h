#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

// The logic behind FontCatalog, kept free of QFontDatabase and fontconfig so
// that tests can drive it with made-up data. FontCatalog only gathers the
// inputs from the two databases and hands them here.
namespace FontMatching
{

// One FC_FAMILY value of a fontconfig pattern and the FC_FAMILYLANG at the
// same index; `lang` is empty when the pattern carries none for it.
struct FamilyName
{
    QString name;
    QString lang;
};

// Splits one pattern's family names, in fontconfig's order, into groups that
// each name one family in several languages. A group is a run of names with
// distinct languages; a language already present in the current group starts
// the next one. fontconfig lists the names level by level -- the typographic
// family in each of its languages, then the legacy family in each of its
// languages -- so
//   "Ryumin Pr5"@en, "リュウミン Pr5"@ja, "Ryumin Pr5 EB-KL"@en, "リュウミン Pr5 EB-KL"@ja
// becomes two pairs, while "DejaVu Sans"@en, "DejaVu Sans Condensed"@en --
// two different families -- becomes two single names. FcFontList moves the
// name in the process language to the front ("リュウミン Pr5"@ja,
// "Ryumin Pr5"@en, ...), which leaves the same groups. The order carries no
// level boundary, so a level whose first language the previous level lacks
// is joined onto it. A name without a language is never paired: it is a
// group on its own. A name repeated within the pattern is dropped.
QList<QStringList> groupFamilyNames(const QList<FamilyName> &names);

struct NameIndex
{
    // Every name fontconfig knows an installed family by, and every name Qt
    // lists, mapped to the name Qt lists it under.
    QHash<QString, QString> listedByName;
    // Listed name -> the other names of its group, sorted case-insensitively.
    QHash<QString, QStringList> alternates;
    // The listed names that are a fontconfig family. The rest are Qt's own
    // generic entries ("Sans Serif", "Serif", "Monospace").
    QSet<QString> backed;
};

// Joins fontconfig's patterns with the names Qt lists (the unfiltered
// QFontDatabase::families()). Qt lists some of a pattern's names and treats
// the rest as aliases of its first one; a family found in several foundries
// it lists once per foundry, as "Family [Foundry]". A name maps to
// the listed name of its own group, else -- a group Qt folded into another,
// such as "A-OTF Ryumin Pr5 EB-KL" -- to the pattern's first listed name. A
// bare name listed only with foundries maps to the first of those in sort
// order. The result does not depend on the order of `patterns`.
NameIndex buildNameIndex(const QList<QList<FamilyName>> &patterns, const QStringList &listed);

// fzf-style score of `query` against one name, or nullopt when the query's
// characters do not all appear in it in order. Case-insensitive; whitespace
// in the query is ignored, and an empty query scores 0. Each matched
// character scores 16 plus a bonus for where it sits: 10 at the start of the
// name, 8 at a word start (after a space, '-', '_' or any other
// non-alphanumeric character, or on either side of a CJK character, so each
// CJK character is a word of its own), 7 at a lowercase-to-uppercase or
// letter-to-digit step. The first query character's bonus counts twice, and
// a character continuing a run of consecutive matches gets at least the
// run's first bonus, and at least 4. Skipped characters between two matches
// cost 3 for the first and 1 for each further one; characters before the
// first match and after the last are free. The best alignment is taken.
std::optional<int> fuzzyScore(QStringView query, QStringView candidate);

// Indices into `candidates` of the entries matching `query`, best score
// first; an entry scores the best of its names. Equal scores keep their
// order in `candidates`.
QList<qsizetype> fuzzyRank(QStringView query, const QList<QStringList> &candidates);

struct Face
{
    QString styleName;
    int weight;
    bool upright;
};

struct Weight
{
    int weight;
    QString styleName;
};

// One entry per distinct weight among the upright faces, lightest first.
// Faces with no weight (below 1) are skipped. When several styles share a
// weight, a name without a width word (Condensed, Expanded, Narrow, ...) is
// preferred, then the shorter name, then the alphabetically first.
QList<Weight> uprightWeights(const QList<Face> &faces);

// The same over the slanted faces, italic and oblique alike: the ones Qt
// draws from when a font asks for italic. Empty for a family that has none,
// which Qt then slants itself from an upright face.
QList<Weight> italicWeights(const QList<Face> &faces);

// The CSS Fonts level 4 weight matching rule; FontCatalog::snapWeight()
// documents it.
int snapWeight(const QList<int> &available, int target);

} // namespace FontMatching
