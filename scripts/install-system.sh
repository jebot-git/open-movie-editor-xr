#!/bin/sh
# Invoke through sudo after staging and verifying build-system.
set -eu
ome_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
/usr/bin/cmake --install "$ome_root/build-system"
/usr/bin/update-desktop-database /usr/local/share/applications
/usr/bin/update-mime-database /usr/local/share/mime
/usr/local/bin/openmovieeditor --version
