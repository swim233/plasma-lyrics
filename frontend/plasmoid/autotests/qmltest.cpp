// SPDX-License-Identifier: GPL-2.0-only
#include <QQmlEngine>
#include <QtQuickTest>

#include <KLocalizedQmlContext>

// The applet's QML calls i18n() everywhere, and that global comes from the
// localized context plasmashell installs on its engine -- not from an import.
// Without the same context here the config pages still construct, but every
// label binding throws a ReferenceError and buries the test output.
class Setup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        KLocalization::setupLocalizedContext(engine)->setTranslationDomain(
            QStringLiteral("plasma_applet_io.github.swim233.plasma-lyrics"));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(appearance, Setup)

#include "qmltest.moc"
