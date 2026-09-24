extenders:
  - name: BeaconGraph
    version: 0.1.0
    type: listener
    description: "Microsoft Graph API (OneDrive dead-drop) C2 listener for Adaptix"
    author: stillbigjosh
    min_server_version: "v2.0"
    build:
      - make
    release:
      dir: dist/
