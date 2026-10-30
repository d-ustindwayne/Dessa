#!/usr/bin/env sh
# Dessa installer for Linux and macOS.
#
#   sh install.sh            (from the Dessa folder)
#
# Builds dessa and riv, installs them to ~/.local/share/dessa, links them
# into ~/.local/bin, lets riv use the bundled packages without downloading
# them, and gets the WebAssembly compiler web pages need (dessa setup wasm).
# Options:  --yes (ask nothing)  --prefix DIR  --no-wasm  --uninstall
set -e

SOURCE=$(cd "$(dirname "$0")" && pwd)
PREFIX="${HOME}/.local/share/dessa"
LINKS="${HOME}/.local/bin"
YES=0
UNINSTALL=0
NOWASM=0

while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) YES=1 ;;
        --prefix) shift; PREFIX="$1" ;;
        --uninstall) UNINSTALL=1 ;;
        --no-wasm) NOWASM=1 ;;
        *) echo "unknown option: $1"; exit 1 ;;
    esac
    shift
done

say()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
ok()   { printf '   ok  %s\n' "$*"; }
warn() { printf '   !   %s\n' "$*"; }
fail() { printf '\nInstallation stopped: %s\n' "$*"; exit 1; }

ask() {   # ask "question" -> 0 for yes (default yes)
    [ "$YES" = 1 ] && return 0
    printf '   %s [Y/n] ' "$1"
    read -r answer
    case "$answer" in n|N|no|NO) return 1 ;; *) return 0 ;; esac
}

if [ "$UNINSTALL" = 1 ]; then
    step "Removing Dessa from $PREFIX"
    ask "Remove Dessa and its links in $LINKS?" || exit 0
    rm -f "$LINKS/dessa" "$LINKS/riv"
    rm -rf "$PREFIX"
    ok "removed (your riv store in ~/.riv and riv_modules folders were left alone)"
    say "   If you added $LINKS to PATH for Dessa, you can remove that line from your shell profile."
    exit 0
fi

say ""
say "  Dessa installer"
say "  ---------------"
say "  This builds Dessa from $SOURCE and installs it to $PREFIX."
# A downloaded release has bin/dessa already: nothing to build
if [ -x "$SOURCE/bin/dessa" ]; then PREBUILT=1; else PREBUILT=0; fi
[ "$PREBUILT" = 1 ] || [ -f "$SOURCE/CMakeLists.txt" ] || fail "run this script from the Dessa folder (a release with bin/dessa, or the source)"

step "Checking the tools Dessa needs"
hint() {
    if command -v apt-get >/dev/null 2>&1; then say "   install with: sudo apt-get install g++ cmake make"
    elif command -v dnf >/dev/null 2>&1; then say "   install with: sudo dnf install gcc-c++ cmake make"
    elif command -v pacman >/dev/null 2>&1; then say "   install with: sudo pacman -S gcc cmake make"
    elif command -v brew >/dev/null 2>&1; then say "   install with: xcode-select --install && brew install cmake"
    fi
}
if command -v g++ >/dev/null 2>&1; then CXX_FOUND=g++; elif command -v clang++ >/dev/null 2>&1; then CXX_FOUND=clang++; else CXX_FOUND=""; fi
[ -n "$CXX_FOUND" ] || { warn "no C++ compiler found (Dessa needs one to build your programs)"; hint; fail "install a C++20 compiler"; }
ok "C++ compiler: $(command -v "$CXX_FOUND")"
if command -v clang >/dev/null 2>&1; then ok "C compiler: $(command -v clang)"
else say "   C compiler: gcc (installing clang makes Dessa use it for the C it makes)"; fi
if [ "$PREBUILT" = 0 ]; then
    command -v cmake >/dev/null 2>&1 || { warn "CMake was not found"; hint; fail "install CMake"; }
    ok "CMake: $(command -v cmake)"
fi

if [ "$PREBUILT" = 1 ]; then
    BUILD="$SOURCE/bin"
else
    step "Building dessa and riv"
    BUILD="$SOURCE/build-release-$(uname -s)"   # one per OS: the folder may be shared with Windows
    LOG="${TMPDIR:-/tmp}/dessa-install.log"
    : > "$LOG"
    cmake -S "$SOURCE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >>"$LOG" 2>&1 || fail "CMake could not configure the build; see $LOG"
    JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
    cmake --build "$BUILD" -j "$JOBS" >>"$LOG" 2>&1 || fail "building dessa failed; see $LOG"
    ok "dessa built"
    cmake --build "$BUILD" --target riv >>"$LOG" 2>&1 || fail "building riv failed; see $LOG"
    ok "riv built"
fi

step "Installing to $PREFIX"
mkdir -p "$PREFIX/bin"
cp "$BUILD/dessa" "$BUILD/riv" "$PREFIX/bin/"
for folder in runtime include; do
    rm -rf "${PREFIX:?}/$folder"
    cp -R "$SOURCE/$folder" "$PREFIX/$folder"
done
rm -rf "${PREFIX:?}/packages"
cp -R "$SOURCE/tools/riv/packages" "$PREFIX/packages"
cp "$SOURCE/LICENSE" "$PREFIX/"
ok "dessa, riv, runtime, include, packages"

step "Commands"
mkdir -p "$LINKS"
ln -sf "$PREFIX/bin/dessa" "$LINKS/dessa"
ln -sf "$PREFIX/bin/riv" "$LINKS/riv"
ok "linked dessa and riv into $LINKS"
case ":$PATH:" in
    *":$LINKS:"*) ok "$LINKS is already on PATH" ;;
    *)
        PROFILE="$HOME/.profile"
        case "$SHELL" in */zsh) PROFILE="$HOME/.zshrc" ;; */bash) PROFILE="$HOME/.bashrc" ;; esac
        if ask "Add $LINKS to PATH in $PROFILE?"; then
            printf '\n# Dessa\nexport PATH="%s:$PATH"\n' "$LINKS" >> "$PROFILE"
            ok "added; open a new terminal to use it"
        else
            warn "add $LINKS to your PATH yourself"
        fi
        ;;
esac

step "Telling riv where the bundled packages are"
"$PREFIX/bin/riv" bundled "$PREFIX/packages" || warn "riv could not note the packages; run: riv bundled $PREFIX/packages"

if [ "$NOWASM" = 0 ]; then
    step "The WebAssembly compiler (for web pages, about 50 MB, once)"
    "$PREFIX/bin/dessa" setup wasm || warn "it couldn't be downloaded now; pages need it, so run later: dessa setup wasm"
fi

VERSION=$("$PREFIX/bin/dessa" --version)
say ""
say "  $VERSION is installed. Open a new terminal and try:"
say "      dessa run hello.ds"
say "      riv init myapp && riv add sql"
say "  Uninstall: sh install.sh --uninstall"
say ""
