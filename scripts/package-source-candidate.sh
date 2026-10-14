#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo 'usage: package-source-candidate.sh OUTPUT-DIRECTORY' >&2
    exit 2
fi
command -v git >/dev/null || { echo 'git is required' >&2; exit 2; }
command -v zstd >/dev/null || { echo 'zstd is required' >&2; exit 2; }
command -v sha256sum >/dev/null || { echo 'sha256sum is required' >&2; exit 2; }
git rev-parse --show-toplevel >/dev/null
if ! git diff --quiet || ! git diff --cached --quiet || [ -n "$(git ls-files --others --exclude-standard)" ]; then
    echo 'source archive requires a committed worktree' >&2
    exit 2
fi
if [ -n "$(git ls-files INSTRUCTION.md SUPERVISOR-BRIEF.md RUN-ACCOUNTING.md)" ]; then
    echo 'operator records must never enter a source archive' >&2
    exit 2
fi
version=$(sed -n 's/.*astra \([0-9][0-9.]*-dev\).*/\1/p' src/main.cpp | head -n 1)
if [ -z "$version" ]; then
    echo 'cannot identify the development version' >&2
    exit 2
fi
output_dir=$1
mkdir -p "$output_dir"
archive="$output_dir/astra-$version.tar.zst"
if [ -e "$archive" ] || [ -e "$archive.sha256" ]; then
    echo 'candidate artifact already exists; choose a fresh output directory' >&2
    exit 2
fi
git archive --format=tar --prefix="astra-$version/" HEAD | zstd -T1 -q -o "$archive"
(cd "$output_dir" && sha256sum "astra-$version.tar.zst" > "astra-$version.tar.zst.sha256")
printf '%s\n' "$archive"
