#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_DIR="${ROOT_DIR}/AlpacaCore"
HTTP_DIR="${ROOT_DIR}/AlpacaHTTP"
AGENT_DIR="${ROOT_DIR}/AlpacaAgent"
HTTP_BEAST="${ALPACAHTTP_USE_BOOST_BEAST:-OFF}"
CORE_VENDORS="${ALPACACORE_ENABLE_ALL_VENDORS:-ON}"
CORE_WEEWX="${ALPACACORE_ENABLE_WEEWX:-ON}"
INSTALL_UDEV_RULES="${ALPACA_INSTALL_UDEV_RULES:-ON}"

if [[ ! -d "${CORE_DIR}" ]]; then
  echo "AlpacaCore not found at ${CORE_DIR}"
  exit 1
fi

if [[ ! -d "${HTTP_DIR}" ]]; then
  echo "AlpacaHTTP not found at ${HTTP_DIR}"
  exit 1
fi

if [[ ! -d "${AGENT_DIR}" ]]; then
  echo "AlpacaAgent not found at ${AGENT_DIR}"
  exit 1
fi

rm -rf "${CORE_DIR}/build" "${HTTP_DIR}/build"
rm -rf "${AGENT_DIR}/build"

if [[ "${INSTALL_UDEV_RULES}" == "ON" && "${OSTYPE:-}" == "linux"* ]]; then
  RULES_SRC=()
  while IFS= read -r -d '' rule; do
    RULES_SRC+=("${rule}")
  done < <(find "${CORE_DIR}/external" -name "*.rules" -type f -print0 | sort -z)
  for rule in "${RULES_SRC[@]}"; do
    echo "Installing udev rule: ${rule}"
    sudo install -m 644 "${rule}" /etc/udev/rules.d/
  done
  sudo udevadm control --reload-rules
  sudo udevadm trigger
fi

if [[ "${OSTYPE:-}" == "darwin"* ]]; then
  PARALLEL="$(sysctl -n hw.ncpu)"
else
  if command -v nproc >/dev/null 2>&1; then
    PARALLEL="$(nproc)"
  else
    PARALLEL="4"
  fi
fi

build_make() {
  local project_dir="$1"
  local name="$2"

  echo "== ${name} =="
  case "${name}" in
    "AlpacaHTTP")
      cmake -S "${project_dir}" -B "${project_dir}/build" \
        -DALPACAHTTP_USE_BOOST_BEAST="${HTTP_BEAST}" \
        -DALPACACORE_ENABLE_ALL_VENDORS="${CORE_VENDORS}" \
        -DALPACACORE_ENABLE_WEEWX="${CORE_WEEWX}" \
        -DALPACACORE_REQUIRE_WEEWX="${CORE_WEEWX}"
      ;;
    "AlpacaCore")
      cmake -S "${project_dir}" -B "${project_dir}/build" \
        -DALPACACORE_ENABLE_ALL_VENDORS="${CORE_VENDORS}" \
        -DALPACACORE_ENABLE_WEEWX="${CORE_WEEWX}" \
        -DALPACACORE_REQUIRE_WEEWX="${CORE_WEEWX}"
      ;;
    *)
      cmake -S "${project_dir}" -B "${project_dir}/build"
      ;;
  esac
  if [[ -f "${project_dir}/build/Makefile" ]]; then
    make -C "${project_dir}/build" clean 2>&1
    make -C "${project_dir}/build" -j"${PARALLEL}" 2>&1
  else
    cmake --build "${project_dir}/build" --parallel "${PARALLEL}" 2>&1
  fi
}

build_make "${CORE_DIR}" "AlpacaCore"
build_make "${HTTP_DIR}" "AlpacaHTTP"
build_make "${AGENT_DIR}" "AlpacaAgent"

find_executable() {
  local build_dir="$1"
  local binary_name="$2"
  local candidate=""

  for candidate in \
    "${build_dir}/${binary_name}" \
    "${build_dir}/Debug/${binary_name}" \
    "${build_dir}/Release/${binary_name}" \
    "${build_dir}/RelWithDebInfo/${binary_name}" \
    "${build_dir}/MinSizeRel/${binary_name}"; do
    if [[ -x "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  return 1
}

HTTP_BIN="$(find_executable "${HTTP_DIR}/build" "alpacahttp_server" || true)"
if [[ -z "${HTTP_BIN}" ]]; then
  echo "Could not find alpacahttp_server binary in ${HTTP_DIR}/build"
  exit 1
fi

AGENT_PID=""
cleanup() {
  if [[ -n "${AGENT_PID}" ]] && kill -0 "${AGENT_PID}" 2>/dev/null; then
    kill "${AGENT_PID}" 2>/dev/null || true
    wait "${AGENT_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

AGENT_BIN="$(find_executable "${AGENT_DIR}/build" "alpacaagent_server" || true)"
if [[ -z "${AGENT_BIN}" ]]; then
  echo "Could not find alpacaagent_server binary in ${AGENT_DIR}/build"
  exit 1
fi

echo "Starting AlpacaAgent on http://localhost:6810/ ..."
if [[ -f "${AGENT_DIR}/agent_config.json" ]]; then
  "${AGENT_BIN}" --config "${AGENT_DIR}/agent_config.json" &
else
  "${AGENT_BIN}" &
fi
AGENT_PID=$!
sleep 0.3
if ! kill -0 "${AGENT_PID}" 2>/dev/null; then
  echo "AlpacaAgent failed to start"
  exit 1
fi

echo "AlpacaHTTP is running. Open http://localhost:6800/ in your browser."
"${HTTP_BIN}"
exit $?
