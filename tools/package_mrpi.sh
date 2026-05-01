#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STAGE_DIR="${ROOT_DIR}/dist/stage"
DIST_DIR="${ROOT_DIR}/dist"
PKG_NAME="kindle-llm-chat-mrpi"

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/extensions/kindle_llm_chat/bin"
mkdir -p "${STAGE_DIR}/extensions/kindle_llm_chat/layouts"
mkdir -p "${DIST_DIR}"

cp "${ROOT_DIR}/builddir_kindlehf/kindle-llm-chat" "${STAGE_DIR}/extensions/kindle_llm_chat/bin/kindle-llm-chat"
cp "${ROOT_DIR}/kindle.pkg/bin/run.sh" "${STAGE_DIR}/extensions/kindle_llm_chat/bin/run.sh"
cp "${ROOT_DIR}/kindle.pkg/menu.json" "${STAGE_DIR}/extensions/kindle_llm_chat/menu.json"
cp "${ROOT_DIR}/kindle.pkg/layouts/"*.xml "${STAGE_DIR}/extensions/kindle_llm_chat/layouts/"

chmod +x "${STAGE_DIR}/extensions/kindle_llm_chat/bin/kindle-llm-chat"
chmod +x "${STAGE_DIR}/extensions/kindle_llm_chat/bin/run.sh"

tar -C "${STAGE_DIR}" -czf "${DIST_DIR}/${PKG_NAME}.tar.gz" .
echo "Created ${DIST_DIR}/${PKG_NAME}.tar.gz"
