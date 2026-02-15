#!/usr/bin/env bash
# THIS IS A TESTING SCRIPT.
# To run lxwm, exit your current WM and return to your display manager after installation.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DISPLAY_NUM="${DISPLAY_NUM:-:1}"
SCREEN_GEOM="${SCREEN_GEOM:-1280x800}"
MONITORS="${MONITORS:-1}"
LOG_FILE="${LOG_FILE:-/tmp/lxwm.log}"

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
  -d, --display <display>    Xephyr display (default: ${DISPLAY_NUM})
  -g, --geometry <WxH>       Per-monitor geometry (default: ${SCREEN_GEOM})
  -m, --monitors <count>     Number of virtual monitors (default: ${MONITORS})
  -h, --help                 Show this help

Examples:
  $0
  $0 -m 2
  $0 --display :2 --geometry 1600x900 --monitors 3
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
    -d | --display)
        DISPLAY_NUM="${2:-}"
        shift 2
        ;;
    -g | --geometry)
        SCREEN_GEOM="${2:-}"
        shift 2
        ;;
    -m | --monitors)
        MONITORS="${2:-}"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
    esac
done

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd make
need_cmd Xephyr
need_cmd xdpyinfo

if [ ! -f "${ROOT_DIR}/src/main.c" ] || [ ! -f "${ROOT_DIR}/Makefile" ]; then
    echo "Run this script from the project root." >&2
    exit 1
fi

if ! [[ "${MONITORS}" =~ ^[0-9]+$ ]] || [ "${MONITORS}" -lt 1 ]; then
    echo "Invalid monitor count: ${MONITORS} (must be >= 1)" >&2
    exit 1
fi

if ! [[ "${SCREEN_GEOM}" =~ ^([0-9]+)x([0-9]+)$ ]]; then
    echo "Invalid geometry: ${SCREEN_GEOM} (expected WxH, e.g. 1280x800)" >&2
    exit 1
fi

SCREEN_W="${BASH_REMATCH[1]}"
SCREEN_H="${BASH_REMATCH[2]}"

XEPHYR_ARGS=("${DISPLAY_NUM}")
XEPHYR_ARGS+=(+xinerama)
XEPHYR_ARGS+=(-screen "${SCREEN_W}x${SCREEN_H}")
for ((i = 1; i < MONITORS; i++)); do
    offset_x=$((i * SCREEN_W))
    XEPHYR_ARGS+=(-origin "${offset_x},0" -screen "${SCREEN_W}x${SCREEN_H}")
done
XEPHYR_ARGS+=(-ac -br -noreset)

echo "[1/4] Building..."
make -C "${ROOT_DIR}"

echo "[2/4] Starting Xephyr on ${DISPLAY_NUM} (${MONITORS} monitor(s), ${SCREEN_GEOM} each)..."
Xephyr "${XEPHYR_ARGS[@]}" >/tmp/lxwm-xephyr.log 2>&1 &
XEPHYR_PID=$!

cleanup() {
    set +e
    if [ -n "${WM_PID:-}" ] && kill -0 "${WM_PID}" 2>/dev/null; then
        kill "${WM_PID}" 2>/dev/null
    fi
    if kill -0 "${XEPHYR_PID}" 2>/dev/null; then
        kill "${XEPHYR_PID}" 2>/dev/null
    fi
}
trap cleanup EXIT INT TERM

echo "[3/4] Waiting for Xephyr to become ready..."
for _ in $(seq 1 50); do
    if DISPLAY="${DISPLAY_NUM}" xdpyinfo >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

if ! DISPLAY="${DISPLAY_NUM}" xdpyinfo >/dev/null 2>&1; then
    echo "Xephyr failed to start. Check /tmp/lxwm-xephyr.log" >&2
    exit 1
fi

if [ ! -f "$HOME/.config/lxwm/config" ]; then
    echo "[info] No config at ~/.config/lxwm/config; creating from config.example"
    mkdir -p "$HOME/.config/lxwm"
    cp "${ROOT_DIR}/config.example" "$HOME/.config/lxwm/config"
fi

echo "[4/4] Starting lxwm on ${DISPLAY_NUM} (logs: ${LOG_FILE})..."
DISPLAY="${DISPLAY_NUM}" "${ROOT_DIR}/lxwm" >"${LOG_FILE}" 2>&1 &
WM_PID=$!
sleep 0.3

if ! kill -0 "${WM_PID}" 2>/dev/null; then
    echo "lxwm exited immediately. Check ${LOG_FILE}" >&2
    exit 1
fi

echo
echo "Xephyr is running on ${DISPLAY_NUM}."
echo "Virtual monitors: ${MONITORS} (${SCREEN_GEOM} each)"
echo "Xephyr Xinerama: enabled"
echo "Logs: ${LOG_FILE} and /tmp/lxwm-xephyr.log"
echo "Press Ctrl+C in this terminal to stop everything."

wait "${XEPHYR_PID}"
