#!/usr/bin/env bash
# eiskaltdcpp (transfix fork): the core library, its headers and the JSON-RPC daemon. No GUI.
set -euo pipefail

: "${CVC_SOURCE_DIR:?CVC_SOURCE_DIR must be set}"
: "${CVC_BUILD_DIR:?CVC_BUILD_DIR must be set}"
: "${CVC_INSTALL_DIR:?CVC_INSTALL_DIR must be set}"
: "${CVC_DEPS_PREFIX:?CVC_DEPS_PREFIX must be set}"

CVC_JOBS="${CVC_JOBS:-$(nproc 2>/dev/null || echo 4)}"
case "$(echo "${CVC_BUILD_TYPE:-release}" | tr '[:upper:]' '[:lower:]')" in
  debug) BUILD_TYPE=Debug ;;
  *) BUILD_TYPE=Release ;;
esac

# protoc comes with cvcpkg's protobuf, in the deps prefix; the configure looks for it on PATH.
export PATH="${CVC_DEPS_PREFIX}/bin:${PATH}"
prefix_path="${CVC_DEPS_PREFIX}"
if [[ -n "${CVC_BUILD_PREFIX:-}" && "${CVC_BUILD_PREFIX}" != "${CVC_DEPS_PREFIX}" ]]; then
  prefix_path="${CVC_DEPS_PREFIX};${CVC_BUILD_PREFIX}"
fi

# These three change the layout of public classes; build-flags.txt below records them for
# consumers (eiskaltdcpp-py). Keep the two lists in step.
ABI_FLAGS=(-DLUA_SCRIPT=ON -DWITH_DHT=ON -DWITH_NMDCPB=ON)

cmake -G Ninja -S "${CVC_SOURCE_DIR}" -B "${CVC_BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${CVC_INSTALL_DIR}" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="${prefix_path}" \
  -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=OFF \
  -DEISKALTDCPP_SDK=ON \
  "${ABI_FLAGS[@]}" \
  -DUSE_QT6=OFF -DUSE_GTK3=OFF -DUSE_ASPELL=OFF -DDBUS_NOTIFY=OFF \
  -DJSONRPC_DAEMON=ON -DXMLRPC_DAEMON=OFF -DNO_UI_DAEMON=OFF \
  -DUSE_CLI_JSONRPC=OFF -DUSE_CLI_XMLRPC=OFF \
  -DUSE_MINIUPNP=ON -DPERL_REGEX=ON -DUSE_IDN2=ON -DUSE_XATTR=OFF \
  -DLOCAL_JSONCPP=ON \
  -DWITH_DEV_FILES=ON \
  -DWITH_EMOTICONS=OFF -DWITH_SOUNDS=OFF -DWITH_EXAMPLES=OFF -DWITH_LUASCRIPTS=ON \
  -DINSTALL_RUNTIME_PATH=OFF -DUSE_CCACHE=OFF -DBUILD_TESTS=OFF \
  -DCOMPRESS_MANPAGES=OFF -DINSTALL_METAINFO=OFF
cmake --build "${CVC_BUILD_DIR}" -j "${CVC_JOBS}"
cmake --install "${CVC_BUILD_DIR}"

grep -q 'WITH_NMDCPB:BOOL=ON' "${CVC_BUILD_DIR}/CMakeCache.txt" || {
  echo "eiskaltdcpp: NMDCpb was switched off at configure time (protobuf not found)" >&2; exit 1; }

# WITH_DEV_FILES installs dcpp/*.h only. The public headers also include extra/, dht/ and the
# generated proto/nmdcpb.pb.h (the fork's own SDK workflow stages extra/ by hand too).
inc="${CVC_INSTALL_DIR}/include/eiskaltdcpp"
install -d "${inc}/extra" "${inc}/dht" "${inc}/proto"
install -m 0644 "${CVC_SOURCE_DIR}"/extra/*.h "${inc}/extra/"
install -m 0644 "${CVC_SOURCE_DIR}"/dht/*.h "${inc}/dht/"
install -m 0644 "${CVC_BUILD_DIR}"/dcpp/proto/*.pb.h "${inc}/proto/"

share="${CVC_INSTALL_DIR}/share/eiskaltdcpp"
install -d "${share}"
cat > "${share}/build-flags.txt" <<TXT
# eiskaltdcpp ${CVC_PACKAGE_VERSION:-2.6.2}: compile definitions a consumer of include/eiskaltdcpp needs
LUA_SCRIPT
WITH_DHT
WITH_NMDCPB
TXT
echo "cvcpkg: eiskaltdcpp -> ${CVC_INSTALL_DIR}"
