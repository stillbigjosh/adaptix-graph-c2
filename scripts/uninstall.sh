#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ -z "$1" ]; then
    echo "Usage: $0 /path/to/AdaptixC2"
    echo "  Removes the Graph API C2 channel from an Adaptix C2 installation."
    exit 1
fi

ADAPTIX_DIR="$1"

if [ ! -d "$ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent" ]; then
    echo "Error: $ADAPTIX_DIR does not appear to be a valid Adaptix C2 directory."
    exit 1
fi

AGENT_DIR="$ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent"
BEACON_SRC="$AGENT_DIR/src_beacon/beacon"

echo "[*] Uninstalling Graph API C2 channel..."

# Step 1: Reverse patches
echo "[+] Reversing patches..."

reverse_patch() {
    local patch_file="$1"
    local patch_name="$(basename "$patch_file")"

    if patch --dry-run -R -p1 -d "$ADAPTIX_DIR" < "$patch_file" > /dev/null 2>&1; then
        patch -R -p1 -d "$ADAPTIX_DIR" < "$patch_file"
        echo "    Reversed: $patch_name"
    else
        echo "    Skipped (not applied or conflict): $patch_name"
    fi
}

reverse_patch "$PROJECT_DIR/patches/agent_build_payload.patch"
reverse_patch "$PROJECT_DIR/patches/agent_generate_profiles.patch"
reverse_patch "$PROJECT_DIR/patches/config_cpp.patch"
reverse_patch "$PROJECT_DIR/patches/agent_config_cpp.patch"
reverse_patch "$PROJECT_DIR/patches/agent_config_h.patch"
reverse_patch "$PROJECT_DIR/patches/agent_main_connector.patch"

# Step 2: Remove connector source files
echo "[+] Removing ConnectorGraph source files..."
rm -f "$BEACON_SRC/ConnectorGraph.h"
rm -f "$BEACON_SRC/ConnectorGraph.cpp"

# Step 3: Remove objects directory
echo "[+] Removing objects_graph directory..."
rm -rf "$AGENT_DIR/objects_graph"

# Step 4: Uninstall listener plugin
echo "[+] Uninstalling listener plugin..."
if command -v axtool > /dev/null 2>&1; then
    axtool ext uninstall BeaconGraph 2>/dev/null || true
    echo "    Uninstalled via axtool."
else
    echo "    Warning: axtool not found. Remove the BeaconGraph plugin manually."
fi

echo ""
echo "[*] Uninstall complete. Restart the Adaptix server to apply changes."
