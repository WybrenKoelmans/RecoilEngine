# VR Stereo Rendering Implementation for RecoilEngine

## Overview
This implementation adds full OpenXR-based VR stereo rendering support to RecoilEngine, compatible with SteamVR 1.0 and tested on Valve Index HMD. The VR rendering path is completely separated from desktop rendering and can be enabled via the `VRMode` configuration option.

## Architecture

### Core Components

1. **OpenXRManager** (`rts/System/Platform/OpenXRManager.{h,cpp}`)
   - Low-level OpenXR wrapper managing instance, session, and swapchains
   - Handles HMD pose tracking and per-eye view/projection matrices
   - Uses `XR_REFERENCE_SPACE_TYPE_LOCAL` (seated VR mode)
   - Supports both Windows (WGL) and Linux (GLX) OpenGL contexts
   - Comprehensive error logging with L_ERROR on failures

2. **VRRenderer** (`rts/Rendering/VRRenderer.{h,cpp}`)
   - High-level VR rendering coordinator
   - Manages per-eye framebuffer objects with depth renderbuffers
   - Implements complete frame loop: BeginFrame() → RenderEye() × 2 → EndFrame()
   - Switches active camera between left/right eye for each render pass
   - Calls existing `WorldDrawer::Draw()` to reuse all rendering code

3. **Camera System Extensions** (`rts/Game/Camera.{h,cpp}`)
   - Added `CAMTYPE_VR_LEFT` and `CAMTYPE_VR_RIGHT` camera types
   - `ApplyVRTransform()` method inherits player camera state and applies VR matrices
   - VR cameras track player camera movements with HMD pose overlaid on top

4. **Game Loop Integration** (`rts/Game/Game.{h,cpp}`)
   - VR initialization in `PostLoadRendering()`
   - Runtime branching in `Draw()` based on `GlobalRendering::vrEnabled`
   - Desktop rendering path remains completely unchanged
   - VR cleanup in `KillRendering()`

5. **Configuration** (`rts/Rendering/GlobalRendering.{h,cpp}`)
   - `vrEnabled` flag initialized from config `VRMode` setting
   - Controls VR/desktop rendering path selection

6. **Build System** (`rts/CMakeLists.txt`)
   - `USE_OPENXR` CMake option (default OFF)
   - Automatic OpenXR library detection with manual fallback
   - Links OpenXR only when enabled

## Technical Details

### IPD-Based Stereo Rendering
- OpenXR runtime provides separate view matrices for left/right eyes
- View matrices already include:
  - HMD position and orientation
  - Per-eye offset (IPD separation)
- Engine uses these matrices directly without additional calculations

### Camera Inheritance
- Player camera continues normal updates from game controllers
- VR cameras read final player camera state each frame
- `ApplyVRTransform()` copies player state then overrides with VR matrices
- This allows player movement while maintaining VR head tracking

### Frame Timing
```
BeginFrame()
  ↓
xrWaitFrame() // sync with compositor
  ↓
xrBeginFrame() 
  ↓
xrLocateViews() // get HMD pose + eye matrices
  ↓
For each eye:
  ↓
  AcquireSwapchainImage() // get texture from runtime
  ↓
  Bind FBO with swapchain texture
  ↓
  Set active camera to VR eye camera
  ↓
  ApplyVRTransform(player camera, VR matrices)
  ↓
  WorldDrawer::Draw() // existing rendering code
  ↓
  ReleaseSwapchainImage()
  ↓
EndFrame()
  ↓
xrEndFrame() // submit to compositor
```

### Swapchain Management
- OpenXR provides pre-allocated swapchain textures
- Engine creates FBOs with depth renderbuffers
- Color attachment is OpenXR swapchain texture (bound per-frame)
- Resolution: Uses OpenXR recommended resolution (typically 2468×2740 per eye for Index)

## Usage

### Building
```bash
cd build-windows  # or your build directory
cmake .. -DUSE_OPENXR=ON
cmake --build .
```

### Configuration
Add to `springsettings.cfg`:
```ini
VRMode = 1
```

Or launch with command line:
```bash
./spring --VRMode=1
```

### Runtime Requirements
- SteamVR must be running before launching engine
- OpenXR runtime installed (SteamVR provides OpenXR 1.0 support)
- VR headset connected and tracked

## Logging
All VR operations log extensively:
- `L_INFO`: Initialization, resolution, system properties
- `L_DEBUG`: Per-frame operations (can be verbose)
- `L_ERROR`: All OpenXR failures with error codes
- `L_FATAL`: Initialization failures that prevent VR startup

Enable verbose logging:
```bash
./spring --VRMode=1 --LogLevel=DEBUG
```

## Error Handling
- **OpenXR initialization failure**: Engine logs fatal error and exits
- **Per-frame errors**: Logged but rendering continues (desktop fallback not implemented)
- **Runtime not ready**: Frame rendering skipped (xrWaitFrame returns shouldRender=false)

## Known Limitations
1. **HUD rendering**: Currently disabled in VR mode (ignored in design)
2. **Desktop window**: Still renders desktop view alongside VR (player camera)
3. **Performance**: Renders full scene twice per frame (no occlusion optimizations)
4. **Input**: Uses desktop mouse/keyboard (no VR controller support)
5. **Menu system**: Not VR-aware

## Future Enhancements
- VR controller input integration
- Foveated rendering for performance
- Asymmetric projection matrices for HMD-specific FOV
- VR-specific HUD overlay rendering
- Hand tracking support
- Room-scale support (XR_REFERENCE_SPACE_TYPE_STAGE)

## Testing Notes
- Tested on Linux with SteamVR
- Target HMD: Valve Index (2880×1600 combined resolution)
- Recommended GPU: RTX 2060 or equivalent
- Expected frame time: ~11ms per eye at native resolution

## Files Modified/Created

### Created
- `rts/System/Platform/OpenXRManager.h` (157 lines)
- `rts/System/Platform/OpenXRManager.cpp` (534 lines)
- `rts/Rendering/VRRenderer.h` (103 lines)
- `rts/Rendering/VRRenderer.cpp` (250 lines)

### Modified
- `rts/CMakeLists.txt` (+34 lines)
- `rts/Game/Camera.h` (+3 lines, +1 method declaration)
- `rts/Game/Camera.cpp` (+28 lines for ApplyVRTransform)
- `rts/Rendering/GlobalRendering.h` (+8 lines)
- `rts/Rendering/GlobalRendering.cpp` (+1 line)
- `rts/Game/Game.h` (+5 lines)
- `rts/Game/Game.cpp` (+40 lines)

**Total additions**: ~1000 lines of new code with comprehensive logging

## Dependencies
- OpenXR SDK 1.0+
- SteamVR (provides OpenXR runtime)
- OpenGL 4.x (engine existing requirement)
- Platform-specific: WGL (Windows) or GLX (Linux)

## License
All new code follows Spring engine GPL v2+ license (see LICENSE.html)
