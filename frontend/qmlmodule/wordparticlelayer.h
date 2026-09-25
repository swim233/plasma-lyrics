#pragma once

#include "wordparticles.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariant>

// DESIGN.md decision 77: draws every word particle of the widget in one
// QSGGeometryNode with Qt's own QSGVertexColorMaterial -- no shader of our
// own. AnimatedLyric feeds it the line being sung and calls detach() on every
// line switch; the particles themselves are WordParticles' pure functions of
// positionMs, so pausing freezes them and a seek lands where playing would
// have.
//
// The software backend (QT_QUICK_BACKEND=software, and QT_QPA_PLATFORM=
// offscreen, which the QML suite runs on) draws no custom geometry node at
// all: there the particles are simply not seen, and nothing else changes.
class WordParticleLayer : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // Off clears everything at once, the line being sung included.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // A change drops every particle, the line being sung included: a new
    // track's position says nothing about the old track's lines.
    Q_PROPERTY(QString fingerprint READ fingerprint WRITE setFingerprint NOTIFY fingerprintChanged)
    Q_PROPERTY(qreal positionMs READ positionMs WRITE setPositionMs NOTIFY positionMsChanged)
    // Sizes and motion are defined at 34 px and scale by fontSize / 34.
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    // Only the RGB is used; the brightness envelope stands in for alpha.
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // LyricLine.particleLine of the line being sung, or null: { startMs, text,
    // words: [{ startMs, endMs, text, x, baseline }], x, y, font }: the row's
    // position in that line's coordinates, and each word's within the row.
    Q_PROPERTY(QVariant line READ line WRITE setLine NOTIFY lineChanged)
    // That line's top left in this item, leaving out what lineOffset carries.
    Q_PROPERTY(QPointF lineOrigin READ lineOrigin WRITE setLineOrigin NOTIFY lineOriginChanged)
    // What the line being sung is following -- the marquee scroll and the
    // block's slide -- and its block's opacity. A detached line keeps the
    // values these had when it was detached.
    Q_PROPERTY(QPointF lineOffset READ lineOffset WRITE setLineOffset NOTIFY lineOffsetChanged)
    Q_PROPERTY(qreal lineOpacity READ lineOpacity WRITE setLineOpacity NOTIFY lineOpacityChanged)

    // When the last particle of anything kept goes out, -Infinity when
    // nothing is. LyricsView keeps its frame clock running until then.
    Q_PROPERTY(qreal particlesAliveUntilMs READ particlesAliveUntilMs NOTIFY snapshotsChanged)
    // Lines with particles kept, the one being sung included. For tests.
    Q_PROPERTY(int snapshotCount READ snapshotCount NOTIFY snapshotsChanged)

public:
    explicit WordParticleLayer(QQuickItem *parent = nullptr);

    bool active() const;
    QString fingerprint() const;
    qreal positionMs() const;
    int fontSize() const;
    QColor color() const;
    QVariant line() const;
    QPointF lineOrigin() const;
    QPointF lineOffset() const;
    qreal lineOpacity() const;
    qreal particlesAliveUntilMs() const;
    int snapshotCount() const;

    void setActive(bool value);
    void setFingerprint(const QString &value);
    void setPositionMs(qreal value);
    void setFontSize(int value);
    void setColor(const QColor &value);
    void setLine(const QVariant &value);
    void setLineOrigin(const QPointF &value);
    void setLineOffset(const QPointF &value);
    void setLineOpacity(qreal value);

    /// The line being sung stops being the current one; its particles stay
    /// where they are and finish their flight. Called before the line switch
    /// moves anything; a switch that lands on the same line again is sorted
    /// out once it is complete, before the next frame.
    Q_INVOKABLE void detach();
    /// [{ startMs, text, births, offsetX, offsetY, opacity, current }] per
    /// snapshot, births being every particle's birth point. For tests: the
    /// software backend the QML suite runs on draws none of the geometry.
    Q_INVOKABLE QVariantList describeSnapshots() const;

Q_SIGNALS:
    void activeChanged();
    void fingerprintChanged();
    void positionMsChanged();
    void fontSizeChanged();
    void colorChanged();
    void lineChanged();
    void lineOriginChanged();
    void lineOffsetChanged();
    void lineOpacityChanged();
    void snapshotsChanged();

protected:
    void componentComplete() override;
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    WordParticles::LineLayout layoutOfLine();
    void recaptureLine();
    void redraw();
    void fieldChanged();

    bool m_active = false;
    QString m_fingerprint;
    qreal m_positionMs = 0;
    int m_fontSize = 34;
    QColor m_color = Qt::white;
    QVariant m_line;
    QPointF m_lineOrigin;
    QPointF m_lineOffset;
    qreal m_lineOpacity = 1;

    WordParticles::Field m_field;
    qreal m_aliveUntilMs;
    int m_snapshotCount = 0;

    // Glyph measurements for the line last captured. A line is captured
    // again every time the Row moves a word while laying it out, so they
    // are kept until the font or the words change.
    QFont m_measuredFont;
    QStringList m_measuredTexts;
    QList<double> m_inkWidths;
    double m_cjkAscent = 0;

    QList<WordParticles::Sprite> m_sprites;
};
