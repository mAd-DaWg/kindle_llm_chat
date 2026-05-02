#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STAGE_DIR="${ROOT_DIR}/dist/stage"
DIST_DIR="${ROOT_DIR}/dist"
PKG_NAME="kindle-llm-chat-mrpi"

# Default matches README cross-build: meson setup … builddir_kindlehf
# Override: ./tools/package_mrpi.sh my_build_dir   OR   KINDLE_LLM_CHAT_BUILD_DIR=my_build_dir ./tools/package_mrpi.sh
BUILD_DIR="${1:-${KINDLE_LLM_CHAT_BUILD_DIR:-builddir_kindlehf}}"
BIN="${ROOT_DIR}/${BUILD_DIR}/kindle-llm-chat"

if [ ! -f "${BIN}" ]; then
  echo "package_mrpi: missing ${BIN}" >&2
  echo "  Build the Kindle binary first (e.g. meson compile -C ${BUILD_DIR}), or pass the Meson build dir as the first argument." >&2
  exit 1
fi

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}/extensions/kindle_llm_chat/bin"
mkdir -p "${STAGE_DIR}/extensions/kindle_llm_chat/layouts"
mkdir -p "${DIST_DIR}"

cp "${BIN}" "${STAGE_DIR}/extensions/kindle_llm_chat/bin/kindle-llm-chat"
cp "${ROOT_DIR}/kindle.pkg/bin/run.sh" "${STAGE_DIR}/extensions/kindle_llm_chat/bin/run.sh"
cp "${ROOT_DIR}/kindle.pkg/config.xml" "${STAGE_DIR}/extensions/kindle_llm_chat/config.xml"
cp "${ROOT_DIR}/kindle.pkg/menu.json" "${STAGE_DIR}/extensions/kindle_llm_chat/menu.json"
cp "${ROOT_DIR}/kindle.pkg/layouts/"*.xml "${STAGE_DIR}/extensions/kindle_llm_chat/layouts/"

chmod +x "${STAGE_DIR}/extensions/kindle_llm_chat/bin/kindle-llm-chat"
chmod +x "${STAGE_DIR}/extensions/kindle_llm_chat/bin/run.sh"

tar -C "${STAGE_DIR}" -czf "${DIST_DIR}/${PKG_NAME}.tar.gz" .
echo "Created ${DIST_DIR}/${PKG_NAME}.tar.gz (from ${BUILD_DIR}/kindle-llm-chat)"
