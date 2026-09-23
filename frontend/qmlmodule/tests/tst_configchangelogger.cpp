#include "frontend/qmlmodule/configchangelogger.h"

#include <QDBusConnection>
#include <QQmlPropertyMap>
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

// QQmlPropertyMap::create() first appeared in Qt 6.11, which also deprecated
// the public constructor. Debian 13 ships Qt 6.8, which has no create(), and
// the -Werror CI build on Arch runs Qt 6.11+, where the constructor warns.
QQmlPropertyMap *makePropertyMap()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    return QQmlPropertyMap::create();
#else
    return new QQmlPropertyMap;
#endif
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
        std::unique_ptr<QQmlPropertyMap> map(makePropertyMap());
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
        std::unique_ptr<QQmlPropertyMap> map(makePropertyMap());
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
        std::unique_ptr<QQmlPropertyMap> map(makePropertyMap());
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
        std::unique_ptr<QQmlPropertyMap> map(makePropertyMap());
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

    void survivesConfigurationDestroyedFromUnderneath()
    {
        auto *heapMap = makePropertyMap();
        heapMap->insert(QStringLiteral("desktopFontSize"), 34);

        ConfigChangeLogger logger;
        logger.setAppletId(12);
        logger.setForm(QStringLiteral("desktop"));
        logger.setConfiguration(heapMap);

        // Nothing in main.qml destroys the map out from under the logger
        // today, but the property is a plain pointer with no ownership
        // contract, so this must not crash: the QPointer inside the logger
        // is expected to read back as null once the map is gone, rather
        // than leaving a dangling QQmlPropertyMap* for setConfiguration()
        // to disconnect from or dereference.
        delete heapMap;

        // Clearing the configuration must not touch the freed map either.
        logger.setConfiguration(nullptr);

        std::unique_ptr<QQmlPropertyMap> replacement(makePropertyMap());
        replacement->insert(QStringLiteral("desktopFontSize"), 50);
        logger.setConfiguration(replacement.get());

        changeValue(replacement.get(), QStringLiteral("desktopFontSize"), 60);

        QTRY_COMPARE(m_control.calls.size(), 1);
        QCOMPARE(m_control.calls.first().oldValue, QStringLiteral("50"));
        QCOMPARE(m_control.calls.first().newValue, QStringLiteral("60"));
    }

    void replacingConfigurationResnapshots()
    {
        std::unique_ptr<QQmlPropertyMap> firstMap(makePropertyMap());
        firstMap->insert(QStringLiteral("desktopFontSize"), 34);

        std::unique_ptr<QQmlPropertyMap> secondMap(makePropertyMap());
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
