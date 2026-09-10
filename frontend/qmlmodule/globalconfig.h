#pragma once

#include <QObject>
#include <QQmlEngine>

// Configuration page front-end for the global lyric offset (DESIGN.md
// decision 41). Shaped after BackendConfig: setters only mutate members and
// mark the object unsaved, save() is what actually reaches storage. Unlike
// BackendConfig, the values live in the LyricStore SQLite database rather
// than a QSettings INI so save() can report storage errors synchronously.
// After a successful write it sends the daemon a best-effort notification;
// the daemon then republishes the effective value to all snapshot consumers.
class GlobalConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int offsetMs READ offsetMs WRITE setOffsetMs NOTIFY changed)
    Q_PROPERTY(int maximumOffsetMs READ maximumOffsetMs CONSTANT)
    Q_PROPERTY(bool unsavedChanges READ unsavedChanges NOTIFY unsavedChangesChanged)

public:
    explicit GlobalConfig(QObject *parent = nullptr);

    bool enabled() const;
    int offsetMs() const;
    int maximumOffsetMs() const;
    bool unsavedChanges() const;

    void setEnabled(bool value);
    void setOffsetMs(int value);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool save();

Q_SIGNALS:
    void changed();
    void unsavedChangesChanged();
    void saved();

private:
    void markUnsaved();

    bool m_enabled = false;
    int m_offsetMs = 0;
    bool m_unsavedChanges = false;
};
