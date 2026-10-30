#!/usr/bin/env bash
# cachecliff -- install git hooks by pointing core.hooksPath at .githooks.
# Run once after cloning:  scripts/install-hooks.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
chmod +x .githooks/* scripts/*.sh 2>/dev/null || true
git config core.hooksPath .githooks
echo "git hooks installed (core.hooksPath = .githooks)"
echo "pre-commit enforces clang-format on staged C files and black + pylint on staged Python files."
echo "commit-msg enforces Conventional-Commit subject lines."
