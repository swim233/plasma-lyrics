#include "wordparticles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace WordParticles {

namespace {

constexpr double kTwoPi = 2 * std::numbers::pi;

template<int N>
std::array<double, 2 * N> unitCircle()
{
    std::array<double, 2 * N> out{};
    for (int i = 0; i < N; ++i) {
        const double angle = kTwoPi * i / N;
        out[2 * i] = std::cos(angle);
        out[2 * i + 1] = std::sin(angle);
    }
    return out;
}

// Ring i + 1's radius over R.
std::array<double, kRings> ringFractions()
{
    std::array<double, kRings> out{};
    for (int i = 0; i < kRings; ++i) {
        out[i] = std::pow(static_cast<double>(i + 1) / kRings, kRingSpacing);
    }
    return out;
}

// One sprite's triangles, its centre being vertex 0: the fan around the
// centre, then each band between two neighbouring rings as one quad per
// segment, every quad cut along the same diagonal.
std::array<quint32, kIndicesPerSprite> spriteIndices()
{
    const auto at = [](int ring, int segment) {
        return static_cast<quint32>(1 + ring * kRingSegments + segment % kRingSegments);
    };
    std::array<quint32, kIndicesPerSprite> out{};
    quint32 *index = out.data();
    for (int i = 0; i < kRingSegments; ++i) {
        for (quint32 vertex : {0u, at(0, i), at(0, i + 1)}) {
            *index++ = vertex;
        }
    }
    for (int ring = 0; ring + 1 < kRings; ++ring) {
        for (int i = 0; i < kRingSegments; ++i) {
            const quint32 inner = at(ring, i);
            const quint32 innerNext = at(ring, i + 1);
            const quint32 outer = at(ring + 1, i);
            const quint32 outerNext = at(ring + 1, i + 1);
            for (quint32 vertex : {inner, outer, outerNext, inner, outerNext, innerNext}) {
                *index++ = vertex;
            }
        }
    }
    return out;
}

unsigned char toByte(double value)
{
    return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}

// The factor every length of one particle is scaled by before its own
// fontSize scaling: the smaller (dimmer) ones also rise and sway less.
double depthScale(const Particle &particle)
{
    return 0.6 + 0.4 * depth(particle.size);
}

} // namespace

Random::Random(quint64 seed)
    : m_state(seed)
{
}

quint64 Random::next()
{
    m_state += 0x9E3779B97F4A7C15ULL;
    quint64 z = m_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double Random::unit()
{
    return static_cast<double>(next() >> 11) * 0x1.0p-53;
}

quint64 seedFor(qint64 lineStartMs, int wordIndex)
{
    Random line(static_cast<quint64>(lineStartMs));
    return line.next() ^ (static_cast<quint64>(static_cast<quint32>(wordIndex)) * 0xD1B54A32D192ED03ULL);
}

double depth(double size)
{
    return (size - 1) / 3;
}

double envelope(double ageMs)
{
    if (ageMs <= 0 || ageMs >= kLifeMs) {
        return 0;
    }
    if (ageMs < kFadeInMs) {
        return ageMs / kFadeInMs;
    }
    if (ageMs <= kFadeOutFromMs) {
        return 1;
    }
    return (kLifeMs - ageMs) / (kLifeMs - kFadeOutFromMs);
}

double brightness(const Particle &particle, double ageMs)
{
    return envelope(ageMs) * (0.45 + 0.55 * depth(particle.size));
}

double rise(const Particle &particle, double ageMs)
{
    const double u = std::clamp(ageMs / kLifeMs, 0.0, 1.0);
    return kRiseHeight * depthScale(particle) * (1 + 0.25 * particle.jitter) * (1 - (1 - u) * (1 - u));
}

double swayRamp(double ageMs)
{
    const double x = std::clamp(ageMs / kSwayRampMs, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

double wander(const Particle &particle, double ageMs)
{
    const double a = kSway * depthScale(particle);
    const double turn = kTwoPi * ageMs / particle.periodMs;
    return swayRamp(ageMs) * a * particle.amplitude * 2
        * (0.62 * std::sin(turn + particle.phase1)
           + 0.28 * std::sin(1.73 * turn + particle.phase2)
           + 0.10 * std::sin(2.9 * turn + particle.phase3));
}

double wind(const Particle &particle, double ageMs)
{
    const double a = kSway * depthScale(particle);
    const double omega = kTwoPi / kWindPeriodMs;
    const double psi = kWindPhasePerPixel * particle.x;
    return 1.2 * a * (std::cos(omega * particle.birthMs + psi) - std::cos(omega * (particle.birthMs + ageMs) + psi));
}

double drift(const Particle &particle, double ageMs)
{
    return wind(particle, ageMs) + 0.5 * wander(particle, ageMs);
}

double fall(const Particle &particle, double ageMs)
{
    const double a = kSway * depthScale(particle);
    const double bob = swayRamp(ageMs) * a * 0.35
        * std::sin(kTwoPi * ageMs / (0.8 * particle.periodMs) + particle.phase2);
    return bob - rise(particle, ageMs);
}

QList<Particle> spawn(qint64 lineStartMs, int wordIndex, const WordSpan &word,
                      double glyphTop, double ascent)
{
    QList<Particle> out;
    if (word.endMs <= word.startMs) {
        return out;
    }
    Random random(seedFor(lineStartMs, wordIndex));
    const int count = 1 + std::min(2, static_cast<int>(random.unit() * 3));
    const double spreadMs = std::min(kBirthSpreadMs, static_cast<double>(word.endMs - word.startMs));
    out.reserve(count);
    // One fixed order of draws per particle: reordering these reshuffles
    // every particle of every line.
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.wordStartMs = static_cast<double>(word.startMs);
        p.birthMs = word.startMs + random.unit() * spreadMs;
        p.size = std::pow(kMaxSize, random.unit());
        p.x = word.left + word.width * (kInkFrom + (kInkTo - kInkFrom) * random.unit());
        p.y = glyphTop + ascent * kBirthDepth * random.unit();
        p.jitter = 2 * random.unit() - 1;
        p.amplitude = 0.6 + 0.8 * random.unit();
        p.periodMs = 1400 + 800 * random.unit();
        p.phase1 = kTwoPi * random.unit();
        p.phase2 = kTwoPi * random.unit();
        p.phase3 = kTwoPi * random.unit();
        out.append(p);
    }
    return out;
}

Snapshot capture(const LineLayout &line)
{
    Snapshot snapshot;
    snapshot.startMs = line.startMs;
    snapshot.text = line.text;
    snapshot.lastBirthMs = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < line.words.size(); ++i) {
        const WordSpan &word = line.words.at(i);
        const QList<Particle> born = spawn(line.startMs, i, word, word.top, line.ascent);
        for (const Particle &p : born) {
            snapshot.lastBirthMs = std::max(snapshot.lastBirthMs, p.birthMs);
        }
        snapshot.particles.append(born);
    }
    return snapshot;
}

void Field::setLive(const Snapshot &live)
{
    const double offsetX = m_live.offsetX;
    const double offsetY = m_live.offsetY;
    const double opacity = m_live.opacity;
    m_live = live;
    m_live.offsetX = offsetX;
    m_live.offsetY = offsetY;
    m_live.opacity = opacity;
    if (m_dropping && !m_live.sameLine(m_dropped)) {
        m_dropping = false;
    }
    applyDrop();
    dedupe();
}

void Field::setLiveFollow(double offsetX, double offsetY, double opacity)
{
    m_live.offsetX = offsetX;
    m_live.offsetY = offsetY;
    m_live.opacity = opacity;
}

void Field::detach(double positionMs)
{
    // Only the words started by now, each with all of its particles. Lines
    // overlap -- duets and backing vocals in AMLL, some QRC -- and the next
    // one takes over the moment it starts, so a line can be switched away
    // from with words still to come; born later, those would rise from where
    // the line was, over whatever the slot shows by then. A word that has
    // just started is being sung, though, and keeps the particles its 90 ms
    // birth window still holds: most often it is a QQ line's held last
    // note, whose line ends before the word does.
    Snapshot kept = m_live;
    kept.particles.removeIf([positionMs](const Particle &p) { return p.wordStartMs > positionMs; });
    if (!kept.particles.isEmpty()) {
        kept.lastBirthMs = -std::numeric_limits<double>::infinity();
        for (const Particle &p : std::as_const(kept.particles)) {
            kept.lastBirthMs = std::max(kept.lastBirthMs, p.birthMs);
        }
        m_kept.removeIf([this](const Snapshot &other) { return other.sameLine(m_live); });
        m_kept.append(kept);
    }
    retain(positionMs);
}

void Field::dropAll(double positionMs)
{
    m_kept.clear();
    m_dropping = true;
    m_dropped.startMs = m_live.startMs;
    m_dropped.text = m_live.text;
    m_droppedThroughMs = positionMs;
    applyDrop();
}

void Field::applyDrop()
{
    if (!m_dropping) {
        return;
    }
    // The earliest births go, so whenever anything is left the last birth,
    // and with it aliveUntilMs(), stands as it was.
    const double through = m_droppedThroughMs;
    m_live.particles.removeIf([through](const Particle &p) { return p.birthMs <= through; });
}

void Field::prune(double positionMs)
{
    retain(positionMs);
    dedupe();
}

void Field::retain(double positionMs)
{
    m_kept.removeIf([positionMs](const Snapshot &kept) {
        return positionMs < kept.startMs || positionMs >= kept.aliveUntilMs();
    });
}

void Field::dedupe()
{
    if (!live()) {
        return;
    }
    m_kept.removeIf([this](const Snapshot &kept) { return kept.sameLine(m_live); });
}

double Field::aliveUntilMs() const
{
    double until = -std::numeric_limits<double>::infinity();
    for (const Snapshot *snapshot : snapshots()) {
        until = std::max(until, snapshot->aliveUntilMs());
    }
    return until;
}

QList<const Snapshot *> Field::snapshots() const
{
    QList<const Snapshot *> out;
    out.reserve(m_kept.size() + 1);
    for (const Snapshot &kept : m_kept) {
        out.append(&kept);
    }
    if (const Snapshot *current = live()) {
        out.append(current);
    }
    return out;
}

const Snapshot *Field::live() const
{
    return m_live.particles.isEmpty() ? nullptr : &m_live;
}

bool evaluate(const Particle &particle, double positionMs, double scale,
              double offsetX, double offsetY, double opacity, Sprite *sprite)
{
    const double ageMs = positionMs - particle.birthMs;
    const double amount = brightness(particle, ageMs) * opacity;
    if (amount <= 0) {
        return false;
    }
    sprite->x = particle.x + offsetX + scale * drift(particle, ageMs);
    sprite->y = particle.y + offsetY + scale * fall(particle, ageMs);
    sprite->size = sizeScale(scale) * particle.size;
    sprite->brightness = amount;
    return true;
}

double sizeScale(double scale)
{
    if (scale <= 1) {
        return scale;
    }
    // The same 0.5 as the headroom and the e-folding, so that the slope at
    // 34 px is 1 and the curve leaves the straight line without a kink.
    return 1 + kSizeHeadroom * (1 - std::exp(-(scale - 1) / kSizeHeadroom));
}

double coreSigma(double size, double devicePixel)
{
    return std::max(size, kCoreMinWidth * devicePixel) / kHalfWidthPerSigma;
}

double profile(double r, double radius, double sigma)
{
    if (r >= radius) {
        return 0;
    }
    const double q = r / radius;
    const double halo = (1 - q * q) * (1 - q * q);
    return (std::exp(-r * r / (2 * sigma * sigma)) + kHaloStrength * halo) / (1 + kHaloStrength);
}

bool isAdditive(double red, double green, double blue)
{
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue >= 0.5;
}

Vertex colourVertex(double x, double y, double red, double green, double blue,
                    double amount, bool additive)
{
    return Vertex{static_cast<float>(x), static_cast<float>(y),
                  toByte(red * amount), toByte(green * amount), toByte(blue * amount),
                  additive ? static_cast<unsigned char>(0) : toByte(amount)};
}

void writeSprite(const Sprite &sprite, double red, double green, double blue, bool additive,
                 double devicePixel, Vertex *vertices, quint32 firstVertex, quint32 *indices)
{
    static const auto circle = unitCircle<kRingSegments>();
    static const auto rings = ringFractions();
    static const auto pattern = spriteIndices();

    const double radius = kHaloRadius * sprite.size;
    const double sigma = coreSigma(sprite.size, devicePixel);
    *vertices++ = colourVertex(sprite.x, sprite.y, red, green, blue, sprite.brightness, additive);
    for (int ring = 0; ring < kRings; ++ring) {
        // The last fraction is exactly 1, so the outermost ring is at R and
        // p gives it 0.
        const double r = radius * rings[ring];
        const Vertex shade = colourVertex(0, 0, red, green, blue,
                                          profile(r, radius, sigma) * sprite.brightness, additive);
        for (int i = 0; i < kRingSegments; ++i) {
            Vertex vertex = shade;
            vertex.x = static_cast<float>(sprite.x + r * circle[2 * i]);
            vertex.y = static_cast<float>(sprite.y + r * circle[2 * i + 1]);
            *vertices++ = vertex;
        }
    }
    for (int i = 0; i < kIndicesPerSprite; ++i) {
        indices[i] = firstVertex + pattern[i];
    }
}

} // namespace WordParticles
