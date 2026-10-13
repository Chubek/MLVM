#!/usr/bin/env bash
# get-deps.sh — clone MLVM's Git dependencies into third_party/.
#
# Includes the root third_party inventory and the dependencies required by
# the full libstone build. Existing source snapshots are preserved and verified;
# --update requires a Git checkout. No dependencies are fetched by CMake.
#
# Usage:
#   ./scripts/get-deps.sh [options] [package...]
#
#   packages: all (default), cparser-iso11, cpp-httplib, ctl, dparser,
#             kaguya, klib, libglr, lua, metatk, qamrpp, replxx, satie,
#             sfsexp, termlib, tomlc99, ucpp, equinox-ng, exolangtk,
#             memtkx, lmdbxx, isocline
#   options:
#     --dir DIR    destination root (default: <mlvm>/third_party)
#     --update     git pull --ff-only in existing checkouts
#     --force      remove and re-clone existing checkouts
#     --no-verify  skip post-clone file verification
#     -h, --help   this message
#
# Examples:
#   ./scripts/get-deps.sh                   # clone everything missing
#   ./scripts/get-deps.sh sfsexp ucpp       # clone just two packages
#   ./scripts/get-deps.sh --update          # update existing checkouts
#   ./scripts/get-deps.sh --dir /tmp/tp all # use a custom root
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_ROOT="${PROJECT_ROOT}/third_party"
DO_UPDATE=0
DO_FORCE=0
DO_VERIFY=1
PACKAGES=()

ALL_PACKAGES=(cparser-iso11 cpp-httplib ctl dparser kaguya klib libglr lua
  metatk qamrpp replxx satie sfsexp termlib tomlc99 ucpp
  equinox-ng exolangtk memtkx lmdbxx isocline)

# Repository names and directory names deliberately differ for a few packages.
package_info() {
  case "$1" in
    cparser-iso11|cparser) NAME=cparser-iso11; REPO=jkolek/cparser; FILES=(include/CParser.h) ;;
    cpp-httplib) NAME=$1; REPO=yhirose/cpp-httplib; FILES=(httplib.h) ;;
    ctl) NAME=$1; REPO=glouw/ctl; FILES=(ctl/vec.h) ;;
    dparser) NAME=$1; REPO=jplevyak/dparser; FILES=(dparse.h parse.c grammar.g.c make_dparser.c) ;;
    kaguya) NAME=$1; REPO=satoren/kaguya; FILES=(include/kaguya/kaguya.hpp) ;;
    klib) NAME=$1; REPO=attractivechaos/klib; FILES=(ketopt.h) ;;
    libglr) NAME=$1; REPO=Chubek/libglr; FILES=(CMakeLists.txt include/glr/glr.h) ;;
    lua) NAME=$1; REPO=lua/lua; FILES=(lua.h lapi.c lauxlib.c lualib.h) ;;
    metatk) NAME=$1; REPO=Chubek/metatk; FILES=(DSLtk/DSLtk.hpp EkippX/EkippX.hpp SExprTk/include/SExprTk.hpp) ;;
    qamrpp) NAME=$1; REPO=Chubek/QaMRpp; FILES=(include/QaMRpp.hpp) ;;
    replxx) NAME=$1; REPO=AmokHuginnsson/replxx; FILES=(include/replxx.hxx) ;;
    satie) NAME=$1; REPO=Chubek/Satie; FILES=(include/Satie.hpp src/SatieMemory.cpp) ;;
    sfsexp) NAME=$1; REPO=mjsottile/sfsexp; FILES=(src/sexp.h) ;;
    termlib) NAME=$1; REPO=Chubek/termlib; FILES=(termlib.h termlib.c) ;;
    tomlc99|toml) NAME=tomlc99; REPO=cktan/tomlc99; FILES=(toml.c toml.h) ;;
    ucpp) NAME=$1; REPO=lpsantil/ucpp; FILES=(cpp.h cpp.c) ;;
    equinox-ng) NAME=$1; REPO=Chubek/equinox-ng; FILES=(EquinoxNG.hpp) ;;
    exolangtk) NAME=$1; REPO=Chubek/ExolangTk; FILES=(CMakeLists.txt include/ExtensionTk/etk_api.h) ;;
    memtkx) NAME=$1; REPO=Chubek/memtkx; FILES=(include/memtkx/space/free_list.hpp) ;;
    lmdbxx) NAME=$1; REPO=bendiken/lmdbxx; FILES=(lmdb++.h) ;;
    isocline) NAME=$1; REPO=daanx/isocline; FILES=(src/isocline.c include/isocline.h) ;;
    *) die "unknown package '$1' (expected: ${ALL_PACKAGES[*]} all)" ;;
  esac
}

usage() {
  sed -n '2,/^set -euo/{ /^set -euo/d; s/^# \{0,1\}//; p; }' "${BASH_SOURCE[0]}"
}

die() { echo "get-deps.sh: error: $*" >&2; exit 1; }
info() { echo "get-deps.sh: $*"; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command '$1' not found"
}

clone_or_update() {
  local name="$1" url="$2" dest="$3"
  if [ -e "${dest}/.git" ]; then
    if [ "${DO_FORCE}" = "1" ]; then
      info "re-cloning ${name} (forced) -> ${dest}"
      rm -rf "${dest}"
    elif [ "${DO_UPDATE}" = "1" ]; then
      info "updating ${name} in ${dest}"
      git -C "${dest}" pull --ff-only
      git -C "${dest}" submodule update --init --recursive
      return 0
    else
      info "exists, skipping ${name} (${dest}); use --update to pull"
      return 0
    fi
  elif [ -e "${dest}" ]; then
    if [ "${DO_FORCE}" = "1" ]; then
      info "re-cloning ${name} (forced) -> ${dest}"
      rm -rf "${dest}"
    elif [ "${DO_UPDATE}" = "1" ]; then
      die "${dest} is a source snapshot, not a Git checkout (use --force to replace)"
    elif [ -d "${dest}" ]; then
      info "using existing source snapshot ${name} (${dest})"
      return 0
    else
      die "path ${dest} is not a directory (use --force to replace)"
    fi
  fi
  info "cloning ${name} from ${url} -> ${dest}"
  mkdir -p "$(dirname "${dest}")"
  git clone --depth 1 --recurse-submodules --shallow-submodules "${url}" "${dest}"
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

fetch_one() {
  local NAME REPO file
  local -a FILES
  package_info "$1"
  local dest="${DEST_ROOT}/${NAME}"
  clone_or_update "${NAME}" "https://github.com/${REPO}.git" "${dest}"
  if [ "${DO_VERIFY}" = "1" ]; then
    for file in "${FILES[@]}"; do
      [ -f "${dest}/${file}" ] || die "${NAME} verification failed, missing ${file} in ${dest}"
    done
    case "${NAME}" in
      sfsexp) verify_sfsexp "${dest}" ;;
      cparser-iso11) verify_cparser "${dest}" ;;
      ucpp) verify_ucpp "${dest}" ;;
    esac
  fi
  return 0
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
[ -n "${DEST_ROOT}" ] || die "--dir must not be empty"

# Validate the entire request before cloning or replacing anything.
for pkg in "${PACKAGES[@]}"; do
  if [ "${pkg}" != "all" ]; then package_info "${pkg}"; fi
done

if [ "${#PACKAGES[@]}" -eq 0 ] || { [ "${#PACKAGES[@]}" -eq 1 ] && [ "${PACKAGES[0]}" = "all" ]; }; then
  PACKAGES=("${ALL_PACKAGES[@]}")
fi

for pkg in "${PACKAGES[@]}"; do
  if [ "${pkg}" = "all" ]; then
    for p in "${ALL_PACKAGES[@]}"; do fetch_one "${p}"; done
  else
    fetch_one "${pkg}"
  fi
done

info "done -> ${DEST_ROOT}"
info "configure with: cmake -S \"${PROJECT_ROOT}\" -B build -DMLVM_THIRD_PARTY_DIR=\"${DEST_ROOT}\""
info "full libstone build: add -DMLVM_BUILD_LIBSTONE=ON"
