#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

catalog="plasma_applet_io.github.swim233.plasma-lyrics"
podir="${podir:-$(dirname "$0")/templates}"
mkdir -p "$podir"
# i18np has to be listed too: without it xgettext walks straight past the
# auto-hide delay SpinBoxes and the plural drops out of the template, which
# is how "%1 second" came to survive only in the checked-in .pot.
xgettext --from-code=UTF-8 --language=JavaScript --keyword=i18n --keyword=i18nc:1c,2 \
    --keyword=i18np:1,2 \
    --output="$podir/$catalog.pot" \
    frontend/plasmoid/package/contents/config/config.qml \
    frontend/plasmoid/package/contents/ui/*.qml \
    frontend/plasmoid/package/contents/ui/config/*.qml

