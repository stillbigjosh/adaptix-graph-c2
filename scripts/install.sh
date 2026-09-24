#!/bin/bash
set -e

export PATH="/usr/local/go/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ -z "$1" ]; then
    echo "Usage: $0 /path/to/AdaptixC2"
    echo "  Installs the Graph API C2 channel into an Adaptix C2 installation."
    exit 1
fi

ADAPTIX_DIR="$1"

if [ ! -d "$ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent" ]; then
    echo "Error: $ADAPTIX_DIR does not appear to be a valid Adaptix C2 directory."
    echo "Expected: $ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent/"
    exit 1
fi

AGENT_DIR="$ADAPTIX_DIR/AdaptixServer/extenders/beacon_agent"
BEACON_SRC="$AGENT_DIR/src_beacon/beacon"
DIST_DIR="$ADAPTIX_DIR/dist"
BACKUP_DIR="$ADAPTIX_DIR/.dist_backup_$$"

echo "[*] Installing Graph API C2 channel for Adaptix..."

# --- Phase 1: Backup entire dist/ ---
# make server-ext runs 'clean' which does rm -rf dist/
# We preserve everything the user had: certs, profile, database, custom pages, etc.

if [ -d "$DIST_DIR" ]; then
    echo "[+] Backing up dist/..."
    cp -a "$DIST_DIR" "$BACKUP_DIR"
    echo "    Saved to $BACKUP_DIR"
fi

# --- Phase 2: Patch source tree ---

echo "[+] Copying ConnectorGraph source files..."
cp "$PROJECT_DIR/beacon/ConnectorGraph.h"   "$BEACON_SRC/ConnectorGraph.h"
cp "$PROJECT_DIR/beacon/ConnectorGraph.cpp" "$BEACON_SRC/ConnectorGraph.cpp"
echo "    Copied to $BEACON_SRC/"

echo "[+] Applying C++ patches..."

apply_patch() {
    local patch_file="$1"
    local patch_name="$(basename "$patch_file")"

    if patch --dry-run -p1 -d "$ADAPTIX_DIR" < "$patch_file" > /dev/null 2>&1; then
        patch -p1 -d "$ADAPTIX_DIR" < "$patch_file"
        echo "    Applied: $patch_name"
    else
        echo "    Skipped (already applied or conflict): $patch_name"
    fi
}

apply_patch "$PROJECT_DIR/patches/agent_main_connector.patch"
apply_patch "$PROJECT_DIR/patches/agent_config_h.patch"
apply_patch "$PROJECT_DIR/patches/agent_config_cpp.patch"
apply_patch "$PROJECT_DIR/patches/config_cpp.patch"
apply_patch "$PROJECT_DIR/patches/agent_beat.patch"

echo "[+] Applying AX script patches..."
apply_patch "$PROJECT_DIR/patches/ax_config_graph_commands.patch"

echo "[+] Applying Go agent patches..."
apply_patch "$PROJECT_DIR/patches/agent_generate_profiles.patch"
apply_patch "$PROJECT_DIR/patches/agent_build_payload.patch"

echo "[+] Registering BeaconGraph in beacon agent config..."
AGENT_CONFIG="$AGENT_DIR/config.yaml"
if [ -f "$AGENT_CONFIG" ] && ! grep -q "BeaconGraph" "$AGENT_CONFIG"; then
    sed -i '/BeaconDNS/a\  - "BeaconGraph"' "$AGENT_CONFIG"
    echo "    Added BeaconGraph to listeners list"
else
    echo "    Already registered"
fi

# --- Phase 3: Build listener plugin ---

echo "[+] Building listener plugin..."
cd "$PROJECT_DIR/listener"

# Auto-detect GOEXPERIMENT flags from the server binary so the plugin matches exactly.
# Go plugins require identical build flags, including experimental features.
if [ -f "$DIST_DIR/adaptixserver" ]; then
    SERVER_GOFLAGS=$(go version "$DIST_DIR/adaptixserver" 2>/dev/null | grep -oP 'X:\K\S+' || true)
    if [ -n "$SERVER_GOFLAGS" ]; then
        export GOEXPERIMENT="$SERVER_GOFLAGS"
        echo "    Detected GOEXPERIMENT=$GOEXPERIMENT from server binary"
    fi
fi

go mod tidy
make build
echo "    Built successfully."

# --- Phase 4: Rebuild server + extenders ---

echo "[+] Rebuilding Adaptix server and extenders..."
cd "$ADAPTIX_DIR"
make server-ext

# --- Phase 4b: Build objects_graph ---
# make server-ext builds objects_http/smb/tcp/dns but doesn't know about graph.
# We compile all beacon sources with -DBEACON_GRAPH into dist/extenders/beacon_agent/objects_graph/.

echo "[+] Building objects_graph (all beacon sources with -DBEACON_GRAPH)..."
bash "$SCRIPT_DIR/build_objects.sh" "$ADAPTIX_DIR"

# --- Phase 5: Restore user files over the fresh build ---
# The build recreated dist/ with fresh binaries, plugins, profile, and 404page.
# We overlay the user's originals so their certs, profile, database, and
# any custom files survive. Extenders are left as the fresh build produced
# them since the source tree was just patched.

if [ -d "$BACKUP_DIR" ]; then
    echo "[+] Restoring user files..."
    for f in "$BACKUP_DIR"/*; do
        name="$(basename "$f")"
        [ "$name" = "extenders" ] && continue
        cp -a "$f" "$DIST_DIR/"
    done
    rm -rf "$BACKUP_DIR"
    echo "    Restored certs, profile, database, and custom files"
fi

# --- Phase 5b: Ensure SSL certs exist ---
# The profile.yaml references cert/key files. If they don't exist (first install,
# or backup didn't contain them), generate self-signed certs so the server starts.

CERT_FILE=$(grep -oP '^\s*cert:\s*"\K[^"]+' "$DIST_DIR/profile.yaml" 2>/dev/null || echo "server.rsa.crt")
KEY_FILE=$(grep -oP '^\s*key:\s*"\K[^"]+' "$DIST_DIR/profile.yaml" 2>/dev/null || echo "server.rsa.key")

if [ ! -f "$DIST_DIR/$CERT_FILE" ] || [ ! -f "$DIST_DIR/$KEY_FILE" ]; then
    echo "[+] SSL certs missing, generating self-signed..."
    openssl req -x509 -newkey rsa:4096 \
        -keyout "$DIST_DIR/$KEY_FILE" \
        -out "$DIST_DIR/$CERT_FILE" \
        -days 365 -nodes -subj "/CN=localhost" 2>/dev/null
    echo "    Generated $CERT_FILE and $KEY_FILE"
fi

# --- Phase 6: Install listener plugin ---

echo "[+] Installing listener plugin..."
PLUGIN_DIR="$DIST_DIR/extenders/beacon_listener_graph"
mkdir -p "$PLUGIN_DIR"
cp "$PROJECT_DIR/listener/beacon_listener_graph.so" "$PLUGIN_DIR/"
cp "$PROJECT_DIR/listener/config.yaml" "$PLUGIN_DIR/"
cp "$PROJECT_DIR/listener/ax_config.axs" "$PLUGIN_DIR/"

if ! grep -q "beacon_listener_graph" "$DIST_DIR/profile.yaml" 2>/dev/null; then
    sed -i '/beacon_listener_dns/a\    - "extenders/beacon_listener_graph/config.yaml"' "$DIST_DIR/profile.yaml"
fi
echo "    Installed to $PLUGIN_DIR/"

# --- Phase 7: Restart service ---

echo "[+] Restarting Adaptix service..."
if systemctl is-active --quiet adaptix 2>/dev/null; then
    systemctl restart adaptix
    sleep 2
    if systemctl is-active --quiet adaptix 2>/dev/null; then
        echo "    Adaptix restarted successfully"
    else
        echo "    Warning: Adaptix failed to start. Check: journalctl -u adaptix"
    fi
elif command -v systemctl > /dev/null 2>&1 && systemctl list-unit-files adaptix.service > /dev/null 2>&1; then
    systemctl start adaptix
    sleep 2
    echo "    Adaptix started"
else
    echo "    No systemd service found. Restart the server manually."
fi

echo ""
echo "[*] Installation complete!"
echo "    Create a 'BeaconGraph' listener in the Adaptix GUI."
