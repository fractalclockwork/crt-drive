#!/bin/sh
# Start and stop the long-running host page for crt_vtty.
# The pid file is the session leader (uv). Stop signals that process group,
# and also any host still holding the Pico serial node.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

pidfile=.tmp/vtty-host.pid
namefile=.tmp/vtty-host.name
log=.tmp/vtty-host.log

usage() {
    echo "usage: vtty_host.sh start [name] | stop [--quiet] | status" >&2
    exit 2
}

alive() {
    [ -f "$pidfile" ] || return 1
    pid=$(cat "$pidfile" 2>/dev/null || true)
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null
}

serial_node() {
    ls -1 /dev/serial/by-id/usb-Raspberry_Pi_Pico*-if00 2>/dev/null | head -1 || true
}

# Pids with the CDC node open, one per line.
serial_holders() {
    node=$(serial_node)
    [ -n "$node" ] || return 0
    # "node:  <pid>..." — drop the path so ttyACM0 does not become pid 0.
    fuser "$node" 2>/dev/null | sed 's/.*://' | tr ' ' '\n' | sed '/^$/d' || true
}

is_host_cmd() {
    case "$1" in
        *cursorsandbox*|*py_compile*)
            return 1
            ;;
    esac
    case "$1" in
        "uv run python tools/"*.py|"sg dialout -c "*tools/*.py*)
            return 0
            ;;
        */python3\ tools/*.py|*/python\ tools/*.py)
            return 0
            ;;
    esac
    return 1
}

# uv and the page itself, even when the serial node is closed between retries.
host_pids() {
    ps -eo pid=,args= | while read -r pid args; do
        if is_host_cmd "$args"; then
            printf '%s\n' "$pid"
        fi
    done
}

# SIGTERM is ignored by processes started under this desktop session, so stop
# uses SIGKILL on the host pid and the children it spawned (uv's python).
signal_tree() {
    pid=$1
    [ "$pid" = "$$" ] && return 0
    for child in $(ps -o pid= --ppid "$pid" 2>/dev/null); do
        signal_tree "$child"
    done
    /bin/kill -s KILL "$pid" 2>/dev/null || true
}

stop_one() {
    pid=$1
    cmd=$(ps -o args= -p "$pid" 2>/dev/null || true)
    if ! is_host_cmd "$cmd"; then
        return 0
    fi
    signal_tree "$pid"
}

collect_stop_pids() {
    if alive; then
        cat "$pidfile"
    fi
    host_pids
    serial_holders
}

do_stop() {
    quiet=0
    if [ "${1:-}" = "--quiet" ]; then
        quiet=1
    fi
    pids=$(collect_stop_pids | awk 'NF && !seen[$0]++')
    if [ -z "$pids" ]; then
        rm -f "$pidfile" "$namefile"
        if [ "$quiet" -eq 0 ]; then
            echo "vtty host is not running"
        fi
        return 0
    fi
    if [ -f "$namefile" ]; then
        echo "stopping vtty host $(cat "$namefile")"
    else
        echo "stopping vtty host"
    fi
    for pid in $pids; do
        stop_one "$pid" || true
    done
    i=0
    while [ "$i" -lt 20 ]; do
        left=$(collect_stop_pids | awk 'NF && !seen[$0]++')
        [ -z "$left" ] && break
        sleep 0.1
        i=$((i + 1))
    done
    left=$(collect_stop_pids | awk 'NF && !seen[$0]++' || true)
    for pid in $left; do
        stop_one "$pid" || true
    done
    rm -f "$pidfile" "$namefile"
}

do_start() {
    host=${1:-vtty_host_news}
    case "$host" in
        *[!A-Za-z0-9_-]*|"")
            echo "vtty host: bad name '$host'" >&2
            exit 1
            ;;
    esac
    script="tools/$host.py"
    if [ ! -f "$script" ]; then
        echo "vtty host: no $script" >&2
        exit 1
    fi
    if alive; then
        echo "vtty host already running: $(cat "$namefile" 2>/dev/null || echo host) pid $(cat "$pidfile")" >&2
        exit 1
    fi
    busy=$( { host_pids; serial_holders; } | awk 'NF && !seen[$0]++')
    if [ -n "$busy" ]; then
        echo "vtty host: already running (pid $busy). Run make vtty-stop first." >&2
        exit 1
    fi
    mkdir -p .tmp
    rm -f "$pidfile"
    echo "--- $host $(date -Is) ---" >> "$log"
    # The child is the session leader. It records its pid, then becomes uv.
    setsid -f sh -c 'echo $$ > "$1"; exec uv run python "$2"' sh "$pidfile" "$script" \
        >> "$log" 2>&1 < /dev/null
    i=0
    while [ ! -s "$pidfile" ] && [ "$i" -lt 30 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    sleep 0.3
    if ! alive; then
        echo "vtty host failed to start; see $log" >&2
        exit 1
    fi
    printf '%s\n' "$host" > "$namefile"
    echo "started vtty host $host (pid $(cat "$pidfile")); log $log"
}

do_status() {
    if alive; then
        echo "running $(cat "$namefile" 2>/dev/null || echo host) pid $(cat "$pidfile")"
        return 0
    fi
    echo "stopped"
}

cmd=${1:-}
shift || true
case "$cmd" in
    start) do_start "${1:-vtty_host_news}" ;;
    stop) do_stop "${1:-}" ;;
    status) do_status ;;
    *) usage ;;
esac
