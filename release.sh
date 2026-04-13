#!/usr/bin/env bash
#
# LC-1X+ MIDI2DMX — signed, notarised, stapled release build.
#
# Produces in ./dist/:
#   * LC-1X+ MIDI2DMX.component.zip      (AU, drag-install)
#   * LC-1X+ MIDI2DMX.vst3.zip           (VST3, drag-install)
#   * LC-1X+ MIDI2DMX.app.zip            (Standalone, drag-install)
#   * LC-1X+ MIDI2DMX-<version>.pkg      (installer: puts all 3 in the right places)
#
# Prereqs (one-time):
#   1. Developer ID Application cert installed in login keychain.
#   2. Developer ID Installer   cert installed in login keychain.
#   3. Notary profile stored via:
#        xcrun notarytool store-credentials "LC1X_NOTARY" \
#          --apple-id <your-apple-id> --team-id 2N9AC8M66C
#
# Usage:
#   ./release.sh
#
set -euo pipefail

APP_IDENTITY="Developer ID Application: Stephen McLeod Blythe (2N9AC8M66C)"
PKG_IDENTITY="Developer ID Installer: Stephen McLeod Blythe (2N9AC8M66C)"
NOTARY_PROFILE="LC1X_NOTARY"
BUILD_DIR="build-release"
CONFIG="Release"
ARTEFACTS="${BUILD_DIR}/DMXLightController_artefacts/${CONFIG}"
OUT_DIR="dist"
PRODUCT="LC-1X+ MIDI2DMX"
BUNDLE_ID="com.amfas.lc1xplusmidi2dmx"

# Pull version straight out of CMakeLists.txt so pkg filename/identifier match project
VERSION="$(grep -E 'project\(DMXLightController VERSION' CMakeLists.txt \
    | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
if [[ -z "${VERSION}" ]]; then
    echo "!!! Could not parse project VERSION from CMakeLists.txt" >&2
    exit 1
fi
echo ">>> Building ${PRODUCT} v${VERSION}"

rm -rf "${BUILD_DIR}" "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

# ============================================================
# 1. Configure + build (signed via CMake post-build codesign)
# ============================================================
echo ">>> Configuring"
cmake -B "${BUILD_DIR}" -G Xcode \
    -DCMAKE_BUILD_TYPE="${CONFIG}" \
    -DLC1X_CODESIGN_IDENTITY="${APP_IDENTITY}"

echo ">>> Building ${CONFIG}"
cmake --build "${BUILD_DIR}" --config "${CONFIG}"

# ============================================================
# 2. Zip, notarise, staple each bundle (for drag-install users)
# ============================================================
notarise_bundle () {
    local bundle="$1"                 # full path to .component/.vst3/.app
    local name
    name="$(basename "${bundle}")"
    local zip="${OUT_DIR}/${name}.zip"

    echo
    echo ">>> Zipping ${name}"
    ditto -c -k --keepParent "${bundle}" "${zip}"

    echo ">>> Submitting ${name} to notarytool (this can take a few minutes)"
    xcrun notarytool submit "${zip}" \
        --keychain-profile "${NOTARY_PROFILE}" \
        --wait

    echo ">>> Stapling ${name}"
    xcrun stapler staple "${bundle}"

    echo ">>> Gatekeeper assessment for ${name}"
    spctl --assess --type execute --verbose=2 "${bundle}" || true

    # re-zip the now-stapled bundle so the distribution zip contains the ticket
    rm -f "${zip}"
    ditto -c -k --keepParent "${bundle}" "${zip}"
    echo ">>> Done: ${zip}"
}

AU_BUNDLE="${ARTEFACTS}/AU/${PRODUCT}.component"
VST3_BUNDLE="${ARTEFACTS}/VST3/${PRODUCT}.vst3"
APP_BUNDLE="${ARTEFACTS}/Standalone/${PRODUCT}.app"

notarise_bundle "${AU_BUNDLE}"
notarise_bundle "${VST3_BUNDLE}"
notarise_bundle "${APP_BUNDLE}"

# ============================================================
# 3. Build a signed .pkg installer containing all three bundles
# ============================================================
echo
echo ">>> Staging bundles into installer payload"

STAGE="${BUILD_DIR}/pkg-root"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/Library/Audio/Plug-Ins/Components"
mkdir -p "${STAGE}/Library/Audio/Plug-Ins/VST3"
mkdir -p "${STAGE}/Applications"

# ditto preserves bundle attributes, code-sign metadata, symlinks, etc.
ditto "${AU_BUNDLE}"   "${STAGE}/Library/Audio/Plug-Ins/Components/${PRODUCT}.component"
ditto "${VST3_BUNDLE}" "${STAGE}/Library/Audio/Plug-Ins/VST3/${PRODUCT}.vst3"
ditto "${APP_BUNDLE}"  "${STAGE}/Applications/${PRODUCT}.app"

PKG_PATH="${OUT_DIR}/${PRODUCT}-${VERSION}.pkg"
COMPONENT_PLIST="${BUILD_DIR}/component.plist"

# Analyse the payload and then flip BundleIsRelocatable to false on every
# entry. Without this, the macOS Installer's legacy "bundle relocation"
# feature sees an existing bundle with a matching CFBundleIdentifier anywhere
# on disk (e.g. the Standalone in the build folder) and silently redirects
# the install there instead of the pkg's payload path.
echo ">>> Analysing payload to disable bundle relocation"
pkgbuild --analyze --root "${STAGE}" "${COMPONENT_PLIST}"
for i in 0 1 2; do
    plutil -replace "${i}.BundleIsRelocatable" -bool false "${COMPONENT_PLIST}"
done

echo ">>> Building & signing ${PKG_PATH}"
pkgbuild \
    --root "${STAGE}" \
    --component-plist "${COMPONENT_PLIST}" \
    --identifier "${BUNDLE_ID}.pkg" \
    --version "${VERSION}" \
    --install-location "/" \
    --sign "${PKG_IDENTITY}" \
    "${PKG_PATH}"

# ============================================================
# 4. Notarise + staple the .pkg
# ============================================================
echo ">>> Submitting ${PKG_PATH} to notarytool (this can take a few minutes)"
xcrun notarytool submit "${PKG_PATH}" \
    --keychain-profile "${NOTARY_PROFILE}" \
    --wait

echo ">>> Stapling ${PKG_PATH}"
xcrun stapler staple "${PKG_PATH}"

echo ">>> Gatekeeper assessment for ${PKG_PATH}"
spctl --assess --type install --verbose=2 "${PKG_PATH}" || true

# ============================================================
# Done
# ============================================================
echo
echo "============================================================"
echo "All artefacts signed, notarised, and stapled."
echo "Distribution artefacts are in: ${OUT_DIR}/"
echo "============================================================"
ls -la "${OUT_DIR}/"
