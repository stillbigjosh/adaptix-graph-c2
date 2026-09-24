# Adaptix Graph C2

A cloud dead-drop C2 channel plugin for [Adaptix C2](https://github.com/Adaptix-Framework/AdaptixC2) v1.2.

This plugin adds a `graph` protocol to the Adaptix beacon agent. The beacon and listener communicate through cloud storage. They upload and download files to exchange data. All traffic goes through standard HTTPS to Azure services. This makes the C2 traffic look like normal cloud activity.

The plugin supports two storage backends:

- **Azure Blob Storage** (recommended). Simple to set up. No Microsoft 365 license necessary.
- **OneDrive** (Microsoft Graph API). Traffic blends with normal Microsoft 365 file activity.

You do not need a public IP address or inbound firewall ports. The listener and beacon both make outbound HTTPS requests only.

**Use this tool only for authorized red team operations and penetration testing.**

## How It Works

```
BEACON   --[upload c_{nonce}.dat]-->  Cloud Storage
LISTENER <--[poll for c_*.dat]-----  Cloud Storage
LISTENER --[upload r_{nonce}.dat]--> Cloud Storage
BEACON   <--[poll for r_{nonce}.dat] Cloud Storage
```

1. The beacon encrypts its data with RC4 and uploads it as a file (e.g., `c_abc123.dat`).
2. The listener polls the storage container for new checkin files.
3. The listener downloads the checkin file and processes it.
4. The listener uploads a response file (e.g., `r_abc123.dat`) that contains tasking data.
5. The beacon polls for and downloads its response file.
6. Both sides delete processed files after use.

## Before You Start

Make sure you have the following:

- **Adaptix C2 v1.2** installed and built (with `make server-ext` or `make all`).
- **Go** (version 1.25.4 or higher). The Go version must match the server binary exactly.
- **Go in your PATH**. The install script adds `/usr/local/go/bin` automatically.
- **MinGW cross-compilers**: `x86_64-w64-mingw32-g++` and `i686-w64-mingw32-g++`.
- **At least 2 CPU cores and 2 GB of RAM.** Go compilation uses a lot of memory. If you run the server in an LXC container, set the resources before you start the install:

```bash
pct set <container-id> -cores 4 -memory 2048
```

## Step 1: Set Up the Azure Backend

You must set up a cloud storage backend before you install the plugin. Choose one of the two options below.

### Option A: Azure Blob Storage (Recommended)

1. Go to **Azure Portal > Storage accounts > Create**.
2. Set Performance to **Standard** and Redundancy to **LRS**.
3. Enter a unique lowercase name for the storage account (e.g., `mystorageacct`).
4. Click **Create** and wait for the deployment to complete.
5. Go to your new Storage Account.
6. Go to **Security + networking > Access keys**.
7. Copy the value of **Key1**. You will need this later.
8. Choose a container name (e.g., `c2-data`). The listener creates the container automatically at startup.

Save these two values. You will enter them when you create the listener:
- Storage Account name
- Key1 (base64 access key)

### Option B: OneDrive (Microsoft Graph API)

1. Go to **Azure Portal > Entra ID > App registrations > New registration**.
2. Enter any name (e.g., `adaptix-graph`).
3. Set "Supported account types" to **Accounts in this organizational directory only**.
4. Leave the Redirect URI blank.
5. Click **Register**.

6. On the app's **Overview** page, copy these two values:
   - **Application (client) ID**
   - **Directory (tenant) ID**

7. Go to **API permissions > Add a permission > Microsoft Graph > Application permissions**.
8. Search for `Files.ReadWrite.All` and select it.
9. Click **Add permissions**.
10. Click **Grant admin consent for [your org]**. You need Global Admin rights for this step.
11. Make sure the status column shows **Granted**.

12. Go to **Certificates & secrets > Client secrets > New client secret**.
13. Enter any description. Set the expiry period.
14. Click **Add**.
15. Copy the **Value** immediately. You cannot get this value again after you leave the page. Do not copy the "Secret ID".

16. Go to **Entra ID > Users**. Select the user whose OneDrive will store the dead-drop files.
17. Copy the **User principal name** (e.g., `user@contoso.com`) or the **Object ID** (GUID).
18. Make sure this user has an active OneDrive. If not, sign in to office.com once with that account to provision it.

Save these four values. You will enter them when you create the listener:
- Tenant ID
- Client ID
- Client Secret
- User ID or User Principal Name

## Step 2: Install the Plugin

Clone the repository and run the install script:

```bash
git clone <this-repo> adaptix-graph-c2
cd adaptix-graph-c2
./scripts/install.sh /path/to/AdaptixC2
```

The install script does all the work in one run. It completes these steps in order:

1. Makes a backup of the `dist/` directory (certificates, profile, database, and custom files).
2. Copies the ConnectorGraph C++ source files into the beacon source tree.
3. Applies 8 patches to the beacon agent code (C++, Go, and AX script files).
4. Runs `go mod tidy` and builds the listener plugin (`beacon_listener_graph.so`).
5. Runs `make server-ext` to rebuild the Adaptix server and all extender plugins.
6. Cross-compiles all beacon source files with `-DBEACON_GRAPH` to build the `objects_graph` directory.
7. Restores all user files from the backup. Your certificates, database, and profile are safe.
8. Installs the graph listener plugin into `dist/extenders/beacon_listener_graph/`.
9. Adds the plugin to `profile.yaml`.
10. Restarts the Adaptix service.

The script is safe to run more than once. It skips patches that are already applied.

**Note:** During Step 5, you may see an error about `agent_kharon` and `github.com/google/uuid`. This is a pre-existing issue in the Adaptix source tree. It does not affect the graph plugin. The build continues past this error.

## Step 3: Create the Listener

1. Open the Adaptix Client and connect to your server.
2. Go to **Listeners > Create Listener**.
3. In the Protocol dropdown, select **external (graph)**.
4. In the Config dropdown, select **BeaconGraph**.
5. Fill in the fields for your storage backend.

### Blob Mode Fields

| Field | What to Enter |
|-------|---------------|
| Storage Backend | `blob` |
| Storage Account | The name of your Azure storage account |
| Storage Key | The base64 access key (Key1) from Step 1 |
| Container Name | The container name you chose (e.g., `c2-data`) |
| Poll Interval | `5` (seconds between listener polls) |
| Encrypt Key | Leave empty. The server generates a key automatically. |
| Beacon Poll Attempts | `15` (how many times the beacon polls for a response) |
| Beacon Poll Interval (ms) | `3000` (milliseconds between beacon polls) |

### OneDrive Mode Fields

| Field | What to Enter |
|-------|---------------|
| Storage Backend | `onedrive` |
| Tenant ID | Your Azure AD tenant GUID from Step 1 |
| Client ID | Your app registration client ID from Step 1 |
| Client Secret | Your app secret value from Step 1 |
| User ID / UPN | The OneDrive user email or object ID from Step 1 |
| OneDrive Folder | A folder path (e.g., `/adaptix/c2`). The listener creates the folder automatically. |
| Poll Interval | `5` |
| Encrypt Key | Leave empty |
| Beacon Poll Attempts | `15` |
| Beacon Poll Interval (ms) | `3000` |

6. Click **Create**.
7. Select the new listener and click **Start**.

## Step 4: Generate a Beacon Payload

1. Go to **Payloads > Generate Payload**.
2. Select the graph listener you created in Step 3.
3. Set the architecture to **x64** (or x86 if the target is 32-bit).
4. Set the format (Exe, Service Exe, DLL, or Shellcode).
5. Set the sleep time and jitter as required.
6. Click **Generate**.
7. Download the beacon file.

## Step 5: Deploy and Get a Callback

1. Transfer the beacon file to a Windows target.
2. Run the beacon on the target.
3. The beacon uploads a checkin file to Azure.
4. The listener detects the new file and downloads it.
5. The agent appears in the Adaptix GUI. This usually takes a few seconds. The exact time depends on the poll interval.
6. You can now send tasks to the agent through the Adaptix interface. All commands work the same as HTTP beacons: `shell`, `whoami`, BOFs, file operations, pivoting, etc.

## Uninstall

To remove the graph plugin, run:

```bash
./scripts/uninstall.sh /path/to/AdaptixC2
```

This script reverses all patches, removes the connector files, and uninstalls the listener plugin.

## Configuration Reference

### Common Fields (All Modes)

| Field | Description | Default |
|-------|-------------|---------|
| Storage Backend | Set to `onedrive` or `blob` | `onedrive` |
| Poll Interval | How often the listener checks for new files (in seconds) | `5` |
| Encrypt Key | A 32-character hex key for RC4 encryption | Auto-generated |
| Beacon Poll Attempts | How many times the beacon polls for its response file | `15` |
| Beacon Poll Interval (ms) | Time between beacon poll attempts (in milliseconds) | `3000` |

### Azure Blob Storage Fields

| Field | Description |
|-------|-------------|
| Storage Account | The name of your Azure Storage Account |
| Storage Key | The base64-encoded access key (Key1) |
| Container Name | The blob container name. The listener creates it if it does not exist. |

### OneDrive Fields

| Field | Description |
|-------|-------------|
| Tenant ID | Your Azure AD tenant GUID |
| Client ID | The application (client) ID from your app registration |
| Client Secret | The secret value from your app registration |
| User ID / UPN | The OneDrive user (email address or object ID) |
| OneDrive Folder | The folder path for dead-drop files |

## How the SAS Token System Works

The plugin uses SAS (Shared Access Signature) tokens for Azure Blob Storage authentication. The beacon never receives the raw storage key.

- The **listener** generates an Account SAS token with a 24-hour expiry. It refreshes this token automatically in the background.
- The **beacon** receives a Container SAS token with a 90-day expiry. This token is embedded in the beacon's profile data at build time.
- The SAS version is `2020-10-02`.

## File Format

The beacon and listener exchange data through two types of files.

**Checkin files** (`c_{nonce}.dat`), uploaded by the beacon:
```
[4 bytes: beat length as big-endian uint32]
[N bytes: base64-encoded, RC4-encrypted beat data (agent type, ID, and system info)]
[M bytes: RC4-encrypted task output body]
```

**Response files** (`r_{nonce}.dat`), uploaded by the listener:
```
[RC4-encrypted tasking data from the teamserver]
```

## Troubleshooting

### `go: not found` during install

**Cause:** Go is not in the system PATH.

The install script adds `/usr/local/go/bin` to PATH automatically. If your Go installation is in a different location, add it to PATH before you run the script:

```bash
export PATH="/your/go/path/bin:$PATH"
```

### `missing go.sum entry` during listener build

**Cause:** The Go dependency checksums are missing.

The install script runs `go mod tidy` automatically. If this error still occurs, run this command manually:

```bash
cd /path/to/adaptix-graph-c2/listener
go mod tidy
```

Then run the install script again.

### All commands show "Command not found" on graph beacon

**Cause:** The server-side AX script does not register commands for the BeaconGraph listener type.

Make sure the `ax_config_graph_commands.patch` was applied. Run this command to verify:

```bash
grep BeaconGraph /path/to/AdaptixC2/dist/extenders/beacon_agent/ax_config.axs
```

You should see at least two lines with `BeaconGraph`. If you see no output, re-run the install script.

### Beacon connects to `login.microsoftonline.com` in blob mode

**Cause:** The profile data has a serialization error. The beacon reads the wrong value for the storage type and runs in OneDrive mode.

Make sure the `agent_generate_profiles.patch` does NOT add an extra `params = append(params, 0)` line for the listener type. The common profile header field `lWatermark` already contains the listener type value. An extra zero shifts all subsequent fields by 4 bytes.

If you see this behavior, re-apply the patch and rebuild the beacon agent plugin:

```bash
cd /path/to/AdaptixC2
patch -p1 < /path/to/adaptix-graph-c2/patches/agent_generate_profiles.patch
cd AdaptixServer/extenders/beacon_agent
make
cp agent_beacon_new.so ../../dist/extenders/beacon_agent/agent_beacon.so
systemctl restart adaptix
```

Then generate a new beacon payload.

### `objects_graph/config.cpp: No such file or directory` during payload generation

**Cause:** The `objects_graph` directory was not built or was deleted.

Run the build script manually:

```bash
bash /path/to/adaptix-graph-c2/scripts/build_objects.sh /path/to/AdaptixC2
```

The install script runs this step automatically after `make server-ext`.

### `agent_kharon` build error during `make server-ext`

**Cause:** The Kharon agent has a missing Go dependency (`github.com/google/uuid`). This is a pre-existing issue in the Adaptix source tree.

This error does not affect the graph plugin. The build continues past it. All other extenders compile correctly.

If you use the Kharon agent and want to fix this error:

```bash
cd /path/to/AdaptixC2/AdaptixServer/extenders/agent_kharon
go get github.com/google/uuid
```

### 403 AuthenticationFailed on blob operations

**Cause:** The SAS token signature does not match.

Make sure the SAS string-to-sign uses version `2020-10-02`. This version does NOT include the `signedEncryptionScope` field. That field was added in version `2020-12-06`. If the field is included, the signature will not match and Azure will reject the request.

### Listener starts but no beacon callbacks arrive

**Cause:** The beacon cannot reach the Azure endpoint.

Make sure the target machine can connect to these hosts on port 443:
- For blob mode: `{your-account}.blob.core.windows.net`
- For OneDrive mode: `login.microsoftonline.com` and `graph.microsoft.com`

Use Wireshark or a proxy to verify the beacon makes outbound HTTPS connections to these hosts.

### Patches fail to apply

**Cause:** The Adaptix source tree has changed since the patches were created.

The install script skips patches that are already applied. If a patch fails because of changed context lines, you must update the patch offsets manually. Compare the patch file with the current source file and adjust the `@@` line numbers.

### Server fails to start: "file does not exists" for cert/key

**Cause:** The SSL certificate files are missing after the rebuild.

The install script generates self-signed certificates automatically if they are missing. If this error still occurs, generate the certificates manually:

```bash
cd /path/to/AdaptixC2/dist
openssl req -x509 -newkey rsa:4096 \
    -keyout server.rsa.key \
    -out server.rsa.crt \
    -days 365 -nodes -subj "/CN=localhost"
systemctl restart adaptix
```

### Build takes too long or gets killed (OOM)

**Cause:** The system does not have enough resources for Go compilation.

Use at least 2 CPU cores and 2 GB of RAM. If you run the server in a Proxmox LXC container:

```bash
pct shutdown <container-id>
pct set <container-id> -cores 4 -memory 2048
pct start <container-id>
```

## Project Structure

```
adaptix-graph-c2/
  listener/                          Go listener plugin (compiled to .so)
    config.yaml                      Plugin metadata (protocol: "graph")
    pl_main.go                       Plugin interface, SAS generation, Create/Start/Stop
    pl_transport.go                  Dual-mode transport (Graph API + Blob Storage)
    ax_config.axs                    Listener GUI form with storage type selector
    Makefile                         Builds the plugin with: go build -buildmode=plugin
    go.mod                           Go module (requires axc2 v1.2.0)

  beacon/                            C++ connector (copied into beacon source tree)
    ConnectorGraph.h                 Class definition, ProfileGraph struct, GRAPHFUNC API table
    ConnectorGraph.cpp               WinHTTP implementation for OneDrive and Blob Storage

  patches/                           Additive patches for the existing beacon agent
    agent_main_connector.patch       Adds #elif BEACON_GRAPH block in MainAgent.cpp
    agent_config_h.patch             Adds ProfileGraph struct to AgentConfig.h
    agent_config_cpp.patch           Adds profile parsing with storage_type branching
    config_cpp.patch                 Adds BEACON_GRAPH case in config.cpp
    agent_beat.patch                 Adds BEACON_GRAPH to the BuildBeat() conditional
    ax_config_graph_commands.patch   Registers all commands for BeaconGraph listener type
    agent_generate_profiles.patch    Adds "graph" case in GenerateProfiles()
    agent_build_payload.patch        Adds "graph" case and ObjectDir in BuildPayload()

  scripts/
    install.sh                       Full automated installation (one command)
    build_objects.sh                  Cross-compiles all beacon .o files with -DBEACON_GRAPH
    uninstall.sh                     Removes all graph plugin components
```

## License

This project is provided as-is for authorized security testing purposes only.
