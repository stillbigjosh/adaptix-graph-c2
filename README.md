# Adaptix Graph C2

Cloud dead-drop C2 channel plugin for [Adaptix C2](https://github.com/Adaptix-Framework/AdaptixC2) v1.2.

Beacons communicate through Azure cloud storage instead of direct HTTP callbacks. All traffic is outbound HTTPS to Microsoft endpoints. No public IP or inbound ports needed.

Supports two storage backends:
- **Azure Blob Storage** (recommended)
- **OneDrive** (Microsoft Graph API)

**For authorized red team and penetration testing use only.**

For a full walkthrough with screenshots, see the [deployment guide](https://stillbigjosh.github.io/writeup.html?file=writeups/adaptix-graph-c2.md).

## Prerequisites

- Adaptix C2 v1.2 installed and built
- Go 1.25.4+ (must match the server binary version)
- MinGW cross-compilers (`x86_64-w64-mingw32-g++`, `i686-w64-mingw32-g++`)
- At least 2 CPU cores and 2 GB RAM for the build

## Install

```bash
git clone https://github.com/stillbigjosh/adaptix-graph-c2.git
cd adaptix-graph-c2
./scripts/install.sh /path/to/AdaptixC2
```

The script does everything: patches the source, builds the listener plugin, rebuilds the server, compiles beacon objects, and restarts the service.

For LXC containers:

```bash
tar czf /tmp/adaptix-graph-c2.tar.gz -C /path/to adaptix-graph-c2
cat /tmp/adaptix-graph-c2.tar.gz | pct exec <id> -- tar xzf - -C /opt/
pct exec <id> -- bash /opt/adaptix-graph-c2/scripts/install.sh /opt/AdaptixC2
```

## After Install

1. Open the Adaptix Client.
2. Go to **Listeners > Create Listener**.
3. Set Protocol to `external (graph)`, Config to `BeaconGraph`.
4. Set Storage Backend to `blob` or `onedrive` and fill in the required fields.
5. Click **Create**, then **Start** the listener.
6. Generate a beacon payload from the graph listener and deploy it.

## Uninstall

```bash
./scripts/uninstall.sh /path/to/AdaptixC2
```

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `go: not found` | The script adds `/usr/local/go/bin` to PATH. If Go is elsewhere: `export PATH="/your/go/path/bin:$PATH"` |
| `missing go.sum entry` | Run `cd listener && go mod tidy`, then re-run install |
| All commands "Command not found" | Make sure `ax_config_graph_commands.patch` was applied: `grep BeaconGraph dist/extenders/beacon_agent/ax_config.axs` |
| Beacon hits `login.microsoftonline.com` in blob mode | Re-apply `agent_generate_profiles.patch` and regenerate the payload |
| `objects_graph/config.cpp` missing | Run `bash scripts/build_objects.sh /path/to/AdaptixC2` |
| `agent_kharon` build error | Pre-existing Adaptix issue, does not affect this plugin |
| 403 on blob ops | SAS must use version `2020-10-02` without `signedEncryptionScope` |
| No callback | Target cannot reach `{account}.blob.core.windows.net:443` |
| Build OOM / slow | Use at least 2 cores, 2 GB RAM. LXC: `pct set <id> -cores 4 -memory 2048` |

## Project Structure

```
listener/       Go listener plugin (.so)
beacon/         C++ connector (ConnectorGraph)
patches/        8 additive patches for the beacon agent
scripts/        install.sh, build_objects.sh, uninstall.sh
```
