#!/usr/bin/env bash
# cachecliff -- Python format + lint for the generator (tools/) and its tests (tests/).
# Formatting: black. Linting: pylint (see .pylintrc).
#
#   scripts/format-py.sh            format all Python sources in place (black)
#   scripts/format-py.sh --check    check only: black --check + pylint;
#                                   non-zero exit if either fails
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BLACK="${BLACK:-black}"
PYLINT="${PYLINT:-pylint}"
if ! command -v "$BLACK" >/dev/null 2>&1; then
    echo "error: $BLACK not found (pip install black)" >&2
    exit 127
fi
mapfile -t FILES < <(git ls-files '*.py' 2>/dev/null || true)
if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "no Python files to format"
    exit 0
fi
if [[ "${1:-}" != "--check" ]]; then
    "$BLACK" "${FILES[@]}"
    exit 0
fi
fail=0
if ! "$BLACK" --check --diff "${FILES[@]}"; then
    echo "error: run scripts/format-py.sh to fix formatting" >&2
    fail=1
fi
if ! command -v "$PYLINT" >/dev/null 2>&1; then
    echo "error: $PYLINT not found (pip install pylint)" >&2
    exit 127
fi
if ! "$PYLINT" "${FILES[@]}"; then
    echo "error: pylint reported issues (see .pylintrc)" >&2
    fail=1
fi
exit "$fail"
