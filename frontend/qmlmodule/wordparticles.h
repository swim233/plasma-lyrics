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
// says otherwise. When evaluated, the motion scales by x = fontSize / 34 and
// the particle's size by g(x) (sizeScale), which follows x up to 34 px and
// grows ever slower above it. Birth positions and ψ are not scaled: they are
// measured on the glyphs as drawn.
//
// There are two looks, chosen by LyricsView.panelMode: the panel's light
// dot, in a panel's compact representation, and the desktop's star, in the
// full one -- the desktop widget, and a panel widget's popup. Everything
// below is the dot's unless it says star; the star keeps the dot's births,
// shared current and colour and changes only what its kStar* constants and
// functions name.
namespace WordParticles {

enum class Look {
    // A Gaussian core in a wide halo, 1-4 px.
    PanelDot,
    // A small core with a cross of two rays, one star in ten twinkling once.
    DesktopStar,
};

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
constexpr double kSizeHeadroom = 0.5; // g(x) never reaches 1 + this
constexpr double kHaloRadius = 3.5; // R, times s
constexpr double kHaloStrength = 0.8; // h
constexpr double kCoreMinWidth = 1.5; // device pixels, at half the core's height
constexpr double kHalfWidthPerSigma = 2.355; // a Gaussian's width at half height, in σ

constexpr double kStarLifeMs = 1800;
constexpr double kStarFadeInMs = 30;
constexpr double kStarFadeOutFromMs = 1300;
constexpr double kStarMinSize = 1.4; // s = 1.4 × 2.5^r, so s is in [1.4, 3.5)
constexpr double kStarSizeRange = 2.5;
constexpr double kStarBirthLift = 0.063; // of the CJK ascent: the dot's band, moved up
constexpr double kStarRiseHeight = 30;
constexpr double kStarSway = 1.2;
constexpr double kStarHaloRadius = 2.2; // R, times s ...
constexpr double kStarHaloSigmas = 3; // ... or 3σ, whichever is larger
constexpr double kStarHaloStrength = 0.2; // h
constexpr double kTwinkleOdds = 0.1; // q under this twinkles
constexpr double kTwinkleFromMs = 350; // t_f = 350 + 850τ
constexpr double kTwinkleSpreadMs = 850;
constexpr double kTwinkleMs = 260;
constexpr double kTwinklePeak = 0.5;
constexpr double kStarAngle = 0.35; // θ0 in [-0.35, 0.35) rad
constexpr double kStarSpin = 0.4; // ω in [-0.4, 0.4) rad/s
constexpr double kVerticalRay = 0.8; // of the horizontal ray's length
constexpr double kRayHalfWidth = 0.6; // at the centre, times s

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

    Look look = Look::PanelDot;
    // The star's own, drawn after all of the above. Its k comes from the
    // draw its size did, as the dot's would: (4^r - 1) / 3.
    double starDepth = 0;
    double twinkleDraw = 1; // q, in [0, 1): the star twinkles when under 0.1
    double twinkleFromMs = 0; // t_f, in [350, 1200), of its age
    double angle = 0; // θ0, in [-0.35, 0.35) rad
    double spin = 0; // ω, in [-0.4, 0.4) rad/s
};

/// k = (s - 1) / 3: 0 for the smallest particle, 1 for the largest.
double depth(double size);
/// The particle's k: depth(s) for the dot, the star's own starDepth.
double depth(const Particle &particle);

/// How long a particle of this look lives: 2050 ms, 1800 for the star.
double lifeMs(Look look);
/// b(t) before the depth factor: 0 -> 1 over the first 50 ms, held until
/// 1550 ms, back to 0 at 2050 ms, and 0 outside [0, 2050]; the star's is
/// 30, 1300 and 1800 ms.
double envelope(double ageMs, Look look = Look::PanelDot);
/// b(t) × (0.45 + 0.55k), the star's × (0.55 + 0.45k).
double brightness(const Particle &particle, double ageMs);
/// How far the particle has risen, upward positive, before scaling: the
/// star's H is 30 px, over its own life.
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
/// f(t), how far the star is into its one twinkle: 0.5 sin²(π (t - t_f) /
/// 260) within [t_f, t_f + 260] for a star whose q is under 0.1, and 0 for
/// any other star, at any other age and for the dot.
double twinkle(const Particle &particle, double ageMs);

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
/// that glyph above its baseline. Stars come 1 to 4, from the same draws in
/// the same order and four more each after them, and are born 0.063 ×
/// ascent higher.
QList<Particle> spawn(qint64 lineStartMs, int wordIndex, const WordSpan &word,
                      double glyphTop, double ascent, Look look = Look::PanelDot);

struct LineLayout
{
    // The line's identity. startMs is its first word's start: LyricSource
    // hands the renderer words, not the line's own timestamp.
    qint64 startMs = 0;
    QString text;
    // The CJK ascent at the line's font size.
    double ascent = 0;
    QList<WordSpan> words;
    Look look = Look::PanelDot;
};

struct Snapshot
{
    qint64 startMs = 0;
    QString text;
    QList<Particle> particles;
    double lastBirthMs = 0;
    // Every particle's, and with it how long the last one lives.
    Look look = Look::PanelDot;
    // What the line was following when it was the current one: the block's
    // slide and the marquee scroll, and the block's opacity. Frozen once the
    // line is detached.
    double offsetX = 0;
    double offsetY = 0;
    double opacity = 1;

    double aliveUntilMs() const { return lastBirthMs + lifeMs(look); }
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
/// AnimatedLyric lets go of a line's words as soon as its block has left --
/// 270 ms after the switch for a sung line pushed up, 180 ms for one faded
/// out in place, at once with animations off -- and a credit block changes
/// line several times inside one particle's life.
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
    /// last of it goes out 90 + 2050 ms after positionMs at the latest, 90 +
    /// 1800 for stars. The current line itself is left alone until
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

/// g(x), what the particle's size is scaled by at x = fontSize / 34: x itself
/// up to 1, then 1 + 0.5 (1 - e^(-(x - 1) / 0.5)), which leaves x = 1 with
/// slope 1 and never reaches 1.5.
double sizeScale(double scale);

struct Sprite
{
    double x = 0;
    double y = 0;
    // s after g(x), in logical pixels.
    double size = 0;
    double brightness = 0;

    Look look = Look::PanelDot;
    // The star's k, f(t) and the angle of its horizontal ray, θ0 + ω t.
    double depth = 0;
    double twinkle = 0;
    double angle = 0;
};

/// Where, how large and how bright the particle is at positionMs, following
/// (offsetX, offsetY) and dimmed by opacity. False when it is not alive.
bool evaluate(const Particle &particle, double positionMs, double scale,
              double offsetX, double offsetY, double opacity, Sprite *sprite);

/// σ of a sprite of this size: the Gaussian core is s wide at half its
/// height, and never narrower than 1.5 device pixels, devicePixel being one
/// of them in logical pixels -- a narrower one flickers as it moves.
double coreSigma(double size, double devicePixel);

/// p(r), the sprite's brightness at r from its centre, radius being R:
/// (e^(-r² / 2σ²) + h (1 - q²)²) / (1 + h), q = r / R, and 0 from R on.
/// 1 at the centre and never rising outwards. At R the halo term reaches 0
/// with slope 0 and the Gaussian is e^-34 at the full σ; only a sprite whose
/// σ is held at its floor keeps a little of it there, which p cuts to 0.
/// The star's core passes its own h of 0.2.
double profile(double r, double radius, double sigma, double haloStrength = kHaloStrength);

/// The star core's R: 2.2 s, or 3σ where σ's floor makes that larger --
/// 2.2 s would leave the floored Gaussian a step at R, 3σ leaves about 1%.
double starHaloRadius(double size, double devicePixel);
/// L, the star's horizontal ray from its centre to either tip:
/// s (3.2 + 2.2k)(0.3 + 0.7f). The vertical one is 0.8 L.
double rayLength(double size, double depth, double twinkle);
/// A ray's brightness on its axis at u, the distance from the centre over
/// the ray's length: 1, 0.32, 0.08 and 0 at |u| = 0, 0.24, 0.56 and 1,
/// linear in between, and 0 past the tip.
double rayBrightness(double u);
/// W(u), how far a ray's cross-section reaches either side of its axis:
/// 0.6 s (1 - |u|), never under one device pixel.
double rayHalfWidth(double size, double u, double devicePixel);

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

constexpr int kRings = 10;
constexpr int kRingSegments = 24;
constexpr double kRingSpacing = 1.7; // ring i at R (i / 10)^1.7
constexpr int kVerticesPerSprite = 1 + kRings * kRingSegments;
constexpr int kIndicesPerSprite = 3 * kRingSegments + 6 * (kRings - 1) * kRingSegments;

/// Writes one particle's kVerticesPerSprite vertices and kIndicesPerSprite
/// indices, its first vertex being number firstVertex. A triangle list, so
/// any number of particles share one draw: a centre vertex and ten rings
/// around it, ring i (1 to 10) at R (i / 10)^1.7, closer together towards
/// the centre, across the Gaussian core. Every vertex carries p at its radius
/// times the colour and the brightness, linearly interpolated from ring to
/// ring; the outermost ring is R itself, where p is 0. devicePixel is one
/// device pixel in logical pixels, for σ's floor.
void writeSprite(const Sprite &sprite, double red, double green, double blue, bool additive,
                 double devicePixel, Vertex *vertices, quint32 firstVertex, quint32 *indices);

constexpr int kRaySections = 7; // at u = -1, -0.56, -0.24, 0, 0.24, 0.56, 1
constexpr int kVerticesPerRay = 3 * kRaySections;
constexpr int kIndicesPerRay = 4 * 3 * (kRaySections - 1);
constexpr int kVerticesPerStar = kVerticesPerSprite + 2 * kVerticesPerRay;
constexpr int kIndicesPerStar = kIndicesPerSprite + 2 * kIndicesPerRay;

/// Writes one star's kVerticesPerStar vertices and kIndicesPerStar indices,
/// like writeSprite: first its core, the dot's rings at R =
/// starHaloRadius(), with h 0.2 and times 0.7 + 0.3f; then the horizontal
/// ray, at the sprite's angle, and the vertical one, 0.8 times as long, so
/// that blended normally the rays lie over the core. Each ray is seven
/// cross-sections of three vertices -- on the axis rayBrightness(u) times
/// the colour, the brightness and 0.15 + 0.85f, and at rayHalfWidth(u) on
/// either side 0 -- with four triangles between neighbours: a ridge, not a
/// solid diamond, whose edges the scene graph would leave unsmoothed.
void writeStar(const Sprite &sprite, double red, double green, double blue, bool additive,
               double devicePixel, Vertex *vertices, quint32 firstVertex, quint32 *indices);

/// What writeSprite or writeStar takes for one particle of this look.
int vertexCount(Look look);
int indexCount(Look look);

} // namespace WordParticles
