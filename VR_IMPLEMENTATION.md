# VR Camera Implementation for RecoilEngine

## Overview

This implementation adds basic VR camera support to RecoilEngine using OpenXR, enabling rendering to VR headsets like the Valve Index. The implementation focuses on the bare minimum to get stereo rendering working with HMD pose tracking.

## What Was Implemented

### 1. OpenXR Integration (CMake)
- Added OpenXR SDK detection in `CMakeLists.txt`
- Linked OpenXR loader library to engine
- Added `USE_VR` preprocessor definition when OpenXR is found
- Made VR support optional - engine compiles without OpenXR

### 2. VR System Manager (`rts/Rendering/VR/`)
- **VRSystem.h/cpp**: Core VR system management
  - OpenXR instance, session, and swapchain creation
  - HMD pose tracking (position + orientation)
  - Per-eye framebuffer management with depth buffers
  - View and projection matrix calculation from HMD FOV
  - Stereo rendering loop coordination

### 3. Camera System Extensions (`rts/Game/Camera.h/cpp`)
- Added `CAMTYPE_VR_LEFT` and `CAMTYPE_VR_RIGHT` camera types
- Implemented `UpdateMatricesVR()` to accept VR-provided view/projection matrices
- Allows VR system to drive camera position and orientation directly

### 4. Rendering Loop Modification (`rts/Game/Game.cpp`)
- Modified `CGame::Draw()` to support VR stereo rendering
- When VR is active:
  1. Waits for HMD poses from OpenXR
  2. Renders left eye to swapchain framebuffer
  3. Renders right eye to swapchain framebuffer
  4. Submits both eyes to OpenXR compositor
  5. Restores default framebuffer for UI rendering
- Standard rendering path remains unchanged when VR is inactive

### 5. VR Initialization (`rts/Rendering/GlobalRendering.cpp` & `rts/Game/Game.cpp`)
- Initializes VR system (instance and system) after OpenGL context creation in `GlobalRendering.cpp`
- Creates global `g_VRSystem` instance but does **not** create OpenXR session yet
- Session creation is **delayed** until the game loads into a battle/match
- In `CGame::StartPlaying()`, the OpenXR session is started once the game is ready
- Cleans up VR system on shutdown

## Building

### Requirements
- **OpenXR SDK**: Download from https://github.com/KhronosGroup/OpenXR-SDK
- **OpenXR Runtime**: SteamVR (for Valve Index) or another OpenXR-compatible runtime

### Linux Build
```bash
# Install OpenXR SDK (example for Ubuntu)
sudo apt-get install libopenxr-dev

# Configure with CMake
cd RecoilEngine
mkdir build && cd build
cmake .. -DUSE_VR=ON

# Build
make -j$(nproc)
```

### Windows Build
```bash
# Install OpenXR SDK via vcpkg or manually
vcpkg install openxr-loader

# Configure with CMake
cmake .. -DUSE_VR=ON -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build . --config Release
```

## Testing with Valve Index

1. **Install SteamVR** - Provides OpenXR runtime for Valve Index
2. **Connect HMD** - Ensure Index is connected and tracked
3. **Start SteamVR** - Must be running before launching engine
4. **Launch Engine** - VR system will initialize (instance/system) but session won't start yet
5. **Load into a Battle** - Once the game loads into a match, the OpenXR session will start automatically
6. **Check logs** - Look for `[VR]` prefixed messages in console, including "VR session started for battle"

## Known Limitations

This is a **minimal implementation** focused on getting something visible in VR:

- **No VR input handling** - No controller support, only HMD tracking
- **No UI in VR** - UI still renders to desktop window only
- **No performance optimizations** - Renders full scene twice per frame
- **No seated/standing selection** - Defaults to STAGE space, falls back to LOCAL
- **No runtime enable/disable** - VR session starts when game loads and cannot be toggled during gameplay
- **No configuration options** - Uses default OpenXR settings
- **Desktop mirror limited** - Desktop window shows only last rendered view

## Architecture Notes

### Rendering Flow (VR Mode)
```
GlobalRendering::CreateWindowAndContext()
  └─> g_VRSystem->Initialize() - Creates OpenXR instance and system (NO session yet)

CGame::StartPlaying() - Called when game loads into battle
  └─> g_VRSystem->StartSession() - Creates OpenXR session, swapchains, framebuffers

CGame::Draw()
  └─> g_VRSystem->WaitGetPoses()
      └─> For each eye (LEFT, RIGHT):
          ├─> SetupEyeCamera() - Binds eye framebuffer
          ├─> Update camera with VR matrices
          ├─> worldDrawer.Draw() - Renders scene
          └─> SubmitEyeTexture() - Releases swapchain
      └─> Present() - Submits to OpenXR compositor
```

### Key Classes
- **CVRSystem**: OpenXR wrapper, manages HMD and swapchains
- **CCamera**: Extended to accept VR matrices
- **CGame::Draw()**: Orchestrates stereo rendering loop

### Platform-Specific Code
- **Windows**: Uses `XrGraphicsBindingOpenGLWin32KHR`
- **Linux**: Uses `XrGraphicsBindingOpenGLXlibKHR`

## Future Work

To make this production-ready, consider adding:

1. **VR Controller Input** - OpenXR action system integration
2. **VR UI** - Render UI to in-world panels or HUD
3. **Performance Optimization**:
   - Culling optimization for VR FOV
   - Instanced stereo rendering
   - Fixed foveated rendering
   - Reduced quality settings for VR
4. **Configuration Options** - Enable/disable VR, quality presets
5. **Comfort Features** - Vignette, snap turning, teleport locomotion
6. **Debug Visualization** - Show controller rays, boundaries
7. **Runtime VR toggle** - Switch between VR and desktop at runtime

## Troubleshooting

### VR doesn't initialize
- Check that OpenXR runtime (SteamVR) is running
- Verify HMD is connected and tracked
- Look for `[VR]` error messages in logs
- Ensure OpenXR SDK was found during CMake configuration

### Performance issues
- VR requires maintaining 90 FPS minimum (Valve Index)
- Consider reducing graphics quality settings
- Check GPU usage - rendering twice per frame is demanding
- Disable expensive effects like advanced water/shadows

### Black screen in HMD
- Check that framebuffers are being created correctly
- Verify swapchain format matches OpenXR requirements
- Ensure camera matrices are being updated properly
- Look for OpenGL errors in logs

## Files Modified/Added

### Added Files
- `rts/Rendering/VR/VRSystem.h` - VR system interface
- `rts/Rendering/VR/VRSystem.cpp` - VR system implementation

### Modified Files
- `CMakeLists.txt` - OpenXR SDK detection
- `rts/CMakeLists.txt` - Link OpenXR library
- `rts/Rendering/CMakeLists.txt` - Add VR source files
- `rts/Game/Camera.h` - Add VR camera types and UpdateMatricesVR()
- `rts/Game/Camera.cpp` - Implement UpdateMatricesVR()
- `rts/Game/Game.cpp` - Add VR rendering path to Draw()
- `rts/Rendering/GlobalRendering.cpp` - Initialize/shutdown VR system

## References

- [OpenXR Specification](https://www.khronos.org/registry/OpenXR/)
- [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK)
- [Valve Index Specifications](https://www.valvesoftware.com/en/index/headset)
