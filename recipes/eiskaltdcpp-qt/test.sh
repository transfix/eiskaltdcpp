#!/usr/bin/env bash
# eiskaltdcpp-qt smoke test, run by cvcpkg on the staged tree before packing (and by the publish
# workflow on an install from cvcpkg.org). On Qt's offscreen platform, with a config directory of
# its own: the GUI prints its version, starts, loads its icons and resources, and exits 0 with the
# core shut down and its settings saved when a second `eiskaltdcpp-qt --quit` asks it to; started
# with --quit it exits 0 as soon as it is up. On Linux it runs once more from a copy of bin/ and
# share/ elsewhere, with the staged share/eiskaltdcpp hidden, which only finds its data through
# its relocatable data paths (transfix/eiskaltdcpp#12).
#
# Linux, macOS (cvcpkg runs it with /bin/bash 3.2 there) and Windows (Git Bash).
set -euo pipefail
: "${CVC_INSTALL_DIR:?CVC_INSTALL_DIR must be set}"
: "${CVC_DEPS_PREFIX:?CVC_DEPS_PREFIX must be set}"

case "$(uname -s)" in
  Linux) os=linux ;;
  Darwin) os=macos ;;
  MINGW* | MSYS* | CYGWIN*) os=windows ;;
  *) echo "eiskaltdcpp-qt test: unsupported host $(uname -s)"; exit 1 ;;
esac

# Physical paths (macOS's temp dir is /var -> /private/var).
T="$(cd "$(mktemp -d)" && pwd -P)"
pid=""
hidden=""
cleanup() {
  [[ -z "$pid" ]] || kill -KILL "$pid" 2>/dev/null || true
  [[ -z "$hidden" || ! -d "$hidden" ]] || mv "$hidden" "${CVC_INSTALL_DIR}/share/eiskaltdcpp"
  rm -rf "$T"
}
trap cleanup EXIT

# Its own config, temp dir and single-instance key: the instance lock is keyed on $LOGNAME
# (Linux, macOS), so this never reaches someone's running EiskaltDC++.
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_DATA_HOME="$T/home/.local/share"
export TMPDIR="$T/tmp" LOGNAME="cvcpkg-test-$$" QT_QPA_PLATFORM=offscreen
mkdir -p "$HOME" "$TMPDIR"
cfg="$XDG_CONFIG_HOME/eiskaltdc++"

# The staged tree reaches its dependencies through the deps prefix, not its rpaths, unless the
# two are one prefix (after an install); Qt's plugins (the offscreen platform) likewise.
root="$(cd "$CVC_INSTALL_DIR" && pwd -P)"
deps="$(cd "$CVC_DEPS_PREFIX" && pwd -P)"
export QT_PLUGIN_PATH="$deps/plugins"
case "$os" in
  linux)
    export LD_LIBRARY_PATH="$root/lib:$deps/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    exe=bin/eiskaltdcpp-qt
    ;;
  macos)
    # macOS drops DYLD_* from the environment of anything it protects (/bin/bash, the launcher's
    # interpreter), so the staged tree is copied into a prefix of its own whose lib/ also links
    # every dependency's dylib and framework, the layout an install gives it.
    if [[ "$root" != "$deps" ]]; then
      mkdir "$T/root"
      cp -R "$root/." "$T/root/"
      root="$T/root"
      mkdir -p "$root/lib"
      for lib in "$deps"/lib/*.dylib "$deps"/lib/*.framework; do
        [[ -e "$lib" ]] || continue
        [[ -e "$root/lib/${lib##*/}" || -L "$root/lib/${lib##*/}" ]] || ln -s "$lib" "$root/lib/"
      done
    fi
    exe=bin/eiskaltdcpp-qt
    ;;
  windows)
    # EiskaltDC++.exe finds Qt's and the other DLLs beside it or on PATH. It runs from a copy
    # with a dcppboot.xml that puts its settings in $cfg (local mode), never in %APPDATA%.
    export PATH="$root/bin:$deps/bin:$PATH"
    mkdir -p "$T/app/bin"
    cp "$root/bin/EiskaltDC++.exe" "$T/app/bin/"
    cp -R "$root/bin/resources" "$T/app/bin/"
    mkdir -p "$cfg"
    printf '<?xml version="1.0" encoding="utf-8" standalone="yes"?>\n<Boot>\n\t<LocalMode>1</LocalMode>\n\t<ConfigPath>%s\\</ConfigPath>\n</Boot>\n' \
      "$(cygpath -w "$cfg")" > "$T/app/bin/dcppboot.xml"
    root="$T/app"
    exe=bin/EiskaltDC++.exe
    ;;
esac

for f in "$exe" share/licenses/eiskaltdcpp-qt/COPYING; do
  [[ -e "${CVC_INSTALL_DIR}/$f" ]] || { echo "missing $f"; exit 1; }
done
case "$os" in
  linux) data=share/eiskaltdcpp/qt/client-res/default.rcc ;;
  macos) data="EiskaltDC++.app/Contents/Resources/client-res/default.rcc" ;;
  windows) data=bin/resources/client-res/default.rcc ;;
esac
[[ -e "${CVC_INSTALL_DIR}/$data" ]] || { echo "missing $data"; exit 1; }

version="$("$root/$exe" -V 2>/dev/null </dev/null | tr -d '\r' | tail -1)"
[[ "$version" == 2.6.3* ]] || { echo "eiskaltdcpp-qt -V: '$version'"; exit 1; }
echo "eiskaltdcpp-qt -V: $version"

# Wait for the process to exit; it must exit 0, and have shut the core down. Its stdout is a
# file, so block-buffered: the log is only read once it has exited.
expect_clean_exit() {
  local rc=0 i
  for i in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
  if kill -0 "$pid" 2>/dev/null; then echo "eiskaltdcpp-qt still running after $1"; exit 1; fi
  wait "$pid" || rc=$?
  pid=""
  [[ "$rc" == 0 ]] || { echo "eiskaltdcpp-qt exited $rc after $1"; cat "$T/gui.log"; exit 1; }
  grep -q '^Quit\.\.\.' "$T/gui.log" || { echo "eiskaltdcpp-qt did not shut the core down after $1"; cat "$T/gui.log"; exit 1; }
}

# Start the GUI from PREFIX and wait until the core is up (it creates its config directory
# after the instance lock), then ask it to quit from a second instance. Its main window must
# have loaded the icons and resources, and it must have saved its settings.
run_gui() {
  local prefix="$1" i
  rm -rf "$cfg"/* "$HOME/.local/share/eiskaltdc++"
  [[ "$os" == windows ]] || rm -rf "$cfg"
  "$prefix/$exe" </dev/null >"$T/gui.log" 2>&1 &
  pid=$!
  for i in $(seq 1 60); do
    [[ -e "$cfg/DCPlusPlus.xml" || ( "$os" != windows && -d "$cfg" ) ]] && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
  done
  sleep 3
  kill -0 "$pid" 2>/dev/null || { echo "eiskaltdcpp-qt ($prefix) exited at start"; cat "$T/gui.log"; exit 1; }
  "$prefix/$exe" --quit </dev/null >"$T/quit.log" 2>&1 || {
    echo "eiskaltdcpp-qt --quit failed"; cat "$T/quit.log"; exit 1; }
  expect_clean_exit "eiskaltdcpp-qt --quit ($prefix)"
  grep -q 'Application icons has been loaded' "$T/gui.log" || {
    echo "eiskaltdcpp-qt ($prefix) did not load its icons"; cat "$T/gui.log"; exit 1; }
  [[ -s "$cfg/EiskaltDC++_Qt.conf" && -s "$cfg/DCPlusPlus.xml" ]] || {
    echo "eiskaltdcpp-qt ($prefix) saved no settings"; ls -la "$cfg"; exit 1; }
}

run_gui "$root"
echo "started and quit ($root)"

# Started with --quit on its own command line, it quits as soon as it is up.
"$root/$exe" --quit </dev/null >"$T/gui.log" 2>&1 &
pid=$!
expect_clean_exit "starting with --quit"
echo "started with --quit and quit"

if [[ "$os" == linux ]]; then
  # A relocated copy: the compiled-in data paths point into the staged tree (or the build
  # machine's prefix, after an install), which it must not need.
  mkdir -p "$T/reloc"
  cp -a "$root/bin" "$root/share" "$T/reloc/"
  hidden="${CVC_INSTALL_DIR}/share/eiskaltdcpp.hidden-by-test"
  mv "${CVC_INSTALL_DIR}/share/eiskaltdcpp" "$hidden"
  run_gui "$T/reloc"
  mv "$hidden" "${CVC_INSTALL_DIR}/share/eiskaltdcpp"
  hidden=""
  echo "started and quit from a relocated copy"
fi

echo "eiskaltdcpp-qt smoke test OK"
