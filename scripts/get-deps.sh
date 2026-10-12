#!/usr/bin/env bash
# get-deps.sh — clone MLVM's Git dependencies into third_party/.
#
# Scanned sources:
#   CMakeLists.txt            (MLVM_ENABLE_CEPHYR / MLVM_ENABLE_OPTILISP /
#                              MLVM_ENABLE_BINUTILS, Python3, Threads)
#   cmake/MLVMDependencies.cmake  third_party/sfsexp   (Optilisp reader)
#   cmake/MLVMCParser.cmake       third_party/cparser-iso11 (Cephyr frontend)
#   cmake/MLVMUcpp.cmake          third_party/ucpp     (Cephyr preprocessor)
#   cmake/MLVMBinutils.cmake      third_party/klib + third_party/tomlc99
#                                 (binutils CLI parsing / target configs;
#                                  Limestone itself is checked in under
#                                  libstone/limestone and is NOT cloned)
#   cmake/FindMLVMTreeSitter.cmake is an unused finder for a system
#                                 tree-sitter; it is NOT cloned here.
#
# Usage:
#   ./get-deps.sh [options] [package...]
#
#   packages: all (default), sfsexp, cparser-iso11, ucpp, klib, tomlc99
#   options:
#     --dir DIR    destination root (default: <mlvm>/third_party)
#     --update     git pull --ff-only in existing checkouts
#     --force      remove and re-clone existing checkouts
#     --no-verify  skip post-clone file verification
#     -h, --help   this message
#
# Examples:
#   ./get-deps.sh                    # clone everything missing
#   ./get-deps.sh sfsexp ucpp        # clone just two packages
#   ./get-deps.sh --update           # fetch latest in existing checkouts
#   ./get-deps.sh --dir /tmp/tp all  # clone into a custom root
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_ROOT="${SCRIPT_DIR}/third_party"
DO_UPDATE=0
DO_FORCE=0
DO_VERIFY=1
PACKAGES=()

SFSEXP_URL="https://github.com/mjsottile/sfsexp.git"
CPARSER_URL="https://github.com/jkolek/cparser.git"
UCPP_URL="https://github.com/lpsantil/ucpp.git"
KLIB_URL="https://github.com/attractivechaos/klib.git"
TOML_URL="https://github.com/cktan/tomlc99.git"

usage() {
  sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die() { echo "get-deps.sh: error: $*" >&2; exit 1; }
info() { echo "get-deps.sh: $*"; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command '$1' not found"
}

clone_or_update() {
  local name="$1" url="$2" dest="$3"
  if [ -d "${dest}/.git" ]; then
    if [ "${DO_FORCE}" = "1" ]; then
      info "re-cloning ${name} (forced) -> ${dest}"
      rm -rf "${dest}"
    elif [ "${DO_UPDATE}" = "1" ]; then
      info "updating ${name} in ${dest}"
      git -C "${dest}" pull --ff-only
      return 0
    else
      info "exists, skipping ${name} (${dest}); use --update to pull"
      return 0
    fi
  elif [ -e "${dest}" ]; then
    if [ "${DO_FORCE}" = "1" ]; then
      info "re-cloning ${name} (forced) -> ${dest}"
      rm -rf "${dest}"
    else
      die "path ${dest} exists but is not a git checkout (use --force to replace)"
    fi
  fi
  info "cloning ${name} from ${url} -> ${dest}"
  mkdir -p "$(dirname "${dest}")"
  git clone --depth 1 "${url}" "${dest}"
}

# Verify the exact files the CMake modules check for, so a wrong remote or
# a sparse checkout fails here instead of at configure time.
verify_sfsexp() {
  local root="$1"
  local missing=""
  for f in src/sexp.c src/sexp.h src/sexp_ops.c src/sexp_ops.h \
           src/sexp_vis.c src/sexp_vis.h src/sexp_memory.c src/sexp_memory.h \
           src/cstring.c src/cstring.h src/faststack.c src/faststack.h \
           src/io.c src/parser.c src/event_temp.c src/sexp_errors.h; do
    [ -f "${root}/${f}" ] || missing="${missing} ${f}"
  done
  [ -z "${missing}" ] || die "sfsexp verification failed, missing:${missing}"
}

verify_cparser() {
  local root="$1"
  local missing=""
  for f in src/CParser.cpp src/Parser.cpp src/Lexer.cpp src/CLexer.cpp \
           src/ASTNode.cpp src/AbstractSyntaxTree.cpp src/SymbolTable.cpp \
           src/TreeVisitor.cpp include/CParser.h include/Parser.h \
           include/Lexer.h include/CLexer.h include/ASTNode.h \
           include/AbstractSyntaxTree.h include/SymbolTable.h \
           include/TreeVisitor.h include/common.h \
           include/PrintTreeVisitor.h LICENSE; do
    [ -f "${root}/${f}" ] || missing="${missing} ${f}"
  done
  [ -z "${missing}" ] || die "cparser-iso11 verification failed, missing:${missing}"
}

verify_ucpp() {
  local root="$1"
  local missing=""
  for f in mem.c nhash.c cpp.c lexer.c assert.c macro.c eval.c arith.c \
           cpp.h ucppi.h mem.h nhash.h arith.h config.h tune.h LICENSE; do
    [ -f "${root}/${f}" ] || missing="${missing} ${f}"
  done
  [ -z "${missing}" ] || die "ucpp verification failed, missing:${missing}"
}

verify_klib() {
  local root="$1"
  [ -f "${root}/ketopt.h" ] || die "klib verification failed, missing ketopt.h in ${root}"
}

verify_toml() {
  local root="$1"
  [ -f "${root}/toml.c" ] && [ -f "${root}/toml.h" ] \
    || die "tomlc99 verification failed, missing toml.c/toml.h in ${root}"
}

fetch_one() {
  local pkg="$1"
  case "${pkg}" in
    sfsexp)
      clone_or_update "sfsexp" "${SFSEXP_URL}" "${DEST_ROOT}/sfsexp"
      [ "${DO_VERIFY}" = "1" ] && verify_sfsexp "${DEST_ROOT}/sfsexp"
      ;;
    cparser-iso11|cparser)
      clone_or_update "cparser-iso11" "${CPARSER_URL}" "${DEST_ROOT}/cparser-iso11"
      [ "${DO_VERIFY}" = "1" ] && verify_cparser "${DEST_ROOT}/cparser-iso11"
      ;;
    ucpp)
      clone_or_update "ucpp" "${UCPP_URL}" "${DEST_ROOT}/ucpp"
      [ "${DO_VERIFY}" = "1" ] && verify_ucpp "${DEST_ROOT}/ucpp"
      ;;
    klib)
      clone_or_update "klib" "${KLIB_URL}" "${DEST_ROOT}/klib"
      [ "${DO_VERIFY}" = "1" ] && verify_klib "${DEST_ROOT}/klib"
      ;;
    tomlc99|toml)
      clone_or_update "tomlc99" "${TOML_URL}" "${DEST_ROOT}/tomlc99"
      [ "${DO_VERIFY}" = "1" ] && verify_toml "${DEST_ROOT}/tomlc99"
      ;;
    *) die "unknown package '${pkg}' (expected: sfsexp cparser-iso11 ucpp klib tomlc99 all)" ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --dir) shift; [ "$#" -gt 0 ] || die "--dir needs a value"; DEST_ROOT="$1" ;;
    --dir=*) DEST_ROOT="${1#--dir=}" ;;
    --update) DO_UPDATE=1 ;;
    --force) DO_FORCE=1 ;;
    --no-verify) DO_VERIFY=0 ;;
    --) shift; while [ "$#" -gt 0 ]; do PACKAGES+=("$1"); shift; done; break ;;
    -*) die "unknown option '$1' (see --help)" ;;
    *) PACKAGES+=("$1") ;;
  esac
  shift
done

need_cmd git

if [ "${#PACKAGES[@]}" -eq 0 ] || { [ "${#PACKAGES[@]}" -eq 1 ] && [ "${PACKAGES[0]}" = "all" ]; }; then
  PACKAGES=(sfsexp cparser-iso11 ucpp klib tomlc99)
fi

for pkg in "${PACKAGES[@]}"; do
  if [ "${pkg}" = "all" ]; then
    for p in sfsexp cparser-iso11 ucpp klib tomlc99; do fetch_one "${p}"; done
  else
    fetch_one "${pkg}"
  fi
done

info "done -> ${DEST_ROOT}"
info "configure with: cmake -S . -B build (defaults resolve to ${DEST_ROOT})"
info "overrides: -DMLVM_THIRD_PARTY_DIR=... -DMLVM_SFSEXP_SOURCE_DIR=... -DMLVM_CPARSER_SOURCE_DIR=... -DMLVM_UCPP_SOURCE_DIR=... -DMLVM_KLIB_SOURCE_DIR=... -DMLVM_TOML_SOURCE_DIR=..."
