#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -DRELEASE_BUILD -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isrc -Itests/fakes tests/test_core.cpp src/markdown.cpp src/text_editor.cpp src/file_manager.cpp \
  -o "$out/test_core"
"$out/test_core"
