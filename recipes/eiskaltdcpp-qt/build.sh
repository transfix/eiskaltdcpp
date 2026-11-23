#!/usr/bin/env bash
# eiskaltdcpp-qt (transfix fork): the Qt 6 desktop client, with the core library linked in
# statically. Linux and macOS; Windows is build.ps1.
set -euo pipefail

: "${CVC_SOURCE_DIR:?CVC_SOURCE_DIR must be set}"
: "${CVC_BUILD_DIR:?CVC_BUILD_DIR must be set}"
: "${CVC_INSTALL_DIR:?CVC_INSTALL_DIR must be set}"
: "${CVC_DEPS_PREFIX:?CVC_DEPS_PREFIX must be set}"

CVC_JOBS="${CVC_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
case "$(echo "${CVC_BUILD_TYPE:-release}" | tr '[:upper:]' '[:lower:]')" in
  debug) BUILD_TYPE=Debug ;;
  *) BUILD_TYPE=Release ;;
esac

# protoc comes with cvcpkg's protobuf, in the deps prefix; cmake and ninja with their packages.
export PATH="${CVC_DEPS_PREFIX}/bin:${PATH}"
prefix_path="${CVC_DEPS_PREFIX}"
if [[ -n "${CVC_BUILD_PREFIX:-}" && "${CVC_BUILD_PREFIX}" != "${CVC_DEPS_PREFIX}" ]]; then
  export PATH="${CVC_BUILD_PREFIX}/bin:${PATH}"
  prefix_path="${CVC_DEPS_PREFIX};${CVC_BUILD_PREFIX}"
fi

common=(
  -G Ninja -S "${CVC_SOURCE_DIR}" -B "${CVC_BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DCMAKE_INSTALL_PREFIX="${CVC_INSTALL_DIR}"
  -DCMAKE_INSTALL_LIBDIR=lib
  -DCMAKE_PREFIX_PATH="${prefix_path}"
  # The same features as the eiskaltdcpp recipe's core.
  -DLUA_SCRIPT=ON -DWITH_DHT=ON -DWITH_NMDCPB=ON
  -DUSE_MINIUPNP=ON -DPERL_REGEX=ON -DUSE_IDN2=ON -DUSE_XATTR=OFF -DLOCAL_JSONCPP=ON
  # The Qt 6 client only. cvcpkg's Qt has no QML/Quick (JavaScript scripting) or Linguist tools
  # (lrelease, for the GUI's translations: linguas is empty); the GUI's spell checker needs
  # aspell, which cvcpkg lacks too.
  -DUSE_QT6=ON -DUSE_QT_QML=OFF -DUSE_JS=OFF -DUSE_QT_SQLITE=ON -DUSE_ASPELL=OFF
  -DUSE_GTK3=OFF -DJSONRPC_DAEMON=OFF -DXMLRPC_DAEMON=OFF -DNO_UI_DAEMON=OFF
  -DUSE_CLI_JSONRPC=OFF -DUSE_CLI_XMLRPC=OFF
  -DWITH_DEV_FILES=OFF -DEISKALTDCPP_SDK=OFF
  -DWITH_EMOTICONS=ON -DWITH_SOUNDS=ON -DWITH_EXAMPLES=OFF -DWITH_LUASCRIPTS=ON
  -Dlinguas=
  -DUSE_CCACHE=OFF -DBUILD_TESTS=OFF -DCOMPRESS_MANPAGES=OFF -DINSTALL_METAINFO=OFF
)

case "$(uname -s)" in
  Linux)
    # The core is linked into the GUI (the daemon's libeiskaltdcpp.so is a separate package).
    # Desktop notifications go over D-Bus (cvcpkg's QtDBus loads the host's libdbus-1).
    cmake "${common[@]}" \
      -DSTATIC_CORE=ON -DDBUS_NOTIFY=ON \
      -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' -DCMAKE_BUILD_WITH_INSTALL_RPATH=OFF \
      -DINSTALL_RUNTIME_PATH=OFF
    ;;
  Darwin)
    # The core is static on macOS anyway. EiskaltDC++.app goes at the top of the prefix; it finds
    # Qt's frameworks and the other dylibs in the prefix's lib/ (cvcpkg's Qt ships no
    # macdeployqt, so the install step's call to it does nothing). gettext's msgfmt and libintl
    # are cvcpkg's.
    cmake "${common[@]}" \
      -DDBUS_NOTIFY=OFF -DCMAKE_INSTALL_RPATH='@executable_path/../../../lib' \
      -DGETTEXT_SEARCH_PATH="${CVC_DEPS_PREFIX}" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-13.0}"
    ;;
  *) echo "eiskaltdcpp-qt: unsupported build host $(uname -s)" >&2; exit 1 ;;
esac
cmake --build "${CVC_BUILD_DIR}" -j "${CVC_JOBS}"
cmake --install "${CVC_BUILD_DIR}"

grep -q 'WITH_NMDCPB:BOOL=ON' "${CVC_BUILD_DIR}/CMakeCache.txt" || {
  echo "eiskaltdcpp-qt: NMDCpb was switched off at configure time (protobuf not found)" >&2; exit 1; }

if [[ "$(uname -s)" == Darwin ]]; then
  app="${CVC_INSTALL_DIR}/EiskaltDC++.app/Contents/MacOS/EiskaltDC++"
  test -x "$app" || { echo "eiskaltdcpp-qt: no $app after the install" >&2; exit 1; }
  # bin/eiskaltdcpp-qt, as on Linux: runs the application bundle's executable.
  install -d "${CVC_INSTALL_DIR}/bin"
  cat > "${CVC_INSTALL_DIR}/bin/eiskaltdcpp-qt" <<'SH'
#!/bin/sh
here="$(cd "$(dirname "$0")" && pwd -P)"
exec "$here/../EiskaltDC++.app/Contents/MacOS/EiskaltDC++" "$@"
SH
  chmod 0755 "${CVC_INSTALL_DIR}/bin/eiskaltdcpp-qt"
fi

if [[ "$(uname -s)" == Linux ]]; then
  # The core's message catalogues come with the eiskaltdcpp package, a runtime dependency; two
  # packages must not install the same file.
  rm -rf "${CVC_INSTALL_DIR}/share/locale"
fi
echo "cvcpkg: eiskaltdcpp-qt -> ${CVC_INSTALL_DIR}"
