# SnkeXR Marker Tracker — Unreal Engine Plugin

Cobra SDK marker tracker integration for Unreal Engine 5.4+. Streams marker pose data from the Cobra gRPC service and updates actor transforms in real-time.

## Plugin structure

```
SnkeXRMarkerTracker/
├── SnkeXRMarkerTracker.uplugin
├── SnkeXRMarkerTracker_UPL.xml          # Android: bundles .so into APK
├── CMakeLists.txt                       # Builds libmarker_tracker_client.so
├── build.sh                             # Linux build script
├── Lib/Android/arm64-v8a/
│   └── libmarker_tracker_client.so      # Pre-built Android .so
└── Source/SnkeXRMarkerTracker/
    ├── SnkeXRMarkerTracker.Build.cs
    ├── Public/
    │   ├── SnkeXRMarkerTrackerModule.h
    │   └── SnkeXRMarkerTrackerComponent.h
    └── Private/
        ├── SnkeXRMarkerTrackerModule.cpp
        ├── SnkeXRMarkerTrackerComponent.cpp
        └── CobraSDKBridge.cpp           # Stubs for editor compilation
```

## Building the .so

On Linux (requires `ANDROID_NDK` or `ANDROID_HOME`, CMake 3.21+, host gcc/g++):

```bash
cd SnkeXRMarkerTracker
./build.sh              # full build
./build.sh --clean      # wipe and rebuild from scratch
```

This reuses the Godot build's host tools (protoc, grpc_cpp_plugin) if available at `../../godot/SnkeXRMarkerTracker/build-host-tools/`, otherwise builds them locally.

Output: `Lib/Android/arm64-v8a/libmarker_tracker_client.so`

## Setup

1. **Build the .so** on Linux using `build.sh` (see above).

2. **Copy or symlink** this plugin folder into your UE project's `Plugins/` directory.

3. **Enable** the plugin in your `.uproject`:
   ```json
   {
       "Name": "SnkeXRMarkerTracker",
       "Enabled": true
   }
   ```

4. **Rebuild** the project.

## Usage

1. Add `SnkeXRMarkerTrackerComponent` to any actor (search "Snke" in the Add Component menu).

2. In the Details panel, set:
   - **Marker ID**: which marker to track (0, 1, 2, etc.). -1 disables tracking.
   - **Server Address**: gRPC target (default `unix-abstract:cobra_grpc`).
   - **Algorithm Mode**: 0 = pose only, 1 = 2D ellipses, 2 = both.
   - **Update Owner Transform**: if true (default), the actor's world transform updates automatically.

3. Optionally bind the **On Marker Received** event in Blueprint to get per-frame data (position, rotation, RMS error, timestamps).

Multiple components can track different markers simultaneously — they share a single gRPC connection and streaming thread.

## Architecture

Mirrors the Godot `SnkeXRMarkerTracker` node:

- First component to start initializes a shared gRPC client via `generalStart()`
- A background streaming thread calls `streamMarkersDirect()` in a loop
- Marker data is distributed to matching components by Marker ID
- Each component drains its buffer on tick, keeps the newest sample
- Last component to exit tears down the client and thread
- Auto-reconnect with 1-second backoff on connection loss

## Coordinate conversion

The plugin converts Cobra SDK poses (right-handed Y-up, meters) to UE space (left-handed Z-up, centimeters) using the same mapping as UE's OpenXR plugin:

```
UE.X = -SDK.Z * WorldToMeters
UE.Y =  SDK.X * WorldToMeters
UE.Z =  SDK.Y * WorldToMeters
```

If poses appear mirrored or rotated, adjust `ConvertToUnrealSpace()` in `SnkeXRMarkerTrackerComponent.cpp`.
