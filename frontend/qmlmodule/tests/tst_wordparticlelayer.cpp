#include "frontend/qmlmodule/wordparticlelayer.h"

#include <QFontMetricsF>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QTest>

#include <cmath>
#include <limits>

// DESIGN.md decision 77: WordParticleLayer's own half, without GL. The QML
// suite runs the software backend, which never draws the layer's node, so
// the node's life and contents are pinned here by calling updatePaintNode()
// directly, the way the scene graph would. Only relations are asserted, never
// glyph sizes: the CI container has hardly any fonts.
namespace {

class Layer : public WordParticleLayer
{
public:
    using WordParticleLayer::WordParticleLayer;
    using WordParticleLayer::updatePaintNode;

    // What the scene graph does with the returned node: keeps it for the
    // next call, whatever it is.
    QSGGeometryNode *paint()
    {
        m_node = static_cast<QSGGeometryNode *>(updatePaintNode(m_node, nullptr));
        return m_node;
    }
    ~Layer() override { delete m_node; }

private:
    QSGGeometryNode *m_node = nullptr;
};

QVariantMap word(qint64 startMs, qint64 endMs, const QString &text, double x, double baseline = 40)
{
    return {{QStringLiteral("startMs"), startMs}, {QStringLiteral("endMs"), endMs},
            {QStringLiteral("text"), text}, {QStringLiteral("x"), x}, {QStringLiteral("baseline"), baseline}};
}

QFont lineFont()
{
    QFont font;
    font.setPixelSize(34);
    return font;
}

QVariantMap line(qint64 startMs, const QVariantList &words)
{
    return {{QStringLiteral("startMs"), startMs}, {QStringLiteral("text"), QStringLiteral("line")},
            {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
            {QStringLiteral("font"), lineFont()}, {QStringLiteral("words"), words}};
}

// Two words at 1000 and 1400; the last particle goes out before 1490 + 2050.
QVariantMap twoWords()
{
    return line(1000, {word(1000, 1400, QStringLiteral("ab"), 0), word(1400, 1800, QStringLiteral("cd"), 60)});
}

void setUp(Layer &layer, const QVariant &value, double positionMs)
{
    layer.setActive(true);
    layer.setFontSize(34);
    layer.setColor(QColor(QStringLiteral("#fffaf5")));
    layer.setLine(value);
    layer.setPositionMs(positionMs);
}

double distance(const QSGGeometry::ColoredPoint2D &a, const QSGGeometry::ColoredPoint2D &b)
{
    return std::hypot(double(a.x) - b.x, double(a.y) - b.y);
}

// How far the sprite whose centre is vertex `centre` is, on any ring but the
// outermost, from carrying p with its core held at `floor` logical pixels,
// in colour steps. Its size and each ring's radius are read back from the
// vertices; red only: #fffaf5's red is 1, so the centre's red is b and a
// ring's b × p.
double ringError(const QSGGeometry::ColoredPoint2D *v, int centre, double floor)
{
    const int rings = WordParticles::kRings;
    const int segments = WordParticles::kRingSegments;
    const double radius = distance(v[centre], v[centre + 1 + (rings - 1) * segments]);
    const double sigma = std::max(radius / 3.5, floor) / 2.355;
    double worst = 0;
    for (int ring = 1; ring < rings; ++ring) {
        const QSGGeometry::ColoredPoint2D &vertex = v[centre + 1 + (ring - 1) * segments];
        const double r = distance(v[centre], vertex);
        const double q = r / radius;
        const double p = (std::exp(-r * r / (2 * sigma * sigma)) + 0.8 * (1 - q * q) * (1 - q * q)) / 1.8;
        worst = std::max(worst, std::abs(vertex.r - v[centre].r * p));
    }
    return worst;
}

QList<QPointF> births(const Layer &layer)
{
    QList<QPointF> out;
    for (const QVariant &snapshot : layer.describeSnapshots()) {
        if (snapshot.toMap().value(QStringLiteral("current")).toBool()) {
            for (const QVariant &birth : snapshot.toMap().value(QStringLiteral("births")).toList()) {
                out.append(birth.toPointF());
            }
        }
    }
    return out;
}

} // namespace

class WordParticleLayerTest : public QObject
{
    Q_OBJECT

public:
    // Before the application exists: every window is then of ratio 2, which
    // the offscreen platform honours too. A layer with no window, as in all
    // but one case, still draws at ratio 1.
    static void initMain() { qputenv("QT_SCALE_FACTOR", "2"); }

private Q_SLOTS:
    void nothingToDrawIsNoNode()
    {
        Layer layer;
        layer.setActive(true);
        QCOMPARE(layer.paint(), nullptr);
        layer.setLine(twoWords());
        layer.setPositionMs(900); // before any birth
        QCOMPARE(layer.snapshotCount(), 1);
        QCOMPARE(layer.paint(), nullptr);
    }

    // qa-1's finding, without GL: a node left in the scene with no vertices
    // is never drawn again once it fills up, so the layer must never hand the
    // scene graph one. The node goes when nothing is visible and a fresh one
    // comes with the next visible particle.
    void aNodeLivesOnlyWhileSomethingIsVisible()
    {
        Layer layer;
        setUp(layer, twoWords(), 1500);
        QSGGeometryNode *node = layer.paint();
        QVERIFY(node);
        const int vertices = node->geometry()->vertexCount();
        QVERIFY(vertices > 0);
        QCOMPARE(vertices % WordParticles::kVerticesPerSprite, 0);
        QCOMPARE(node->geometry()->indexCount(),
                 vertices / WordParticles::kVerticesPerSprite * WordParticles::kIndicesPerSprite);
        QCOMPARE(node->geometry()->indexType(), int(QSGGeometry::UnsignedIntType));

        // A seek into the gap before the line: nothing visible, no node.
        layer.setPositionMs(900);
        QCOMPARE(layer.paint(), nullptr);
        // Back in flight: a new node with the same particles.
        layer.setPositionMs(1500);
        node = layer.paint();
        QVERIFY(node);
        QCOMPARE(node->geometry()->vertexCount(), vertices);

        // Off and on again mid-line, the other way qa-1 reached it.
        layer.setActive(false);
        QCOMPARE(layer.paint(), nullptr);
        layer.setActive(true);
        node = layer.paint();
        QVERIFY(node);
        QCOMPARE(node->geometry()->vertexCount(), vertices);

        // Played through: whatever is returned is never an empty node.
        for (double position = 0; position < 4000; position += 23) {
            layer.setPositionMs(position);
            node = layer.paint();
            QVERIFY2(!node || node->geometry()->vertexCount() > 0, qPrintable(QString::number(position)));
        }
        QCOMPARE(node, nullptr);
    }

    // Decision 77: every length is defined at 34 px. The motion scales with
    // x = fontSize / 34 and the size with g(x), which is x up to 34 px -- the
    // panel's 16 px draws at 16 / 34 -- and grows ever slower above it. The
    // birth points do not move, so each sprite's R, from its centre to its
    // outermost ring, halves exactly at 17 px and grows by g(2), not 2, at
    // 68 px.
    void sizesScaleWithTheFontSize()
    {
        Layer full;
        setUp(full, twoWords(), 1500);
        Layer half;
        setUp(half, twoWords(), 1500);
        half.setFontSize(17);
        Layer twice;
        setUp(twice, twoWords(), 1500);
        twice.setFontSize(68);
        const QSGGeometryNode *big = full.paint();
        const QSGGeometryNode *small = half.paint();
        const QSGGeometryNode *large = twice.paint();
        QVERIFY(big && small && large);
        QCOMPARE(small->geometry()->vertexCount(), big->geometry()->vertexCount());
        QCOMPARE(large->geometry()->vertexCount(), big->geometry()->vertexCount());
        const auto *a = big->geometry()->vertexDataAsColoredPoint2D();
        const auto *b = small->geometry()->vertexDataAsColoredPoint2D();
        const auto *c = large->geometry()->vertexDataAsColoredPoint2D();
        const double grown = 1 + 0.5 * (1 - std::exp(-2.0));
        for (int sprite = 0; sprite < big->geometry()->vertexCount() / WordParticles::kVerticesPerSprite; ++sprite) {
            const int centre = sprite * WordParticles::kVerticesPerSprite;
            const int rim = centre + 1 + 9 * WordParticles::kRingSegments;
            const double haloBig = distance(a[centre], a[rim]);
            const double haloSmall = distance(b[centre], b[rim]);
            const double haloLarge = distance(c[centre], c[rim]);
            QVERIFY(haloBig > 0);
            QVERIFY2(std::abs(haloSmall / haloBig - 0.5) < 1e-4, qPrintable(QString::number(haloSmall / haloBig)));
            QVERIFY2(std::abs(haloLarge / haloBig - grown) < 1e-4, qPrintable(QString::number(haloLarge / haloBig)));
            // 3.5 s, s being in [1, 4) at 34 px.
            QVERIFY(haloBig >= 3.5 - 1e-3 && haloBig < 14);
        }
    }

    // The core's σ has a floor of 1.5 device pixels, and without a window
    // the ratio is 1: at 17 px most sprites are under 1.5 px, and their rings
    // carry the Gaussian of a 1.5 px core.
    void aSmallCoreIsHeldAtOneAndAHalfDevicePixels()
    {
        Layer layer;
        setUp(layer, twoWords(), 1500);
        layer.setFontSize(17);
        const QSGGeometryNode *node = layer.paint();
        QVERIFY(node);
        const auto *v = node->geometry()->vertexDataAsColoredPoint2D();
        int floored = 0;
        for (int sprite = 0; sprite < node->geometry()->vertexCount() / WordParticles::kVerticesPerSprite; ++sprite) {
            const int centre = sprite * WordParticles::kVerticesPerSprite;
            const double size = distance(v[centre], v[centre + 1 + 9 * WordParticles::kRingSegments]) / 3.5;
            QVERIFY2(ringError(v, centre, 1.5) <= 1.01, qPrintable(QStringLiteral("size %1").arg(size)));
            // Well under the floor and bright enough to tell: neither the
            // bare core nor a ratio-2 floor would give this.
            if (size < 1 && v[centre].r > 40) {
                ++floored;
                QVERIFY(ringError(v, centre, 0) > 2);
                QVERIFY(ringError(v, centre, 0.75) > 2);
            }
        }
        QVERIFY(floored > 0);
    }

    // In a window the floor is 1.5 of its device pixels: at ratio 2 (see
    // initMain()), 0.75 logical pixels -- neither the ratio-1 floor nor one
    // of 1.5 times the ratio.
    void aWindowsRatioSetsTheCoresFloor()
    {
        QQuickWindow window;
        QCOMPARE(window.effectiveDevicePixelRatio(), 2.0);
        Layer layer(window.contentItem());
        setUp(layer, twoWords(), 1500);
        layer.setFontSize(17);
        const QSGGeometryNode *node = layer.paint();
        QVERIFY(node);
        const auto *v = node->geometry()->vertexDataAsColoredPoint2D();
        int floored = 0;
        for (int sprite = 0; sprite < node->geometry()->vertexCount() / WordParticles::kVerticesPerSprite; ++sprite) {
            const int centre = sprite * WordParticles::kVerticesPerSprite;
            const double size = distance(v[centre], v[centre + 1 + 9 * WordParticles::kRingSegments]) / 3.5;
            QVERIFY2(ringError(v, centre, 0.75) <= 1.01, qPrintable(QStringLiteral("size %1").arg(size)));
            if (size < 1 && v[centre].r > 40) {
                ++floored;
                QVERIFY(ringError(v, centre, 1.5) > 2);
                QVERIFY(ringError(v, centre, 3) > 2);
            }
        }
        QVERIFY(floored > 0);
    }

    // The item's own choice of blending, from its colour.
    void aLightColourAddsAndADarkOneBlends()
    {
        Layer layer;
        setUp(layer, twoWords(), 1500);
        const QSGGeometryNode *node = layer.paint();
        QVERIFY(node);
        const auto *v = node->geometry()->vertexDataAsColoredPoint2D();
        bool anyColour = false;
        for (int i = 0; i < node->geometry()->vertexCount(); ++i) {
            QCOMPARE(int(v[i].a), 0);
            anyColour = anyColour || v[i].r > 0;
        }
        QVERIFY(anyColour);

        layer.setColor(QColor(QStringLiteral("#1f1b16")));
        node = layer.paint();
        QVERIFY(node);
        v = node->geometry()->vertexDataAsColoredPoint2D();
        const int middle = 1 + 4 * WordParticles::kRingSegments;
        const int rim = 1 + 9 * WordParticles::kRingSegments;
        QVERIFY(v[0].a > 0);
        QVERIFY(v[middle].a > 0);
        QVERIFY(v[middle].a < v[0].a);
        QCOMPARE(int(v[rim].a), 0);
        // Premultiplied: no channel above alpha.
        QVERIFY(v[0].r <= v[0].a);
        QVERIFY(v[middle].r <= v[middle].a);
    }

    // Measured once per line, from the words' own text: the ink leaves out
    // trailing whitespace, and a new line with as many words as the last one
    // is measured again.
    void eachLineIsMeasuredFromItsOwnWords()
    {
        const QFontMetricsF metrics(lineFont());
        Layer layer;
        setUp(layer, line(1000, {word(1000, 1400, QStringLiteral("i "), 0), word(1400, 1800, QStringLiteral("i"), 60)}), 1500);
        QList<QVariant> words = layer.describeLine().value(QStringLiteral("words")).toList();
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("ink")).toDouble(), metrics.horizontalAdvance(QStringLiteral("i")));

        const QString wide = QStringLiteral("WWWWWWWW");
        layer.setLine(line(5000, {word(5000, 5400, wide, 0), word(5400, 5800, wide + QStringLiteral("  "), 400)}));
        layer.setPositionMs(5500);
        words = layer.describeLine().value(QStringLiteral("words")).toList();
        const double ink = metrics.horizontalAdvance(wide);
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("ink")).toDouble(), ink);
        QCOMPARE(words.at(1).toMap().value(QStringLiteral("ink")).toDouble(), ink);
        for (const QPointF &birth : births(layer)) {
            const double left = birth.x() < 400 ? 0 : 400;
            QVERIFY(birth.x() >= left + 0.1 * ink - 1e-9);
            QVERIFY(birth.x() <= left + 0.9 * ink + 1e-9);
        }

        // The same words at another size -- "fit" shrinking them -- are
        // measured again too.
        QVariantMap smaller = line(5000, {word(5000, 5400, wide, 0), word(5400, 5800, wide + QStringLiteral("  "), 400)});
        QFont font = lineFont();
        font.setPixelSize(17);
        smaller.insert(QStringLiteral("font"), font);
        layer.setLine(smaller);
        words = layer.describeLine().value(QStringLiteral("words")).toList();
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("ink")).toDouble(), QFontMetricsF(font).horizontalAdvance(wide));
    }

    // A move of the whole line (a translation appearing, the widget resized)
    // moves the births of the line being sung with it.
    void theLineBeingSungFollowsItsOrigin()
    {
        Layer layer;
        setUp(layer, twoWords(), 1500);
        const QList<QPointF> before = births(layer);
        QVERIFY(!before.isEmpty());
        layer.setLineOrigin(QPointF(100, 50));
        const QList<QPointF> after = births(layer);
        QCOMPARE(after.size(), before.size());
        for (int i = 0; i < before.size(); ++i) {
            QCOMPARE(after.at(i), before.at(i) + QPointF(100, 50));
        }
        QCOMPARE(layer.describeLine().value(QStringLiteral("x")).toDouble(), 100.0);
    }

    // Turned on while the line already follows an offset -- a marquee
    // scrolled, and paused so that the offset never changes again -- the
    // particles start from that offset, not from the unscrolled row.
    void turnedOnTheLineFollowsTheOffsetItAlreadyHas()
    {
        Layer layer;
        layer.setLineOffset(QPointF(-500, 12));
        layer.setLineOpacity(0.5);
        layer.setLine(twoWords());
        layer.setPositionMs(1500);
        QCOMPARE(layer.snapshotCount(), 0);
        layer.setActive(true);
        const QVariantList snapshots = layer.describeSnapshots();
        QCOMPARE(snapshots.size(), 1);
        const QVariantMap live = snapshots.first().toMap();
        QCOMPARE(live.value(QStringLiteral("offsetX")).toDouble(), -500.0);
        QCOMPARE(live.value(QStringLiteral("offsetY")).toDouble(), 12.0);
        QCOMPARE(live.value(QStringLiteral("opacity")).toDouble(), 0.5);
    }

    // The same line set again is no change at all.
    void anUnchangedLineIsIgnored()
    {
        Layer layer;
        setUp(layer, twoWords(), 1500);
        int changes = 0;
        connect(&layer, &WordParticleLayer::lineChanged, this, [&changes] { ++changes; });
        layer.setLine(twoWords());
        QCOMPARE(changes, 0);
        layer.setLine(line(1000, {word(1000, 1400, QStringLiteral("ab"), 0), word(1400, 1800, QStringLiteral("cd"), 61)}));
        QCOMPARE(changes, 1);
    }

    // Frames are asked for only while there is something to draw, plus the
    // one that clears the last of it.
    void framesAreRequestedOnlyForParticles()
    {
        Layer idle;
        idle.setActive(true);
        const int start = idle.updateRequests();
        idle.setColor(Qt::red);
        idle.setFontSize(20);
        idle.setLineOffset(QPointF(3, 4));
        idle.setLineOpacity(0.5);
        idle.setPositionMs(1234);
        QCOMPARE(idle.updateRequests(), start);

        Layer layer;
        setUp(layer, twoWords(), 1500);
        // Paused: the position stands still, and a colour fade still redraws.
        int requests = layer.updateRequests();
        layer.setColor(Qt::green);
        QCOMPARE(layer.updateRequests(), requests + 1);
        layer.setLineOffset(QPointF(0, 7));
        QCOMPARE(layer.updateRequests(), requests + 2);
        layer.setLineOpacity(0.5);
        QCOMPARE(layer.updateRequests(), requests + 3);
        layer.setFontSize(16);
        QCOMPARE(layer.updateRequests(), requests + 4);

        // Switched to a line without words: the kept line redraws each
        // frame until it goes, then once more to clear, then never.
        layer.detach();
        layer.setLine(QVariant());
        const double until = layer.particlesAliveUntilMs();
        QVERIFY(std::isfinite(until));
        requests = layer.updateRequests();
        layer.setPositionMs(until - 1);
        QCOMPARE(layer.updateRequests(), requests + 1);
        layer.setPositionMs(until);
        QCOMPARE(layer.snapshotCount(), 0);
        QCOMPARE(layer.updateRequests(), requests + 2);
        layer.setPositionMs(until + 16);
        layer.setColor(Qt::blue);
        QCOMPARE(layer.updateRequests(), requests + 2);
    }
};

QTEST_MAIN(WordParticleLayerTest)
#include "tst_wordparticlelayer.moc"
