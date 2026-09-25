#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// DESIGN.md decision 77: the maths behind the word particles, kept apart from
// the scene-graph item (WordParticleLayer) that draws them so that all of it
// can be tested without Qt Quick -- the software backend the QML suite runs
// on draws no custom geometry at all. Everything here is a pure function of
// the lyric position: the same line, played or replayed, gives the same
// particles, and after a seek every line still held shows what playing up
// to that point would have. Only those lines can come back: a line a seek
// jumped over was never captured, and one already pruned -- the line before
// the target of a backward seek, say -- is gone, so neither shows the
// particles that playing would have left in the air.
//
// Lengths are logical pixels at the 34 px reference font size unless a name
// says otherwise, and are scaled by fontSize / 34 when evaluated. Birth
// positions and ψ are not: they are measured on the glyphs as drawn.
namespace WordParticles {

constexpr double kReferencePixelSize = 34;
constexpr double kLifeMs = 2050;
constexpr double kFadeInMs = 50;
constexpr double kFadeOutFromMs = 1550;
constexpr double kBirthSpreadMs = 90;
constexpr double kMaxSize = 4; // s = kMaxSize^r, so s is in [1, 4)
constexpr double kInkFrom = 0.10;
constexpr double kInkTo = 0.90;
constexpr double kBirthDepth = 0.35; // of the CJK ascent, down from the glyph top
constexpr double kRiseHeight = 36;
constexpr double kSway = 1;
constexpr double kSwayRampMs = 700;
constexpr double kWindPeriodMs = 3400;
constexpr double kWindPhasePerPixel = 0.012; // rad per logical pixel, unscaled
constexpr double kHaloRadius = 3.5; // times s
constexpr double kHaloCentre = 0.55;

/// splitmix64, with its own mapping to [0, 1). Not std::uniform_*_distribution:
/// those are implementation-defined, so one seed gives different particles on
/// libstdc++ and libc++ and no test could pin them.
class Random
{
public:
    explicit Random(quint64 seed);
    quint64 next();
    /// The top 53 bits over 2^53: in [0, 1), never 1.
    double unit();

private:
    quint64 m_state;
};

/// One seed per (line, word): replaying a line, or seeking back into it,
/// draws the same particles for every word.
quint64 seedFor(qint64 lineStartMs, int wordIndex);

struct Particle
{
    double birthMs = 0;
    // The start of the word it rose from: a line switched away from keeps
    // the particles of the words it had started singing.
    double wordStartMs = 0;
    // Birth point in the layer, before any offset the line is following.
    double x = 0;
    double y = 0;
    double size = 1; // s, at the reference size
    double jitter = 0; // j, in [-1, 1)
    double amplitude = 1; // amp, in [0.6, 1.4)
    double periodMs = 1400; // P1, in [1400, 2200)
    double phase1 = 0;
    double phase2 = 0;
    double phase3 = 0;
};

/// k = (s - 1) / 3: 0 for the smallest particle, 1 for the largest.
double depth(double size);

/// b(t) before the depth factor: 0 -> 1 over the first 50 ms, held until
/// 1550 ms, back to 0 at 2050 ms, and 0 outside [0, 2050].
double envelope(double ageMs);
/// b(t) × (0.45 + 0.55k).
double brightness(const Particle &particle, double ageMs);
/// How far the particle has risen, upward positive, before scaling.
double rise(const Particle &particle, double ageMs);
/// smoothstep(0, 700 ms): 0 at birth, so the particle leaves the glyph
/// straight up and only starts to sway after it.
double swayRamp(double ageMs);
/// w(t), the particle's own wander, before scaling.
double wander(const Particle &particle, double ageMs);
/// The shared current: every particle born near the same x at about the same
/// time drifts the same way. 0 at birth. Before scaling.
double wind(const Particle &particle, double ageMs);
/// Horizontal offset from the birth point, before scaling: wind + 0.5 w.
double drift(const Particle &particle, double ageMs);
/// Vertical offset from the birth point, downward positive, before scaling:
/// the bob minus the rise.
double fall(const Particle &particle, double ageMs);

struct WordSpan
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    // The word's ink in the layer: its glyph width with trailing whitespace
    // left out.
    double left = 0;
    double width = 0;
    // Where a CJK glyph's top is on this word's baseline, in the layer. Per
    // word, not per line: every word is its own Text, vertically centred on
    // its own line height, and a word the fallback CJK font draws sits on a
    // baseline a pixel or two away from a Latin word's.
    double top = 0;
};

/// A word's particles: none for a zero-length token, otherwise 1, 2 or 3
/// with equal odds, each born within the first min(90, endMs - startMs) ms.
/// glyphTop is the word's CJK glyph top in the layer, ascent the height of
/// that glyph above its baseline.
QList<Particle> spawn(qint64 lineStartMs, int wordIndex, const WordSpan &word,
                      double glyphTop, double ascent);

struct LineLayout
{
    // The line's identity. startMs is its first word's start: LyricSource
    // hands the renderer words, not the line's own timestamp.
    qint64 startMs = 0;
    QString text;
    // The CJK ascent at the line's font size.
    double ascent = 0;
    QList<WordSpan> words;
};

struct Snapshot
{
    qint64 startMs = 0;
    QString text;
    QList<Particle> particles;
    double lastBirthMs = 0;
    // What the line was following when it was the current one: the block's
    // slide and the marquee scroll, and the block's opacity. Frozen once the
    // line is detached.
    double offsetX = 0;
    double offsetY = 0;
    double opacity = 1;

    double aliveUntilMs() const { return lastBirthMs + kLifeMs; }
    bool sameLine(const Snapshot &other) const
    {
        return startMs == other.startMs && text == other.text;
    }
};

/// Every word's particles, with the line's identity. No particles at all
/// (every token zero-length) means nothing to keep.
Snapshot capture(const LineLayout &line);

/// The line being sung plus the recent lines whose particles are still in
/// the air. Kept by the layer itself rather than read back from the widget:
/// AnimatedLyric lets go of the previous line's words ~260 ms after a switch,
/// and a credit block changes line several times inside one particle's life.
class Field
{
public:
    /// The current line, re-captured whenever its layout changes. An empty
    /// snapshot means the current line has no particles. A kept copy of the
    /// same line goes: the current line's snapshot replaces an older one.
    void setLive(const Snapshot &live);
    void setLiveFollow(double offsetX, double offsetY, double opacity);
    /// The current line stops being current: a copy holding every particle
    /// of the words started by positionMs is kept, frozen where it was, in
    /// place of any older copy of the same line -- unless at positionMs it
    /// would already fail the condition prune() keeps lines by, as after a
    /// seek back before the line or past its last particle. A started word
    /// keeps all its particles, even those born within its 90 ms after the
    /// switch; a word not started yet spawns nothing from the copy. So the
    /// last of it goes out 90 + 2050 ms after positionMs at the latest. The
    /// current line itself is left alone until
    /// the next setLive() or dedupe(), which is where a line switch that
    /// turns out to land on the same line again (only the second line
    /// changed) drops that copy, the line going on with all its words.
    /// Deduplicating here instead would drop every copy, because the new
    /// line has not arrived yet.
    void detach(double positionMs);
    /// A new track: every kept line goes, and every particle of the current
    /// line born at or before positionMs, now and whenever the same line is
    /// captured again. The words of that line still to come spawn as
    /// usual -- a new track that happens to carry the same line goes on
    /// singing it. A different current line ends the drop.
    void dropAll(double positionMs);
    /// Drops what is never going to be visible again at positionMs: a kept
    /// line that has not started yet, or whose last particle has gone out.
    /// Pure function of the position, so a seek needs nothing more for the
    /// lines held. Also drops a kept copy of the current line, like
    /// dedupe().
    void prune(double positionMs);
    /// Drops a kept copy of the current line: for a line switch that landed
    /// on the same line again without its layout changing at all.
    void dedupe();

    /// When the last particle of anything kept goes out, or -infinity.
    double aliveUntilMs() const;
    /// Every snapshot to draw, the current line last.
    QList<const Snapshot *> snapshots() const;
    /// The current line's snapshot among those, or null when it has none.
    const Snapshot *live() const;

private:
    void retain(double positionMs);
    void applyDrop();

    Snapshot m_live;
    QList<Snapshot> m_kept;
    // The current line when dropAll() ran, and the position it ran at.
    bool m_dropping = false;
    Snapshot m_dropped;
    double m_droppedThroughMs = 0;
};

struct Sprite
{
    double x = 0;
    double y = 0;
    double coreRadius = 0;
    double haloRadius = 0;
    double brightness = 0;
};

/// Where and how bright the particle is at positionMs, following
/// (offsetX, offsetY) and dimmed by opacity. False when it is not alive.
bool evaluate(const Particle &particle, double positionMs, double scale,
              double offsetX, double offsetY, double opacity, Sprite *sprite);

/// Relative luminance of the sRGB components as they are, no linearisation:
/// 0.2126R + 0.7152G + 0.0722B >= 0.5 is additive, anything darker is
/// normal. A dark particle added onto a light wallpaper would not show at all.
bool isAdditive(double red, double green, double blue);

// Laid out like QSGGeometry::ColoredPoint2D, which WordParticleLayer checks.
struct Vertex
{
    float x;
    float y;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

/// A premultiplied vertex colour. The scene graph always blends with
/// One, OneMinusSrcAlpha, so alpha 0 adds the colour onto what is under it
/// and alpha = amount is ordinary over-blending, both from one node.
Vertex colourVertex(double x, double y, double red, double green, double blue,
                    double amount, bool additive);

constexpr int kHaloSegments = 16;
constexpr int kCoreSegments = 12;
constexpr int kVerticesPerSprite = (1 + kHaloSegments) + (1 + 2 * kCoreSegments);
constexpr int kIndicesPerSprite = 3 * kHaloSegments + 9 * kCoreSegments;

/// Writes one particle's kVerticesPerSprite vertices and kIndicesPerSprite
/// indices, its first vertex being number firstVertex. Triangle fans written
/// out as a triangle list, so any number of particles share one draw: the
/// halo, colour × 0.55 at the centre fading linearly to nothing at 3.5 s, and
/// over it the core, solid out to s / 2 and then feathered to nothing across
/// `feather` so that a 1 px particle does not flicker as it moves.
void writeSprite(const Sprite &sprite, double red, double green, double blue, bool additive,
                 double feather, Vertex *vertices, quint32 firstVertex, quint32 *indices);

} // namespace WordParticles
