#include "fontmatching.h"

#include <QChar>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <vector>

namespace FontMatching
{

namespace
{

bool lessCaseInsensitive(const QString &a, const QString &b)
{
    const int folded = QString::compare(a, b, Qt::CaseInsensitive);
    return folded != 0 ? folded < 0 : a < b;
}

// "Nimbus Sans [URW ]" -> "Nimbus Sans"; a name without a foundry suffix is
// returned as it is.
QString withoutFoundry(const QString &listed)
{
    static const QRegularExpression suffix(QStringLiteral("^(.+) \\[[^\\]]*\\]$"));
    const QRegularExpressionMatch match = suffix.match(listed);
    return match.hasMatch() ? match.captured(1) : listed;
}

QString patternKey(const QList<FamilyName> &pattern)
{
    QString key;
    for (const FamilyName &entry : pattern) {
        key += entry.name + QChar(0x1f) + entry.lang + QChar(0x1e);
    }
    return key;
}

// Fuzzy scoring constants; fontmatching.h spells out what each one does.
constexpr int ScoreMatch = 16;
constexpr int GapStart = -3;
constexpr int GapExtension = -1;
constexpr int BonusStart = 10;
constexpr int BonusBoundary = 8;
constexpr int BonusCamel = 7;
constexpr int BonusConsecutive = 4;
constexpr int FirstCharMultiplier = 2;
constexpr int NoScore = std::numeric_limits<int>::min() / 2;

enum class CharClass { Delimiter, Lower, Upper, Digit, Cjk, OtherLetter };

CharClass classOf(char32_t c)
{
    switch (QChar::script(c)) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
    case QChar::Script_Bopomofo:
        return CharClass::Cjk;
    default:
        break;
    }
    if (QChar::isDigit(c)) {
        return CharClass::Digit;
    }
    if (QChar::isLower(c)) {
        return CharClass::Lower;
    }
    if (QChar::isUpper(c) || QChar::isTitleCase(c)) {
        return CharClass::Upper;
    }
    return QChar::isLetterOrNumber(c) ? CharClass::OtherLetter : CharClass::Delimiter;
}

int bonusAt(CharClass previous, CharClass current, bool first)
{
    if (current == CharClass::Delimiter) {
        return 0;
    }
    if (first) {
        return BonusStart;
    }
    if (current == CharClass::Cjk || previous == CharClass::Cjk || previous == CharClass::Delimiter) {
        return BonusBoundary;
    }
    if (previous == CharClass::Lower && current == CharClass::Upper) {
        return BonusCamel;
    }
    if (previous != CharClass::Digit && current == CharClass::Digit) {
        return BonusCamel;
    }
    return 0;
}

bool hasWidthWord(const QString &styleName)
{
    static const QStringList words = {
        QStringLiteral("condensed"), QStringLiteral("compressed"), QStringLiteral("narrow"),
        QStringLiteral("expanded"),  QStringLiteral("extended"),   QStringLiteral("wide"),
    };
    return std::any_of(words.cbegin(), words.cend(), [&styleName](const QString &word) {
        return styleName.contains(word, Qt::CaseInsensitive);
    });
}

bool preferStyle(const QString &a, const QString &b)
{
    const bool aWidth = hasWidthWord(a);
    const bool bWidth = hasWidthWord(b);
    if (aWidth != bWidth) {
        return !aWidth;
    }
    if (a.size() != b.size()) {
        return a.size() < b.size();
    }
    return lessCaseInsensitive(a, b);
}

QList<Weight> weightsOf(const QList<Face> &faces, bool upright)
{
    QMap<int, QString> byWeight;
    for (const Face &face : faces) {
        if (face.upright != upright || face.weight < 1) {
            continue;
        }
        const auto it = byWeight.constFind(face.weight);
        if (it == byWeight.cend() || preferStyle(face.styleName, *it)) {
            byWeight.insert(face.weight, face.styleName);
        }
    }
    QList<Weight> result;
    for (auto it = byWeight.cbegin(); it != byWeight.cend(); ++it) {
        result.append({it.key(), it.value()});
    }
    return result;
}

} // namespace

QList<QStringList> groupFamilyNames(const QList<FamilyName> &names)
{
    QList<QStringList> groups;
    QStringList groupLangs;
    bool open = false;
    QSet<QString> seen;
    for (const FamilyName &entry : names) {
        if (entry.name.isEmpty() || seen.contains(entry.name)) {
            continue;
        }
        seen.insert(entry.name);
        const QString lang = entry.lang.toLower();
        if (!open || lang.isEmpty() || groupLangs.contains(lang)) {
            groups.append(QStringList{entry.name});
            groupLangs = QStringList{lang};
            open = !lang.isEmpty();
            continue;
        }
        groups.last().append(entry.name);
        groupLangs.append(lang);
    }
    return groups;
}

NameIndex buildNameIndex(const QList<QList<FamilyName>> &patterns, const QStringList &listed)
{
    NameIndex index;
    const QSet<QString> listedSet(listed.cbegin(), listed.cend());
    QHash<QString, QStringList> listedByBare;
    for (const QString &name : listed) {
        index.listedByName.insert(name, name);
        const QString bare = withoutFoundry(name);
        if (bare != name) {
            listedByBare[bare].append(name);
        }
    }
    for (QStringList &names : listedByBare) {
        std::sort(names.begin(), names.end(), lessCaseInsensitive);
    }
    const auto listedFor = [&](const QString &name) -> QStringList {
        return listedSet.contains(name) ? QStringList{name} : listedByBare.value(name);
    };

    // FcFontList returns patterns in cache order. Sorting them makes every
    // "first one wins" below independent of it.
    QMap<QString, const QList<FamilyName> *> ordered;
    for (const QList<FamilyName> &pattern : patterns) {
        ordered.insert(patternKey(pattern), &pattern);
    }

    // 1 = mapped through a listed name of its own group, 2 = through the
    // pattern's first listed name. A lower rank replaces a higher one.
    QHash<QString, int> rank;
    const auto map = [&](const QString &name, const QString &target, int newRank) {
        if (listedSet.contains(name)) {
            return;
        }
        const auto it = rank.constFind(name);
        if (it == rank.cend() || newRank < *it) {
            rank.insert(name, newRank);
            index.listedByName.insert(name, target);
        }
    };

    for (const QList<FamilyName> *pattern : std::as_const(ordered)) {
        const QList<QStringList> groups = groupFamilyNames(*pattern);
        QList<QStringList> groupListed;
        QString first;
        for (const QStringList &group : groups) {
            QStringList found;
            for (const QString &name : group) {
                for (const QString &candidate : listedFor(name)) {
                    if (!found.contains(candidate)) {
                        found.append(candidate);
                    }
                }
            }
            if (first.isEmpty() && !found.isEmpty()) {
                first = found.first();
            }
            groupListed.append(found);
        }
        if (first.isEmpty()) {
            continue;
        }
        for (qsizetype i = 0; i < groups.size(); ++i) {
            const QStringList &group = groups.at(i);
            const QStringList &found = groupListed.at(i);
            for (const QString &name : group) {
                map(name, found.isEmpty() ? first : found.first(), found.isEmpty() ? 2 : 1);
            }
            for (const QString &family : found) {
                index.backed.insert(family);
                QStringList &alternates = index.alternates[family];
                for (const QString &name : group) {
                    if (!listedFor(name).contains(family) && !alternates.contains(name)) {
                        alternates.append(name);
                    }
                }
            }
        }
    }

    for (auto it = index.alternates.begin(); it != index.alternates.end();) {
        if (it->isEmpty()) {
            it = index.alternates.erase(it);
            continue;
        }
        std::sort(it->begin(), it->end(), lessCaseInsensitive);
        ++it;
    }
    return index;
}

std::optional<int> fuzzyScore(QStringView query, QStringView candidate)
{
    std::vector<char32_t> needle;
    for (const char32_t c : query.toUcs4()) {
        if (!QChar::isSpace(c)) {
            needle.push_back(QChar::toCaseFolded(c));
        }
    }
    if (needle.empty()) {
        return 0;
    }
    const QList<uint> original = candidate.toUcs4();
    const size_t n = size_t(original.size());
    const size_t m = needle.size();
    if (m > n) {
        return std::nullopt;
    }

    std::vector<char32_t> hay(n);
    std::vector<int> bonus(n);
    CharClass previous = CharClass::Delimiter;
    for (size_t j = 0; j < n; ++j) {
        const char32_t c = original.at(qsizetype(j));
        const CharClass current = classOf(c);
        hay[j] = QChar::toCaseFolded(c);
        bonus[j] = bonusAt(previous, current, j == 0);
        previous = current;
    }

    // Cheap rejection before the table: is the needle a subsequence at all?
    for (size_t i = 0, j = 0; i < m; ++j) {
        if (j == n) {
            return std::nullopt;
        }
        if (hay[j] == needle[i]) {
            ++i;
        }
    }

    // best[i][j]: the best score for needle[0..i] with needle[i] matched at
    // hay[j]. reach[i][s]: the best best[i][k] over k <= s - 2, less the gap
    // penalty for the s - k - 1 characters skipped between k and s -- that
    // is, the best way to have matched needle[0..i] and then start a new run
    // at s.
    std::vector<int> best(m * n, NoScore);
    std::vector<int> reach(m * n, NoScore);
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = i; j < n; ++j) {
            if (hay[j] != needle[i]) {
                continue;
            }
            int top = NoScore;
            // The run of consecutive matches ending at j, for each length:
            // it covers needle[t..i] at hay[s..j].
            for (size_t length = 1; length <= i + 1 && length <= j + 1; ++length) {
                const size_t s = j + 1 - length;
                const size_t t = i + 1 - length;
                if (hay[s] != needle[t]) {
                    break;
                }
                const int before = t == 0 ? 0 : reach[(t - 1) * n + s];
                if (before == NoScore) {
                    continue;
                }
                int run = ScoreMatch * int(length) + bonus[s] * (t == 0 ? FirstCharMultiplier : 1);
                for (size_t p = s + 1; p <= j; ++p) {
                    run += std::max({bonus[p], bonus[s], BonusConsecutive});
                }
                top = std::max(top, before + run);
            }
            best[i * n + j] = top;
        }
        if (i + 1 < m) {
            int running = NoScore;
            for (size_t s = 0; s < n; ++s) {
                if (running != NoScore) {
                    running += GapExtension;
                }
                if (s >= 2 && best[i * n + s - 2] != NoScore) {
                    running = std::max(running, best[i * n + s - 2] + GapStart);
                }
                reach[i * n + s] = running;
            }
        }
    }

    const auto last = best.cbegin() + std::ptrdiff_t((m - 1) * n);
    const int score = *std::max_element(last, last + std::ptrdiff_t(n));
    if (score == NoScore) {
        return std::nullopt;
    }
    return score;
}

QList<qsizetype> fuzzyRank(QStringView query, const QList<QStringList> &candidates)
{
    QList<std::pair<int, qsizetype>> scored;
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        std::optional<int> top;
        for (const QString &name : candidates.at(i)) {
            const std::optional<int> score = fuzzyScore(query, name);
            if (score && (!top || *score > *top)) {
                top = score;
            }
        }
        if (top) {
            scored.append({*top, i});
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    QList<qsizetype> result;
    result.reserve(scored.size());
    for (const auto &[score, i] : std::as_const(scored)) {
        result.append(i);
    }
    return result;
}

QList<Weight> uprightWeights(const QList<Face> &faces)
{
    return weightsOf(faces, true);
}

QList<Weight> italicWeights(const QList<Face> &faces)
{
    return weightsOf(faces, false);
}

int snapWeight(const QList<int> &available, int target)
{
    if (available.isEmpty() || available.contains(target)) {
        return target;
    }
    std::optional<int> heaviestBelow;
    std::optional<int> lightestAbove;
    for (const int weight : available) {
        if (weight < target && (!heaviestBelow || weight > *heaviestBelow)) {
            heaviestBelow = weight;
        }
        if (weight > target && (!lightestAbove || weight < *lightestAbove)) {
            lightestAbove = weight;
        }
    }
    if (target >= 400 && target <= 500) {
        if (lightestAbove && *lightestAbove <= 500) {
            return *lightestAbove;
        }
        return heaviestBelow ? *heaviestBelow : *lightestAbove;
    }
    if (target < 400) {
        return heaviestBelow ? *heaviestBelow : *lightestAbove;
    }
    return lightestAbove ? *lightestAbove : *heaviestBelow;
}

} // namespace FontMatching
