#include "wordparticlelayer.h"

#include <QFontMetricsF>
#include <QJSValue>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <cstddef>
#include <limits>

// WordParticles::writeSprite fills the geometry's vertex buffer directly.
static_assert(sizeof(WordParticles::Vertex) == sizeof(QSGGeometry::ColoredPoint2D));
static_assert(offsetof(WordParticles::Vertex, x) == offsetof(QSGGeometry::ColoredPoint2D, x));
static_assert(offsetof(WordParticles::Vertex, y) == offsetof(QSGGeometry::ColoredPoint2D, y));
static_assert(offsetof(WordParticles::Vertex, r) == offsetof(QSGGeometry::ColoredPoint2D, r));
static_assert(offsetof(WordParticles::Vertex, a) == offsetof(QSGGeometry::ColoredPoint2D, a));

namespace {

QString withoutTrailingSpace(QString text)
{
    while (!text.isEmpty() && text.back().isSpace()) {
        text.chop(1);
    }
    return text;
}

} // namespace

WordParticleLayer::WordParticleLayer(QQuickItem *parent)
    : QQuickItem(parent)
    , m_aliveUntilMs(-std::numeric_limits<qreal>::infinity())
{
    setFlag(ItemHasContents);
}

bool WordParticleLayer::active() const
{
    return m_active;
}

QString WordParticleLayer::fingerprint() const
{
    return m_fingerprint;
}

qreal WordParticleLayer::positionMs() const
{
    return m_positionMs;
}

int WordParticleLayer::fontSize() const
{
    return m_fontSize;
}

QColor WordParticleLayer::color() const
{
    return m_color;
}

QVariant WordParticleLayer::line() const
{
    return m_line;
}

QPointF WordParticleLayer::lineOrigin() const
{
    return m_lineOrigin;
}

QPointF WordParticleLayer::lineOffset() const
{
    return m_lineOffset;
}

qreal WordParticleLayer::lineOpacity() const
{
    return m_lineOpacity;
}

qreal WordParticleLayer::particlesAliveUntilMs() const
{
    return m_aliveUntilMs;
}

int WordParticleLayer::snapshotCount() const
{
    return m_snapshotCount;
}

void WordParticleLayer::setActive(bool value)
{
    if (value == m_active) {
        return;
    }
    m_active = value;
    m_field = WordParticles::Field();
    m_field.setLiveFollow(m_lineOffset.x(), m_lineOffset.y(), m_lineOpacity);
    recaptureLine();
    Q_EMIT activeChanged();
}

void WordParticleLayer::setFingerprint(const QString &value)
{
    if (value == m_fingerprint) {
        return;
    }
    m_fingerprint = value;
    // The value every instance is created with is no track change.
    if (isComponentComplete()) {
        m_field.dropAll();
        fieldChanged();
    }
    Q_EMIT fingerprintChanged();
}

void WordParticleLayer::setPositionMs(qreal value)
{
    if (value == m_positionMs) {
        return;
    }
    m_positionMs = value;
    m_field.prune(m_positionMs);
    fieldChanged();
    Q_EMIT positionMsChanged();
}

void WordParticleLayer::setFontSize(int value)
{
    if (value == m_fontSize) {
        return;
    }
    m_fontSize = value;
    redraw();
    Q_EMIT fontSizeChanged();
}

void WordParticleLayer::setColor(const QColor &value)
{
    if (value == m_color) {
        return;
    }
    m_color = value;
    redraw();
    Q_EMIT colorChanged();
}

void WordParticleLayer::setLine(const QVariant &value)
{
    // A JS object assigned from QML arrives as a QJSValue, and two of those
    // compare equal only when they are the same object: compared as that,
    // no line from QML would ever be recognised as unchanged.
    const QVariant line = value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant()
        : value;
    if (line == m_line) {
        return;
    }
    m_line = line;
    measureLine();
    recaptureLine();
    Q_EMIT lineChanged();
}

void WordParticleLayer::setLineOrigin(const QPointF &value)
{
    if (value == m_lineOrigin) {
        return;
    }
    m_lineOrigin = value;
    recaptureLine();
    Q_EMIT lineOriginChanged();
}

void WordParticleLayer::setLineOffset(const QPointF &value)
{
    if (value == m_lineOffset) {
        return;
    }
    m_lineOffset = value;
    m_field.setLiveFollow(m_lineOffset.x(), m_lineOffset.y(), m_lineOpacity);
    redraw();
    Q_EMIT lineOffsetChanged();
}

void WordParticleLayer::setLineOpacity(qreal value)
{
    if (value == m_lineOpacity) {
        return;
    }
    m_lineOpacity = value;
    m_field.setLiveFollow(m_lineOffset.x(), m_lineOffset.y(), m_lineOpacity);
    redraw();
    Q_EMIT lineOpacityChanged();
}

void WordParticleLayer::detach()
{
    m_field.detach(m_positionMs);
    fieldChanged();
    polish();
}

QVariantList WordParticleLayer::describeSnapshots() const
{
    QVariantList out;
    const QList<const WordParticles::Snapshot *> snapshots = m_field.snapshots();
    for (const WordParticles::Snapshot *snapshot : snapshots) {
        QVariantList births;
        for (const WordParticles::Particle &particle : snapshot->particles) {
            births.append(QPointF(particle.x, particle.y));
        }
        out.append(QVariantMap{
            {QStringLiteral("startMs"), snapshot->startMs},
            {QStringLiteral("text"), snapshot->text},
            {QStringLiteral("births"), births},
            {QStringLiteral("offsetX"), snapshot->offsetX},
            {QStringLiteral("offsetY"), snapshot->offsetY},
            {QStringLiteral("opacity"), snapshot->opacity},
            {QStringLiteral("current"), snapshot == m_field.live()},
        });
    }
    return out;
}

void WordParticleLayer::componentComplete()
{
    QQuickItem::componentComplete();
    measureLine();
    recaptureLine();
}

// The line switch that called detach() has finished by now. If it landed on
// the same line (only the second line changed) and nothing about the line's
// layout moved, no setLive() has come to drop the copy detach() kept, and
// without this every particle would be drawn twice until the position moved.
void WordParticleLayer::updatePolish()
{
    m_field.dedupe();
    fieldChanged();
}

void WordParticleLayer::measureLine()
{
    const QVariantMap line = m_line.toMap();
    const QVariantList words = line.value(QStringLiteral("words")).toList();
    m_words.clear();
    m_lineStartMs = line.value(QStringLiteral("startMs")).toLongLong();
    m_lineText = line.value(QStringLiteral("text")).toString();
    m_rowPosition = QPointF(line.value(QStringLiteral("x")).toDouble(), line.value(QStringLiteral("y")).toDouble());
    if (words.isEmpty()) {
        return;
    }

    const QFont font = line.value(QStringLiteral("font")).value<QFont>();
    QStringList texts;
    texts.reserve(words.size());
    for (const QVariant &word : words) {
        texts.append(word.toMap().value(QStringLiteral("text")).toString());
    }
    if (font != m_measuredFont || texts != m_measuredTexts || m_inkWidths.size() != texts.size()) {
        const QFontMetricsF metrics(font);
        m_inkWidths.clear();
        for (const QString &text : std::as_const(texts)) {
            m_inkWidths.append(metrics.horizontalAdvance(withoutTrailingSpace(text)));
        }
        // The top of a CJK glyph at this size, whatever script the line is
        // in. A font with no such glyph anywhere falls back to its own
        // ascent.
        const QRectF ink = metrics.tightBoundingRect(QStringLiteral("国"));
        m_cjkAscent = ink.isEmpty() ? metrics.ascent() : -ink.top();
        m_measuredFont = font;
        m_measuredTexts = texts;
    }

    m_words.reserve(words.size());
    for (int i = 0; i < words.size(); ++i) {
        const QVariantMap word = words.at(i).toMap();
        MeasuredWord measured;
        measured.startMs = word.value(QStringLiteral("startMs")).toLongLong();
        measured.endMs = word.value(QStringLiteral("endMs")).toLongLong();
        measured.inkWidth = m_inkWidths.at(i);
        if (const auto *item = qobject_cast<QQuickItem *>(word.value(QStringLiteral("item")).value<QObject *>())) {
            measured.x = item->x();
            measured.baseline = item->baselineOffset();
        } else {
            measured.x = word.value(QStringLiteral("x")).toDouble();
            measured.baseline = word.value(QStringLiteral("baseline")).toDouble();
        }
        m_words.append(measured);
    }
}

WordParticles::LineLayout WordParticleLayer::layoutOfLine() const
{
    WordParticles::LineLayout layout;
    const QPointF row = m_lineOrigin + m_rowPosition;
    layout.startMs = m_lineStartMs;
    layout.text = m_lineText;
    layout.ascent = m_cjkAscent;
    layout.words.reserve(m_words.size());
    for (const MeasuredWord &word : m_words) {
        WordParticles::WordSpan span;
        span.startMs = word.startMs;
        span.endMs = word.endMs;
        span.left = row.x() + word.x;
        span.width = word.inkWidth;
        span.top = row.y() + word.baseline - m_cjkAscent;
        layout.words.append(span);
    }
    return layout;
}

void WordParticleLayer::recaptureLine()
{
    if (!isComponentComplete()) {
        return;
    }
    m_field.setLive(m_active ? WordParticles::capture(layoutOfLine()) : WordParticles::Snapshot());
    fieldChanged();
}

QVariantMap WordParticleLayer::describeLine() const
{
    QVariantList words;
    for (const MeasuredWord &word : m_words) {
        words.append(QVariantMap{
            {QStringLiteral("x"), word.x},
            {QStringLiteral("baseline"), word.baseline},
            {QStringLiteral("ink"), word.inkWidth},
        });
    }
    const QPointF row = m_lineOrigin + m_rowPosition;
    return {
        {QStringLiteral("startMs"), m_lineStartMs},
        {QStringLiteral("text"), m_lineText},
        {QStringLiteral("x"), row.x()},
        {QStringLiteral("y"), row.y()},
        {QStringLiteral("ascent"), m_cjkAscent},
        {QStringLiteral("words"), words},
    };
}

int WordParticleLayer::updateRequests() const
{
    return m_updateRequests;
}

void WordParticleLayer::requestUpdate()
{
    ++m_updateRequests;
    update();
}

// Only while there is something to draw: with particles off, or between
// songs, a sliding block or a colour fade asks for no work here at all.
void WordParticleLayer::redraw()
{
    if (m_snapshotCount > 0) {
        requestUpdate();
    }
}

void WordParticleLayer::fieldChanged()
{
    const QList<const WordParticles::Snapshot *> snapshots = m_field.snapshots();
    const qreal aliveUntilMs = m_field.aliveUntilMs();
    const int count = static_cast<int>(snapshots.size());
    // One more frame after the last line goes, to clear what it drew.
    if (count > 0 || m_snapshotCount > 0) {
        requestUpdate();
    }
    if (aliveUntilMs != m_aliveUntilMs || count != m_snapshotCount) {
        m_aliveUntilMs = aliveUntilMs;
        m_snapshotCount = count;
        Q_EMIT snapshotsChanged();
    }
}

QSGNode *WordParticleLayer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    m_sprites.clear();
    const double scale = m_fontSize / WordParticles::kReferencePixelSize;
    for (const WordParticles::Snapshot *snapshot : m_field.snapshots()) {
        for (const WordParticles::Particle &particle : snapshot->particles) {
            WordParticles::Sprite sprite;
            if (WordParticles::evaluate(particle, m_positionMs, scale, snapshot->offsetX,
                                        snapshot->offsetY, snapshot->opacity, &sprite)) {
                m_sprites.append(sprite);
            }
        }
    }
    // Deleted rather than emptied: see the class comment. Deleting a node
    // takes it out of its parent; the window never deletes a node an item
    // replaces, which is why QQuickItem's own default does the same.
    if (m_sprites.isEmpty()) {
        delete oldNode;
        return nullptr;
    }

    auto *node = static_cast<QSGGeometryNode *>(oldNode);
    if (!node) {
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0,
                                         QSGGeometry::UnsignedIntType);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StreamPattern);
        geometry->setIndexDataPattern(QSGGeometry::StreamPattern);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    const double red = m_color.redF();
    const double green = m_color.greenF();
    const double blue = m_color.blueF();
    const bool additive = WordParticles::isAdditive(red, green, blue);
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    const double feather = 1 / (ratio > 0 ? ratio : 1);

    QSGGeometry *geometry = node->geometry();
    const int count = static_cast<int>(m_sprites.size());
    geometry->allocate(count * WordParticles::kVerticesPerSprite, count * WordParticles::kIndicesPerSprite);
    auto *vertices = static_cast<WordParticles::Vertex *>(geometry->vertexData());
    quint32 *indices = geometry->indexDataAsUInt();
    for (int i = 0; i < count; ++i) {
        WordParticles::writeSprite(m_sprites.at(i), red, green, blue, additive, feather,
                                   vertices + i * WordParticles::kVerticesPerSprite,
                                   static_cast<quint32>(i * WordParticles::kVerticesPerSprite),
                                   indices + i * WordParticles::kIndicesPerSprite);
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
