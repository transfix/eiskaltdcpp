#!/usr/bin/env bash
# eiskaltdcpp smoke test, run by cvcpkg on the staged tree before packing: eiskaltdcpp-daemon
# starts with its own config and local dirs, answers JSON-RPC (show.version), and exits 0 on
# daemon.stop and again on SIGTERM. HOME points into a temp dir: without -c the daemon would use ~/.config/eiskaltdc++.
set -euo pipefail
: "${CVC_INSTALL_DIR:?CVC_INSTALL_DIR must be set}"
: "${CVC_DEPS_PREFIX:?CVC_DEPS_PREFIX must be set}"

# The staged tree reaches its dependencies' libraries through the deps prefix, not $ORIGIN, so the
# daemon (only: not curl) runs with it on the loader path.
libpath="${CVC_INSTALL_DIR}/lib:${CVC_DEPS_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
daemon() { env LD_LIBRARY_PATH="$libpath" "${CVC_INSTALL_DIR}/bin/eiskaltdcpp-daemon" "$@"; }
T="$(mktemp -d)"
pid=""
cleanup() { [[ -z "$pid" ]] || kill -KILL "$pid" 2>/dev/null || true; rm -rf "$T"; }
trap cleanup EXIT
export HOME="$T"

[[ "$(daemon -V)" == 2.6.2 ]] || { echo "eiskaltdcpp-daemon -V: $(daemon -V)"; exit 1; }
for f in dcpp/DCPlusPlus.h extra/lunar.h dht/DHT.h proto/nmdcpb.pb.h; do
  test -f "${CVC_INSTALL_DIR}/include/eiskaltdcpp/$f" || { echo "missing include/eiskaltdcpp/$f"; exit 1; }
done

# Start the daemon on a free port and wait until JSON-RPC answers show.version.
start_daemon() {
  port="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')"
  rm -rf "$T/cfg" "$T/local"
  # Not through daemon(): a backgrounded function is a subshell, and $! must be the daemon (env execs).
  env LD_LIBRARY_PATH="$libpath" "${CVC_INSTALL_DIR}/bin/eiskaltdcpp-daemon" -c "$T/cfg/" -l "$T/local/" -L 127.0.0.1 -P "$port" </dev/null >"$T/daemon.log" 2>&1 &
  pid=$!
  local out=""
  for _ in $(seq 1 60); do
    out="$(rpc show.version 2>/dev/null || true)"
    [[ -n "$out" ]] && break
    sleep 0.5
  done
  echo "show.version: $out"
  echo "$out" | grep -q '"result":"2\.6\.2"' || { echo "JSON-RPC did not answer"; cat "$T/daemon.log"; exit 1; }
}
rpc() {
  curl -fsS -X POST -H 'Content-Type: application/json' \
    -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$1\",\"params\":{}}" "http://127.0.0.1:${port}/"
}
# Wait for the daemon to exit; it must exit 0 (v2.6.0 crashed with SIGSEGV here).
expect_clean_exit() {
  local rc=0
  for _ in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
  wait "$pid" || rc=$?
  pid=""
  [[ "$rc" == 0 ]] || { echo "eiskaltdcpp-daemon exited $rc after $1"; cat "$T/daemon.log"; exit 1; }
}

start_daemon
rpc daemon.stop >/dev/null
expect_clean_exit daemon.stop

start_daemon
kill -TERM "$pid"
expect_clean_exit SIGTERM

echo "eiskaltdcpp smoke test OK (port ${port})"
