#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

catalog="plasma_applet_io.github.swim233.plasma-lyrics"
podir="${podir:-$(dirname "$0")/templates}"
mkdir -p "$podir"
xgettext --from-code=UTF-8 --language=JavaScript --keyword=i18n --keyword=i18nc:1c,2 \
    --output="$podir/$catalog.pot" \
    frontend/plasmoid/package/contents/config/config.qml \
    frontend/plasmoid/package/contents/ui/*.qml \
    frontend/plasmoid/package/contents/ui/config/*.qml

