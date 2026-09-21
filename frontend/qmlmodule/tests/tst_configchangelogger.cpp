#include "frontend/qmlmodule/configchangelogger.h"

#include <QDBusConnection>
#include <QQmlPropertyMap>
#include <QStandardPaths>
#include <QTest>

#include <memory>

using namespace PlasmaLyrics;

namespace {

QStringList *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && (type == QtInfoMsg || type == QtWarningMsg)) {
        capturedMessages->append(message);
    }
}

// Mirrors daemon/tests/tst_controlservice.cpp's helper of the same name.
class MessageCapture
{
public:
    MessageCapture()
        : m_previous(qInstallMessageHandler(captureMessages))
    {
        capturedMessages = &m_messages;
    }

    ~MessageCapture()
    {
        capturedMessages = nullptr;
        qInstallMessageHandler(m_previous);
    }

    const QStringList &messages() const { return m_messages; }

private:
    QStringList m_messages;
    QtMessageHandler m_previous;
};

// QML's `Plasmoid.configuration[key] = value` reaches a QQmlPropertyMap
// through the JS engine's dynamic-property write path (QObject::setProperty
// under the hood), which is what actually emits valueChanged() on an
// *existing* key. QQmlPropertyMap::insert()/operator[] do not -- they are
// meant for populating the map, not simulating a QML-side write -- so every
// simulated change below goes through setProperty() to match real usage.
void changeValue(QQmlPropertyMap *map, const QString &key, const QVariant &value)
{
    map->setProperty(key.toUtf8().constData(), value);
}

} // namespace

class FakeControl final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    struct Call
    {
        QString store;
        QString applet;
        QString form;
        QString key;
        QString oldValue;
        QString newValue;
    };

    QList<Call> calls;

public Q_SLOTS:
    void NoteConfigChange(QString store, QString applet, QString form, QString key,
                          QString oldValue, QString newValue)
    {
        calls.append({store, applet, form, key, oldValue, newValue});
    }
};

class ConfigChangeLoggerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &m_control, QDBusConnection::ExportAllSlots));
    }

    void cleanup()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
        m_control.calls.clear();
    }

    void changedValueLogsAndForwards()
    {
        std::unique_ptr<QQmlPropertyMap> map(QQmlPropertyMap::create());
        map->insert(QStringLiteral("desktopFontSize"), 34);

        MessageCapture capture;
        ConfigChangeLogger logger;
        logger.setAppletId(12);
        logger.setForm(QStringLiteral("desktop"));
        logger.setConfiguration(map.get());

        changeValue(map.get(), QStringLiteral("desktopFontSize"), 36);

        QVERIFY(capture.messages().contains(QStringLiteral(
            "config changed store=applet applet=12 form=desktop key=desktopFontSize "
            "old=\"34\" new=\"36\"")));
        QTRY_COMPARE(m_control.calls.size(), 1);
        const auto call = m_control.calls.first();
        QCOMPARE(call.store, QStringLiteral("applet"));
        QCOMPARE(call.applet, QStringLiteral("12"));
        QCOMPARE(call.form, QStringLiteral("desktop"));
        QCOMPARE(call.key, QStringLiteral("desktopFontSize"));
        QCOMPARE(call.oldValue, QStringLiteral("34"));
        QCOMPARE(call.newValue, QStringLiteral("36"));
    }

    void identicalReassignmentProducesNothing()
    {
        std::unique_ptr<QQmlPropertyMap> map(QQmlPropertyMap::create());
        map->insert(QStringLiteral("desktopFontSize"), 34);

        MessageCapture capture;
        ConfigChangeLogger logger;
        logger.setAppletId(12);
        logger.setForm(QStringLiteral("desktop"));
        logger.setConfiguration(map.get());

        changeValue(map.get(), QStringLiteral("desktopFontSize"), 34);

        // Give any (incorrect) D-Bus forward a chance to arrive before
        // asserting it did not.
        QTest::qWait(50);
        QVERIFY(capture.messages().isEmpty());
        QCOMPARE(m_control.calls.size(), 0);
    }

    void boolAndStringListRenderCorrectly()
    {
        std::unique_ptr<QQmlPropertyMap> map(QQmlPropertyMap::create());
        map->insert(QStringLiteral("desktopAutoHide"), false);
        map->insert(QStringLiteral("providers"), QStringList{QStringLiteral("a")});

        ConfigChangeLogger logger;
        logger.setAppletId(1);
        logger.setForm(QStringLiteral("panel"));
        logger.setConfiguration(map.get());

        changeValue(map.get(), QStringLiteral("desktopAutoHide"), true);
        changeValue(map.get(), QStringLiteral("providers"),
                    QStringList{QStringLiteral("a"), QStringLiteral("b")});

        QTRY_COMPARE(m_control.calls.size(), 2);
        QCOMPARE(m_control.calls.at(0).oldValue, QStringLiteral("false"));
        QCOMPARE(m_control.calls.at(0).newValue, QStringLiteral("true"));
        QCOMPARE(m_control.calls.at(1).oldValue, QStringLiteral("a"));
        QCOMPARE(m_control.calls.at(1).newValue, QStringLiteral("a,b"));
    }

    void secondChangeUsesUpdatedSnapshot()
    {
        std::unique_ptr<QQmlPropertyMap> map(QQmlPropertyMap::create());
        map->insert(QStringLiteral("desktopFontSize"), 34);

        ConfigChangeLogger logger;
        logger.setAppletId(12);
        logger.setForm(QStringLiteral("desktop"));
        logger.setConfiguration(map.get());

        changeValue(map.get(), QStringLiteral("desktopFontSize"), 36);
        QTRY_COMPARE(m_control.calls.size(), 1);
        changeValue(map.get(), QStringLiteral("desktopFontSize"), 40);
        QTRY_COMPARE(m_control.calls.size(), 2);

        QCOMPARE(m_control.calls.at(1).oldValue, QStringLiteral("36"));
        QCOMPARE(m_control.calls.at(1).newValue, QStringLiteral("40"));
    }

    void replacingConfigurationResnapshots()
    {
        std::unique_ptr<QQmlPropertyMap> firstMap(QQmlPropertyMap::create());
        firstMap->insert(QStringLiteral("desktopFontSize"), 34);

        std::unique_ptr<QQmlPropertyMap> secondMap(QQmlPropertyMap::create());
        secondMap->insert(QStringLiteral("desktopFontSize"), 100);

        ConfigChangeLogger logger;
        logger.setAppletId(12);
        logger.setForm(QStringLiteral("desktop"));
        logger.setConfiguration(firstMap.get());
        logger.setConfiguration(secondMap.get());

        changeValue(secondMap.get(), QStringLiteral("desktopFontSize"), 200);

        QTRY_COMPARE(m_control.calls.size(), 1);
        QCOMPARE(m_control.calls.first().oldValue, QStringLiteral("100"));
        QCOMPARE(m_control.calls.first().newValue, QStringLiteral("200"));

        // A subsequent change on the old map, which is no longer connected,
        // must not produce a second call.
        changeValue(firstMap.get(), QStringLiteral("desktopFontSize"), 999);
        QTest::qWait(50);
        QCOMPARE(m_control.calls.size(), 1);
    }

private:
    FakeControl m_control;
};

QTEST_GUILESS_MAIN(ConfigChangeLoggerTest)
#include "tst_configchangelogger.moc"
