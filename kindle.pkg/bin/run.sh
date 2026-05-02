#!/bin/sh
set -eu

EXTENSION_DIR="/mnt/us/extensions/kindle_llm_chat"
APP_BIN="${EXTENSION_DIR}/bin/kindle-llm-chat"
LAYOUT_DIR="${EXTENSION_DIR}/layouts"

# KUAL does not show stderr; append everything to a log for diagnosis.
mkdir -p "${EXTENSION_DIR}/data"
LOG="${EXTENSION_DIR}/data/kual-launch.log"
exec >>"${LOG}" 2>&1
echo "===== $(date) ====="

DPI="$(sed -n 's/.*(\([0-9]\+\), [0-9]\+).*/\1/p' /var/log/Xorg.0.log | head -n 1 || true)"
if [ -z "${DPI}" ]; then
  DPI=200
fi

if [ "${DPI}" -gt 290 ]; then
  export KINDLE_LLM_CHAT_LAYOUT="${LAYOUT_DIR}/keyboard-300dpi.xml"
else
  export KINDLE_LLM_CHAT_LAYOUT="${LAYOUT_DIR}/keyboard-200dpi.xml"
fi

exec "${APP_BIN}"
