#include "frontend/qmlmodule/wordparticles.h"

#include <QTest>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace WordParticles;

// DESIGN.md decision 77. Values that come straight out of the generator are
// compared exactly; anything through sin/cos/pow gets a tolerance, since two
// libms (Debian's and Arch's) may differ in the last bit.
class WordParticlesTest : public QObject
{
    Q_OBJECT

    static WordSpan span(qint64 startMs, qint64 endMs, double left = 0, double width = 30)
    {
        WordSpan word;
        word.startMs = startMs;
        word.endMs = endMs;
        word.left = left;
        word.width = width;
        return word;
    }

    // Many different words of one long line, for the distribution checks.
    static QList<Particle> manyParticles(int words)
    {
        QList<Particle> out;
        for (int i = 0; i < words; ++i) {
            out.append(spawn(123456, i, span(i * 300, i * 300 + 250), 10, 30));
        }
        return out;
    }

    static Snapshot snapshotAt(qint64 startMs, const QString &text, qint64 wordMs = 250)
    {
        LineLayout line;
        line.startMs = startMs;
        line.text = text;
        line.glyphTop = 10;
        line.ascent = 30;
        line.words = {span(startMs, startMs + wordMs), span(startMs + wordMs, startMs + 2 * wordMs, 30)};
        return capture(line);
    }

private Q_SLOTS:
    // splitmix64's published first output for seed 0, so this is that
    // generator and not something that merely looks random.
    void generatorIsSplitMix64()
    {
        Random random(0);
        QCOMPARE(random.next(), 0xE220A8397B1DCDAFULL);
        QCOMPARE(random.next(), 0x6E789E6AA1B965F4ULL);
    }

    void unitIsInTheHalfOpenInterval()
    {
        Random random(42);
        double lowest = 1;
        double highest = 0;
        for (int i = 0; i < 100000; ++i) {
            const double u = random.unit();
            QVERIFY(u >= 0);
            QVERIFY(u < 1);
            lowest = std::min(lowest, u);
            highest = std::max(highest, u);
        }
        QVERIFY(lowest < 0.001);
        QVERIFY(highest > 0.999);
        // The top 53 bits over 2^53, which is exactly representable.
        Random again(42);
        const quint64 bits = again.next();
        Random mapped(42);
        QCOMPARE(mapped.unit(), static_cast<double>(bits >> 11) / 9007199254740992.0);
    }

    // The same line and word draw the same particles every time, and each
    // word of a line draws its own.
    void spawningIsDeterministicPerLineAndWord()
    {
        const WordSpan word = span(1000, 1300, 50, 40);
        const QList<Particle> first = spawn(777, 3, word, 5, 30);
        const QList<Particle> second = spawn(777, 3, word, 5, 30);
        QCOMPARE(first.size(), second.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(first.at(i).birthMs, second.at(i).birthMs);
            QCOMPARE(first.at(i).size, second.at(i).size);
            QCOMPARE(first.at(i).x, second.at(i).x);
            QCOMPARE(first.at(i).y, second.at(i).y);
            QCOMPARE(first.at(i).jitter, second.at(i).jitter);
            QCOMPARE(first.at(i).amplitude, second.at(i).amplitude);
            QCOMPARE(first.at(i).periodMs, second.at(i).periodMs);
            QCOMPARE(first.at(i).phase1, second.at(i).phase1);
            QCOMPARE(first.at(i).phase2, second.at(i).phase2);
            QCOMPARE(first.at(i).phase3, second.at(i).phase3);
        }
        QVERIFY(seedFor(777, 3) != seedFor(777, 4));
        QVERIFY(seedFor(777, 3) != seedFor(778, 3));
        QVERIFY(spawn(777, 4, word, 5, 30).first().birthMs != first.first().birthMs);
        QVERIFY(spawn(778, 3, word, 5, 30).first().birthMs != first.first().birthMs);
    }

    // Pinned to the bit: the seed and every draw that does not go through
    // libm are integer arithmetic and IEEE multiplication, so these hold on
    // any standard library. A generator from <random> would fail here.
    void theSeedAndTheDrawsArePinned()
    {
        QCOMPARE(seedFor(12345, 7), 0x99E4853B12D56AB5ULL);
        const QList<Particle> particles = spawn(12345, 7, span(12345, 12645, 0, 30), 0, 30);
        QCOMPARE(particles.size(), 1);
        QVERIFY(particles.first().birthMs == 12421.369027395289);
        QVERIFY(particles.first().x == 5.31817799538738);
        QVERIFY(particles.first().y == 4.928920658156132);
        QVERIFY(particles.first().jitter == -0.44872057941952459);
    }

    void eachWordSpawnsOneTwoOrThreeWithEqualOdds()
    {
        int counts[4] = {0, 0, 0, 0};
        const int words = 6000;
        for (int i = 0; i < words; ++i) {
            const qsizetype n = spawn(98765, i, span(i * 300, i * 300 + 250), 10, 30).size();
            QVERIFY(n >= 1 && n <= 3);
            ++counts[n];
        }
        for (int n = 1; n <= 3; ++n) {
            QVERIFY2(std::abs(counts[n] / double(words) - 1.0 / 3) < 0.03,
                     qPrintable(QStringLiteral("%1: %2").arg(n).arg(counts[n])));
        }
    }

    // Decision 73's reason, applied here: a zero-length token (mostly
    // trailing punctuation) is never sung.
    void aZeroLengthTokenSpawnsNothing()
    {
        QVERIFY(spawn(0, 0, span(500, 500), 0, 30).isEmpty());
        QVERIFY(spawn(0, 0, span(500, 400), 0, 30).isEmpty());
        QVERIFY(!spawn(0, 0, span(500, 501), 0, 30).isEmpty());

        LineLayout line;
        line.startMs = 500;
        line.words = {span(500, 500), span(500, 500)};
        QVERIFY(capture(line).particles.isEmpty());
    }

    void birthsFallInTheFirstNinetyMillisecondsOrTheWholeShortWord()
    {
        double latestLong = 0;
        for (int i = 0; i < 2000; ++i) {
            for (const Particle &p : spawn(1, i, span(10000, 10500), 0, 30)) {
                QVERIFY(p.birthMs >= 10000);
                QVERIFY(p.birthMs < 10000 + 90);
                latestLong = std::max(latestLong, p.birthMs);
            }
            for (const Particle &p : spawn(2, i, span(10000, 10040), 0, 30)) {
                QVERIFY(p.birthMs >= 10000);
                QVERIFY(p.birthMs < 10040);
            }
        }
        QVERIFY(latestLong > 10000 + 88);
    }

    // Across the ink (10% to 90% of it) and down from the glyph top by up to
    // 35% of the CJK ascent.
    void birthPointsLieOnTheGlyph()
    {
        double leftmost = 1e9;
        double rightmost = -1e9;
        double deepest = 0;
        for (int i = 0; i < 2000; ++i) {
            for (const Particle &p : spawn(5, i, span(0, 300, 100, 50), 20, 40)) {
                QVERIFY(p.x >= 105);
                QVERIFY(p.x < 145);
                QVERIFY(p.y >= 20);
                QVERIFY(p.y < 20 + 14);
                leftmost = std::min(leftmost, p.x);
                rightmost = std::max(rightmost, p.x);
                deepest = std::max(deepest, p.y);
            }
        }
        QVERIFY(leftmost < 105.2);
        QVERIFY(rightmost > 144.8);
        QVERIFY(deepest > 33.8);
    }

    // s = 4^r: log4(s) uniform, so half are under 2 px and about a fifth
    // (1 - log4 3 = 20.75%) over 3 px.
    void sizesAreLogUniformBetweenOneAndFour()
    {
        const QList<Particle> particles = manyParticles(8000);
        int buckets[4] = {0, 0, 0, 0};
        int underTwo = 0;
        int overThree = 0;
        for (const Particle &p : particles) {
            QVERIFY(p.size >= 1);
            QVERIFY(p.size < 4);
            ++buckets[std::min(3, static_cast<int>(std::log(p.size) / std::log(4.0) * 4))];
            underTwo += p.size < 2;
            overThree += p.size > 3;
        }
        const double n = particles.size();
        for (int bucket : buckets) {
            QVERIFY2(std::abs(bucket / n - 0.25) < 0.02, qPrintable(QString::number(bucket / n)));
        }
        QVERIFY(std::abs(underTwo / n - 0.5) < 0.02);
        QVERIFY(std::abs(overThree / n - (1 - std::log(3.0) / std::log(4.0))) < 0.02);
    }

    void theOtherDrawsCoverTheirRanges()
    {
        const QList<Particle> particles = manyParticles(4000);
        double jitter[2] = {1, -1};
        double amplitude[2] = {2, 0};
        double period[2] = {1e9, 0};
        double phase[2] = {7, -1};
        for (const Particle &p : particles) {
            jitter[0] = std::min(jitter[0], p.jitter);
            jitter[1] = std::max(jitter[1], p.jitter);
            amplitude[0] = std::min(amplitude[0], p.amplitude);
            amplitude[1] = std::max(amplitude[1], p.amplitude);
            period[0] = std::min(period[0], p.periodMs);
            period[1] = std::max(period[1], p.periodMs);
            for (double value : {p.phase1, p.phase2, p.phase3}) {
                phase[0] = std::min(phase[0], value);
                phase[1] = std::max(phase[1], value);
            }
        }
        QVERIFY(jitter[0] >= -1 && jitter[0] < -0.99);
        QVERIFY(jitter[1] < 1 && jitter[1] > 0.99);
        QVERIFY(amplitude[0] >= 0.6 && amplitude[0] < 0.61);
        QVERIFY(amplitude[1] < 1.4 && amplitude[1] > 1.39);
        QVERIFY(period[0] >= 1400 && period[0] < 1410);
        QVERIFY(period[1] < 2200 && period[1] > 2190);
        QVERIFY(phase[0] >= 0 && phase[0] < 0.01);
        QVERIFY(phase[1] < 2 * std::numbers::pi && phase[1] > 2 * std::numbers::pi - 0.01);
    }

    void depthRunsFromTheSmallestToTheLargest()
    {
        QCOMPARE(depth(1), 0.0);
        QCOMPARE(depth(4), 1.0);
        QCOMPARE(depth(2.5), 0.5);
    }

    // b(t): up over 50 ms, held to 1550 ms, down to 0 at 2050 ms, times
    // 0.45 + 0.55k.
    void brightnessRisesHoldsAndFades()
    {
        QCOMPARE(envelope(-10), 0.0);
        QCOMPARE(envelope(0), 0.0);
        QCOMPARE(envelope(25), 0.5);
        QCOMPARE(envelope(50), 1.0);
        QCOMPARE(envelope(800), 1.0);
        QCOMPARE(envelope(1550), 1.0);
        QCOMPARE(envelope(1800), 0.5);
        QCOMPARE(envelope(2050), 0.0);
        QCOMPARE(envelope(3000), 0.0);
        QCOMPARE(kLifeMs, 2050.0);

        Particle small;
        small.size = 1;
        Particle large;
        large.size = 4;
        Particle middle;
        middle.size = 2.5;
        QCOMPARE(brightness(small, 800), 0.45);
        QCOMPARE(brightness(large, 800), 1.0);
        QCOMPARE(brightness(middle, 800), 0.45 + 0.55 * 0.5);
        QCOMPARE(brightness(large, 25), 0.5);
        QCOMPARE(brightness(large, 2050), 0.0);
    }

    // H × (0.6 + 0.4k) × (1 + 0.25j) × (1 - (1 - u)²), u = t / 2050: fast
    // first, and still rising when it goes out.
    void riseIsFastFirstAndLastsTheWholeLife()
    {
        Particle p;
        p.size = 2.5; // k = 0.5
        p.jitter = 0.4;
        const double full = 36 * (0.6 + 0.4 * 0.5) * (1 + 0.25 * 0.4);
        QCOMPARE(rise(p, 0), 0.0);
        QCOMPARE(rise(p, 2050), full);
        QVERIFY(std::abs(rise(p, 1025) - 0.75 * full) < 1e-9);
        QVERIFY(rise(p, 2000) > rise(p, 1900));
        QVERIFY(rise(p, 1025) - rise(p, 0) > rise(p, 2050) - rise(p, 1025));
    }

    // At birth the particle moves straight up: the shared current is 0 by
    // construction and the sway only ramps in over 700 ms.
    void swayAndWindAreZeroAtBirth()
    {
        QCOMPARE(swayRamp(0), 0.0);
        QCOMPARE(swayRamp(-5), 0.0);
        QCOMPARE(swayRamp(350), 0.5);
        QCOMPARE(swayRamp(700), 1.0);
        QCOMPARE(swayRamp(1500), 1.0);
        for (const Particle &p : manyParticles(200)) {
            QCOMPARE(wind(p, 0), 0.0);
            QCOMPARE(wander(p, 0), 0.0);
            QCOMPARE(drift(p, 0), 0.0);
            QCOMPARE(fall(p, 0), 0.0);
        }
    }

    // The formulas themselves, restated independently.
    void driftAndFallFollowTheFormula()
    {
        Particle p;
        p.birthMs = 61234;
        p.x = 317;
        p.size = 3.1;
        p.jitter = -0.3;
        p.amplitude = 1.1;
        p.periodMs = 1800;
        p.phase1 = 0.7;
        p.phase2 = 2.2;
        p.phase3 = 4.9;
        const double k = (3.1 - 1) / 3;
        const double a = 1 * (0.6 + 0.4 * k);
        for (double t : {100.0, 450.0, 1200.0, 2000.0}) {
            const double x = std::min(1.0, t / 700);
            const double ramp = x * x * (3 - 2 * x);
            const double w = ramp * a * 1.1 * 2
                * (0.62 * std::sin(2 * std::numbers::pi * t / 1800 + 0.7)
                   + 0.28 * std::sin(2 * std::numbers::pi * 1.73 * t / 1800 + 2.2)
                   + 0.10 * std::sin(2 * std::numbers::pi * 2.9 * t / 1800 + 4.9));
            const double omega = 2 * std::numbers::pi / 3400;
            const double psi = 0.012 * 317;
            const double current = 1.2 * a * (std::cos(omega * 61234 + psi) - std::cos(omega * (61234 + t) + psi));
            QVERIFY(std::abs(wander(p, t) - w) < 1e-9);
            QVERIFY(std::abs(wind(p, t) - current) < 1e-9);
            QVERIFY(std::abs(drift(p, t) - (current + 0.5 * w)) < 1e-9);
            const double bob = ramp * a * 0.35 * std::sin(2 * std::numbers::pi * t / (0.8 * 1800) + 2.2);
            const double u = t / 2050;
            const double up = 36 * (0.6 + 0.4 * k) * (1 - 0.25 * 0.3) * (1 - (1 - u) * (1 - u));
            QVERIFY(std::abs(fall(p, t) - (bob - up)) < 1e-9);
        }
    }

    // Neighbours born together drift together; ψ changes with x, in logical
    // pixels and never scaled.
    void theCurrentIsShared()
    {
        Particle left;
        left.birthMs = 5000;
        left.x = 200;
        left.size = 4;
        Particle neighbour = left;
        neighbour.amplitude = 0.7;
        neighbour.phase1 = 3;
        QCOMPARE(wind(left, 900), wind(neighbour, 900));
        Particle far = left;
        far.x = 460; // half the ~520 px wavelength
        QVERIFY(std::abs(wind(left, 900) - wind(far, 900)) > 1e-3);
    }

    void evaluateScalesFollowsAndDims()
    {
        Particle p;
        p.birthMs = 1000;
        p.x = 50;
        p.y = 80;
        p.size = 2;
        Sprite sprite;
        QVERIFY(!evaluate(p, 1000, 1, 0, 0, 1, &sprite));
        QVERIFY(!evaluate(p, 999, 1, 0, 0, 1, &sprite));
        QVERIFY(!evaluate(p, 1000 + 2050, 1, 0, 0, 1, &sprite));
        QVERIFY(!evaluate(p, 1500, 1, 0, 0, 0, &sprite));

        QVERIFY(evaluate(p, 1500, 1, 0, 0, 1, &sprite));
        QVERIFY(std::abs(sprite.x - (50 + drift(p, 500))) < 1e-9);
        QVERIFY(std::abs(sprite.y - (80 + fall(p, 500))) < 1e-9);
        QCOMPARE(sprite.coreRadius, 1.0);
        QCOMPARE(sprite.haloRadius, 7.0);
        QCOMPARE(sprite.brightness, brightness(p, 500));

        // The panel's 16 px: 16 / 34 of every length, the birth point aside.
        const double scale = 16.0 / 34;
        Sprite small;
        QVERIFY(evaluate(p, 1500, scale, 7, -3, 0.5, &small));
        QVERIFY(std::abs(small.x - (50 + 7 + scale * drift(p, 500))) < 1e-9);
        QVERIFY(std::abs(small.y - (80 - 3 + scale * fall(p, 500))) < 1e-9);
        QVERIFY(std::abs(small.coreRadius - scale) < 1e-12);
        QVERIFY(std::abs(small.haloRadius - 7 * scale) < 1e-12);
        QCOMPARE(small.brightness, 0.5 * brightness(p, 500));
    }

    void capturePinsTheLastBirth()
    {
        const Snapshot snapshot = snapshotAt(4000, QStringLiteral("line"));
        QVERIFY(!snapshot.particles.isEmpty());
        double last = 0;
        for (const Particle &p : snapshot.particles) {
            last = std::max(last, p.birthMs);
        }
        QCOMPARE(snapshot.lastBirthMs, last);
        QVERIFY(last >= 4250 && last < 4250 + 90);
        QCOMPARE(snapshot.aliveUntilMs(), last + 2050);
    }

    void aliveUntilCoversEveryKeptLine()
    {
        Field field;
        QCOMPARE(field.aliveUntilMs(), -std::numeric_limits<double>::infinity());
        const Snapshot first = snapshotAt(0, QStringLiteral("one"));
        field.setLive(first);
        QCOMPARE(field.aliveUntilMs(), first.aliveUntilMs());
        field.detach();
        const Snapshot second = snapshotAt(500, QStringLiteral("two"));
        field.setLive(second);
        QCOMPARE(field.snapshots().size(), 2);
        QCOMPARE(field.aliveUntilMs(), second.aliveUntilMs());

        // Moving on to a line with no words leaves the kept ones in charge.
        field.detach();
        field.setLive(Snapshot());
        QCOMPARE(field.snapshots().size(), 2);
        QCOMPARE(field.aliveUntilMs(), second.aliveUntilMs());
    }

    // Kept while startMs <= position < last birth + 2050; the current line is
    // never pruned.
    void keptLinesLiveUntilTheirLastParticleGoesOut()
    {
        Field field;
        const Snapshot line = snapshotAt(1000, QStringLiteral("a"));
        field.setLive(line);
        field.detach();
        field.setLive(Snapshot());
        field.prune(line.aliveUntilMs() - 1);
        QCOMPARE(field.snapshots().size(), 1);
        field.prune(line.aliveUntilMs());
        QCOMPARE(field.snapshots().size(), 0);
        QCOMPARE(field.aliveUntilMs(), -std::numeric_limits<double>::infinity());

        // A seek back to before the line started drops it too.
        field.setLive(line);
        field.detach();
        field.setLive(Snapshot());
        field.prune(999);
        QCOMPARE(field.snapshots().size(), 0);

        // The line being sung stays whatever the position.
        field.setLive(line);
        field.prune(line.aliveUntilMs() + 60000);
        field.prune(0);
        QCOMPARE(field.snapshots().size(), 1);
    }

    // Keyed on (startMs, text): the line being sung replaces an older copy of
    // itself, and a repeated line with other timings is a line of its own.
    void theCurrentLineReplacesAnOlderCopy()
    {
        Field field;
        const Snapshot line = snapshotAt(1000, QStringLiteral("refrain"));
        field.setLive(line);
        field.detach();
        field.setLive(snapshotAt(3000, QStringLiteral("refrain")));
        QCOMPARE(field.snapshots().size(), 2);
        field.detach();
        field.setLive(line);
        QCOMPARE(field.snapshots().size(), 2);
        QCOMPARE(field.snapshots().last()->startMs, 1000);
        QCOMPARE(field.snapshots().first()->startMs, 3000);

        // A switch that lands on the same line (only the second line
        // changed): detach() keeps a copy until the line shows up again,
        // then the copy goes -- through setLive(), or through the next
        // prune() when the line's layout did not change at all. Without that,
        // every particle would be drawn twice.
        field.detach();
        QCOMPARE(field.snapshots().size(), 3);
        field.setLive(line);
        QCOMPARE(field.snapshots().size(), 2);
        field.detach();
        QCOMPARE(field.snapshots().size(), 3);
        field.dedupe();
        QCOMPARE(field.snapshots().size(), 2);
        field.detach();
        QCOMPARE(field.snapshots().size(), 3);
        field.prune(3500);
        QCOMPARE(field.snapshots().size(), 2);
        QCOMPARE(field.snapshots().first()->startMs, 3000);

        // Detaching the same line twice keeps one copy.
        field.setLive(Snapshot());
        field.setLive(line);
        field.detach();
        field.detach();
        field.setLive(Snapshot());
        int copies = 0;
        for (const Snapshot *s : field.snapshots()) {
            copies += s->sameLine(line);
        }
        QCOMPARE(copies, 1);
    }

    // A detached line keeps what it was following at the switch.
    void aDetachedLineFreezesWhereItWas()
    {
        Field field;
        field.setLiveFollow(-40, 12, 0.8);
        field.setLive(snapshotAt(0, QStringLiteral("a")));
        QCOMPARE(field.snapshots().last()->offsetX, -40.0);
        field.detach();
        field.setLiveFollow(0, 33, 0);
        field.setLive(snapshotAt(900, QStringLiteral("b")));
        QCOMPARE(field.snapshots().size(), 2);
        const Snapshot *kept = field.snapshots().first();
        QCOMPARE(kept->offsetX, -40.0);
        QCOMPARE(kept->offsetY, 12.0);
        QCOMPARE(kept->opacity, 0.8);
        const Snapshot *live = field.snapshots().last();
        QCOMPARE(live->offsetY, 33.0);
        QCOMPARE(live->opacity, 0.0);
    }

    // A new track drops everything, and the line being sung at that moment
    // does not come back through the line switch that follows.
    void dropAllTakesTheLineBeingSungWithIt()
    {
        Field field;
        field.setLive(snapshotAt(0, QStringLiteral("a")));
        field.detach();
        field.setLive(snapshotAt(600, QStringLiteral("b")));
        field.dropAll();
        QCOMPARE(field.snapshots().size(), 0);
        QCOMPARE(field.aliveUntilMs(), -std::numeric_limits<double>::infinity());
        // Still dropped when the old line is laid out again.
        field.setLive(snapshotAt(600, QStringLiteral("b")));
        QCOMPARE(field.snapshots().size(), 0);
        field.detach();
        field.setLive(snapshotAt(0, QStringLiteral("new track")));
        QCOMPARE(field.snapshots().size(), 1);
        QCOMPARE(field.snapshots().first()->text, QStringLiteral("new track"));
    }

    void blendingFollowsTheColoursLuminance()
    {
        // Decision 77's defaults: the dark set's #fffaf5 adds, the light
        // set's #1f1b16 blends normally.
        QVERIFY(isAdditive(1.0, 0xfa / 255.0, 0xf5 / 255.0));
        QVERIFY(!isAdditive(0x1f / 255.0, 0x1b / 255.0, 0x16 / 255.0));
        QVERIFY(isAdditive(0.51, 0.51, 0.51));
        QVERIFY(!isAdditive(0.49, 0.49, 0.49));
        // The sRGB components as they are, weighted 0.2126/0.7152/0.0722:
        // green alone crosses 0.5 at 0.7, red and blue alone never do.
        QVERIFY(isAdditive(0, 0.71, 0));
        QVERIFY(!isAdditive(0, 0.69, 0));
        QVERIFY(!isAdditive(1, 0, 0));
        QVERIFY(!isAdditive(0, 0, 1));
        QVERIFY(isAdditive(1, 0.42, 0));
        QVERIFY(!isAdditive(1, 0.39, 0));
    }

    // Premultiplied: additive writes alpha 0, normal writes alpha = amount.
    void vertexColoursArePremultiplied()
    {
        const Vertex added = colourVertex(3, 4, 1, 0.5, 0, 0.5, true);
        QCOMPARE(added.x, 3.0f);
        QCOMPARE(added.y, 4.0f);
        QCOMPARE(int(added.r), 128);
        QCOMPARE(int(added.g), 64);
        QCOMPARE(int(added.b), 0);
        QCOMPARE(int(added.a), 0);
        const Vertex blended = colourVertex(3, 4, 1, 0.5, 0, 0.5, false);
        QCOMPARE(int(blended.r), 128);
        QCOMPARE(int(blended.g), 64);
        QCOMPARE(int(blended.a), 128);
        const Vertex full = colourVertex(0, 0, 1, 1, 1, 1, false);
        QCOMPARE(int(full.r), 255);
        QCOMPARE(int(full.a), 255);
    }

    void aSpriteIsAHaloUnderAFeatheredCore()
    {
        Sprite sprite;
        sprite.x = 100;
        sprite.y = 50;
        sprite.coreRadius = 2;
        sprite.haloRadius = 14;
        sprite.brightness = 0.8;
        std::vector<Vertex> vertices(2 * kVerticesPerSprite);
        std::vector<quint32> indices(kIndicesPerSprite);
        // As the second sprite in the buffer, so the indices are offset.
        writeSprite(sprite, 1, 1, 1, true, 0.8, vertices.data() + kVerticesPerSprite,
                    kVerticesPerSprite, indices.data());
        const Vertex *v = vertices.data() + kVerticesPerSprite;
        for (quint32 index : indices) {
            QVERIFY(index >= quint32(kVerticesPerSprite));
            QVERIFY(index < quint32(2 * kVerticesPerSprite));
        }
        for (int i = 0; i < kVerticesPerSprite; ++i) {
            QCOMPARE(int(v[i].a), 0);
        }

        // Halo: colour × 0.55 at the centre, nothing on the rim at 3.5 s.
        QCOMPARE(v[0].x, 100.0f);
        QCOMPARE(int(v[0].r), int(std::lround(0.55 * 0.8 * 255)));
        for (int i = 1; i <= kHaloSegments; ++i) {
            QCOMPARE(int(v[i].r), 0);
            QVERIFY(std::abs(std::hypot(v[i].x - 100.0, v[i].y - 50.0) - 14) < 1e-4);
        }
        // Core: solid out to s / 2, then transparent across the feather.
        const Vertex *core = v + 1 + kHaloSegments;
        QCOMPARE(int(core[0].r), int(std::lround(0.8 * 255)));
        for (int i = 0; i < kCoreSegments; ++i) {
            const Vertex &edge = core[1 + 2 * i];
            const Vertex &fringe = core[2 + 2 * i];
            QCOMPARE(int(edge.r), int(std::lround(0.8 * 255)));
            QVERIFY(std::abs(std::hypot(edge.x - 100.0, edge.y - 50.0) - 2) < 1e-4);
            QCOMPARE(int(fringe.r), 0);
            QVERIFY(std::abs(std::hypot(fringe.x - 100.0, fringe.y - 50.0) - 2.8) < 1e-4);
        }

        std::vector<Vertex> normal(kVerticesPerSprite);
        writeSprite(sprite, 0.1, 0.1, 0.1, false, 0.8, normal.data(), 0, indices.data());
        QCOMPARE(int(normal[0].a), int(std::lround(0.55 * 0.8 * 255)));
        QCOMPARE(int(normal[1 + kHaloSegments].a), int(std::lround(0.8 * 255)));
        QCOMPARE(int(normal[1].a), 0);
    }
};

QTEST_GUILESS_MAIN(WordParticlesTest)
#include "tst_wordparticles.moc"
