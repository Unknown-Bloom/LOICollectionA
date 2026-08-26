#!/usr/bin/env bash
# Standalone frontend test build (Linux/g++), independent of the LeviLamina
# toolchain:
#   * ll::Expected is shimmed onto std::expected (tests/shim/ll/api/Expected.h)
#   * native classes bound to ll::ui observables are dropped (declarations only)
# Usage: tests/build_frontend_tests.sh [build dir]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/frontend-tests}"

src="$root/src/LOICollectionA"
tests="$root/tests"

sources=(
    "$src/frontend/Lexer.cpp"
    "$src/frontend/Parser.cpp"
    "$src/frontend/SemanticAnalyzer.cpp"
    "$src/frontend/Callback.cpp"
    "$src/frontend/ScriptLoader.cpp"
    "$src/frontend/ir/Compiler.cpp"
    "$src/frontend/ir/Optimizer.cpp"
    "$src/frontend/ir/VM.cpp"
    "$src/frontend/ir/BytecodeSerializer.cpp"
    "$src/frontend/stdlib/StringClass.cpp"
    "$src/frontend/stdlib/ArrayClass.cpp"
    "$src/frontend/stdlib/MapClass.cpp"
    "$src/frontend/stdlib/GlobalValueClass.cpp"
    "$src/frontend/stdlib/CtxValueClass.cpp"
    "$src/frontend/stdlib/StringBuiltin.cpp"
    "$src/frontend/stdlib/MathBuiltin.cpp"
    "$src/frontend/stdlib/FormatBuiltin.cpp"
    "$src/utils/core/MathUtils.cpp"
    "$src/utils/core/Sha256.cpp"
)

test_sources=("$tests"/common/frontend/*.cpp)

mkdir -p "$build"
cxx="${CXX:-clang++}"
exec "$cxx" -std=c++23 -O1 -g \
    -DLOICOLLECTION_A_EXPORTS -DLOICOLLECTION_TEST_NO_OBSERVABLE \
    "-DLOICOLLECTION_TEST_ASSETS_DIR=\"$root/assets/common/gui\"" \
    -I"$tests/shim" -I"$root/src" -I"$tests" \
    "${sources[@]}" "${test_sources[@]}" \
    -lgtest -lgtest_main -lfmt -pthread \
    -o "$build/frontend_tests"
