#include "frontend/qmlmodule/wordparticlelayer.h"

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

} // namespace

class WordParticleLayerTest : public QObject
{
    Q_OBJECT

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

    // Decision 77: every length is defined at 34 px and scales with the
    // font size -- the panel's 16 px draws at 16 / 34. The birth points do
    // not move, so each halo's radius halves exactly at 17 px.
    void sizesScaleWithTheFontSize()
    {
        Layer full;
        setUp(full, twoWords(), 1500);
        Layer half;
        setUp(half, twoWords(), 1500);
        half.setFontSize(17);
        const QSGGeometryNode *big = full.paint();
        const QSGGeometryNode *small = half.paint();
        QVERIFY(big && small);
        QCOMPARE(small->geometry()->vertexCount(), big->geometry()->vertexCount());
        const auto *a = big->geometry()->vertexDataAsColoredPoint2D();
        const auto *b = small->geometry()->vertexDataAsColoredPoint2D();
        for (int sprite = 0; sprite < big->geometry()->vertexCount() / WordParticles::kVerticesPerSprite; ++sprite) {
            const int centre = sprite * WordParticles::kVerticesPerSprite;
            const double haloBig = distance(a[centre], a[centre + 1]);
            const double haloSmall = distance(b[centre], b[centre + 1]);
            QVERIFY(haloBig > 0);
            QVERIFY2(std::abs(haloSmall / haloBig - 0.5) < 1e-4, qPrintable(QString::number(haloSmall / haloBig)));
            // 3.5 s, s being in [1, 4) at 34 px.
            QVERIFY(haloBig >= 3.5 - 1e-3 && haloBig < 14);
            // The core's fringe is one device pixel wide whatever the size,
            // and without a window the ratio is 1.
            const int core = centre + 1 + WordParticles::kHaloSegments;
            const double edge = distance(b[core], b[core + 1]);
            const double fringe = distance(b[core], b[core + 2]);
            QVERIFY2(std::abs(fringe - edge - 1) < 1e-4, qPrintable(QString::number(fringe - edge)));
        }
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
        const int core = 1 + WordParticles::kHaloSegments;
        QVERIFY(v[0].a > 0);
        QVERIFY(v[core].a > v[0].a);
        // Premultiplied: no channel above alpha.
        QVERIFY(v[core].r <= v[core].a);
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
