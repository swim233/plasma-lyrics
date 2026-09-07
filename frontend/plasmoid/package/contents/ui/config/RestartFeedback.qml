import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    id: root

    required property bool succeeded
    required property bool failed
    required property string errorText
    readonly property bool shouldShow: succeeded || failed

    visible: shouldShow
    type: succeeded ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
    text: succeeded
        ? i18n("Lyrics service restarted.")
        : i18n("Could not restart lyrics service: %1", errorText)
}
