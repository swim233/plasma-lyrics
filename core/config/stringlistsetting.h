#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace PlasmaLyrics {

// Q24 / DESIGN.md decision 67. QSettings' INI backend cannot store a
// size-0 QStringList: QSettings::setValue(key, QStringList()) round-trips
// as the literal `@Invalid()`, which reads back with contains(key) == true
// but an *invalid* QVariant -- indistinguishable, by value alone, from "the
// key was never written" (confirmed against the real Qt6 INI backend, not
// assumed). A setting whose "nothing configured" state must be a
// deliberate, distinguishable user choice -- rather than collapsing into
// "unset, use the built-in default" -- therefore cannot use the primary
// key's value as the only signal. StringListSetting pairs it with a second
// boolean key that records "the user set this to nothing" explicitly.
struct StringListSetting {
    QString key;             // e.g. "players/blacklist"
    QString explicitEmptyKey; // e.g. "players/blacklistEmpty"
};

// Three-state read used for settings where empty is a legitimate, distinct
// user intent (players/blacklist, filter/musicUrlPrefixes):
//   - key holds a valid value (even one written by hand) -> that value
//     with blank/whitespace-only entries dropped, regardless of the
//     marker -- what the user just typed under `key` wins over a
//     possibly-stale marker;
//   - otherwise, explicitEmptyKey == true -> QStringList() (explicitly
//     emptied by the user);
//   - otherwise (key absent, or a leftover-invalid literal with no marker)
//     -> defaultValue.
// The last branch is what makes an unmigrated `key=@Invalid()` safe to
// read even before migrateLegacyInvalidEntry() has run: an invalid stored
// value never reaches the caller as "empty" unless the marker actually
// says so.
//
// The marker is stickier than `key` itself: once explicitEmptyKey == true
// has been written, hand-deleting `key` alone does NOT fall back to
// "unset -> built-in default" -- the marker branch is checked before the
// default one, and removing `key` doesn't touch it. Restoring the default
// by hand means deleting the `xxxEmpty` line, not the primary key.
//
// Expressing "empty" is the marker's job, not the primary key's: a bare
// hand-written `key=` is NOT the sanctioned way to write "nothing
// configured" -- writeStringListOrEmpty() never produces it, only the
// marker does. But `key=` parses as a single valid empty-string element
// rather than `@Invalid()`, so dropping blank entries here is what keeps
// it from being read as "one empty prefix", which a caller matching with
// QString::startsWith() would treat as matching every string -- silently
// inverting "no custom prefixes" into "accept everything". A `key=` left
// by hand therefore still reads as empty (for safety), it just isn't the
// form this module writes, and the next save through it normalizes to the
// marker form.
QStringList readStringListOrEmpty(const QSettings &settings, const StringListSetting &setting,
                                   const QStringList &defaultValue);

// Persists `value` for the read semantics above. A non-empty list is
// written normally and the marker cleared; an empty list removes the
// primary key -- never writing the literal `@Invalid()` -- and sets the
// marker so a later read reports "explicitly empty" rather than "unset".
void writeStringListOrEmpty(QSettings &settings, const StringListSetting &setting,
                             const QStringList &value);

// One-time, idempotent cleanup: a key still holding the literal
// `@Invalid()` (written by versions predating this fix, e.g. by
// QSettings::setValue(key, QStringList())) is treated as a defect
// artifact, not user intent -- remove it so the built-in default resumes
// applying. This is NOT a general truth about every leftover `@Invalid()`
// (see migrateLegacyInvalidEntryToExplicitEmpty() below, used precisely
// because it doesn't hold for every key); for the two callers of this
// function (players/blacklist, filter/musicUrlPrefixes) it rests on
// case-specific evidence -- a real journal capture of this exact
// corruption plus the user's own request to have it reverted -- not on
// any structural property of the setting. See DESIGN.md decision 67. A key
// holding any valid value, or already absent, is left untouched. Returns
// true if the file was changed. Idempotent: a second call always returns
// false and touches nothing.
bool migrateLegacyInvalidEntry(QSettings &settings, const QString &key);

// Same trigger as migrateLegacyInvalidEntry() (a key still holding the
// literal `@Invalid()`), but migrates to "explicitly empty" instead of
// "unset". Used for filter/platforms, where migrateLegacyInvalidEntry()'s
// premise doesn't hold: there is no case-specific evidence (no journal
// capture, no user request) that this corruption is unintended, so
// preserving the observed state is safer than guessing it away. This is
// NOT because a checkbox pair is somehow immune to accidental clearing --
// it isn't: reading `@Invalid()` back through the old `.value(key,
// default)` pattern gives an empty list for a checkbox pair exactly as it
// does for a free-text field, so the same self-perpetuating "any unrelated
// save rewrites it" mechanism applies to every key here equally (verified
// by probe, see DESIGN.md decision 67). The split between the two
// migration targets is justified by evidence available per key, not by
// widget type. Removes the key and sets setting.explicitEmptyKey to true.
// A key holding any valid value, or already absent, is left untouched,
// including the marker. Returns true if the file was changed. Idempotent.
bool migrateLegacyInvalidEntryToExplicitEmpty(QSettings &settings, const StringListSetting &setting);

// providers/order and providers/enabled keep their pre-existing "empty
// means: use the built-in default" semantics unchanged (no marker needed --
// empty is never a legitimate intent for either), but must still stop
// writing `@Invalid()`. Their *readers* do not treat "key absent" the same
// way, though, so an empty write needs a different substitute for each:
enum class EmptyStringListPolicy {
    // The reader (Config::providerOrder()) falls back to its built-in
    // default identically whether the key is absent or present-but-empty,
    // so removing the key on an empty write is safe and reproduces
    // exactly what `key=@Invalid()` used to produce.
    RemoveKey,
    // The reader (Config::enabledProviderOrder()) branches on
    // contains(key): absent means "everything providers/order lists is
    // enabled", present-but-drained means "fall back to the built-in
    // default". Removing the key on empty would silently switch to the
    // first branch instead of the second. Writing a single blank element
    // keeps contains() == true without ever writing `@Invalid()`; every
    // known reader of this key already discards blank/whitespace entries,
    // so the observable result is unchanged.
    KeepPresentAsBlank,
};
void writeStringListNoInvalid(QSettings &settings, const QString &key, const QStringList &value,
                               EmptyStringListPolicy policy);

} // namespace PlasmaLyrics
