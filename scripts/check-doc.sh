#!/usr/bin/env bash
# cachecliff -- build the Doxygen documentation; any warning fails (see Doxyfile:
# WARN_AS_ERROR = FAIL_ON_WARNINGS, WARN_NO_PARAMDOC = YES). That makes sure every
# function, macro, type and variable has a Doxygen comment, with all its parameters.
#
#   scripts/check-doc.sh        output in doc/html
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DOXYGEN="${DOXYGEN:-doxygen}"
if ! command -v "$DOXYGEN" >/dev/null 2>&1; then
    echo "error: $DOXYGEN not found (apt install doxygen)" >&2
    exit 127
fi
"$DOXYGEN" Doxyfile
echo "doc OK (doc/html/index.html)"
