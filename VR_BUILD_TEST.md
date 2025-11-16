# VR Build and Test Instructions

## Prerequisites

### Linux (Ubuntu/Debian)
```bash
# Install OpenXR SDK
sudo apt-get install libopenxr-dev libopenxr-loader1

# Install SteamVR (provides OpenXR runtime)
# Install Steam, then install SteamVR from Steam store
# Enable SteamVR OpenXR runtime:
# Steam → SteamVR → Settings → Developer → Set SteamVR as OpenXR runtime
```

### Windows
```bash
# Download OpenXR SDK from https://github.com/KhronosGroup/OpenXR-SDK
# Extract to C:\OpenXR-SDK

# Install SteamVR from Steam
# SteamVR automatically registers as OpenXR runtime on Windows
```

## Building with VR Support

### Linux
```bash
cd /home/wybren/Development/RecoilEngine
mkdir -p build-vr
cd build-vr

# Configure with OpenXR enabled
cmake .. -DUSE_OPENXR=ON

# Build
cmake --build . -j$(nproc)
```

### Windows (MinGW/MSYS2)
```bash
cd build-windows

# Configure with OpenXR enabled
cmake .. -DUSE_OPENXR=ON -DOpenXR_INCLUDE_DIR="C:/OpenXR-SDK/include" -DOpenXR_LIBRARY="C:/OpenXR-SDK/lib/openxr_loader.lib"

# Build
cmake --build . -j8
```

## Configuration

### Enable VR Mode
Edit `springsettings.cfg`:
```ini
[General]
VRMode = 1
Fullscreen = 0  # Recommended for testing
WindowBorderless = 1
```

Or use command line:
```bash
./spring --VRMode=1
```

## Testing Procedure

### 1. Verify OpenXR Runtime
```bash
# Linux
ls /usr/lib/x86_64-linux-gnu/libopenxr_loader.so*
# Should show libopenxr_loader.so.1

# Check active runtime (Steam must be running)
XR_RUNTIME_JSON=/home/$USER/.config/openxr/1/active_runtime.json
cat $XR_RUNTIME_JSON
# Should point to SteamVR's JSON manifest
```

### 2. Start SteamVR
- Launch Steam
- Start SteamVR
- Ensure HMD is detected and tracking (green icons)
- Check SteamVR status window shows "Ready"

### 3. Launch Engine with Logging
```bash
cd build-vr
./spring --VRMode=1 --LogLevel=DEBUG 2>&1 | tee vr_test.log
```

### 4. Expected Log Output
```
[INFO] [VR] Initializing VR rendering system...
[INFO] [VR] OpenXR Manager created
[DEBUG] [VR] Creating OpenXR instance...
[DEBUG] [VR] Found required extension: XR_KHR_opengl_enable
[DEBUG] [VR] xrCreateInstance succeeded
[DEBUG] [VR] Getting OpenXR system...
[INFO] [VR] HMD System: Valve Index
[INFO] [VR] Max Layers: 16, Max Swapchain Size: 4096x4096
[DEBUG] [VR] Enumerating view configurations...
[INFO] [VR] Eye 0: Recommended resolution 2468x2740, max 3024x3360
[INFO] [VR] Eye 1: Recommended resolution 2468x2740, max 3024x3360
[DEBUG] [VR] Creating OpenXR session...
[INFO] [VR] OpenGL version required: 4.3 - 4.6
[DEBUG] [VR] xrCreateSession succeeded
[DEBUG] [VR] xrBeginSession succeeded
[DEBUG] [VR] Creating reference space (LOCAL for seated VR)...
[DEBUG] [VR] xrCreateReferenceSpace succeeded
[DEBUG] [VR] Creating swapchains...
[INFO] [VR] Eye 0 swapchain created: 2468x2740, 3 images
[INFO] [VR] Eye 1 swapchain created: 2468x2740, 3 images
[INFO] [VR] VR rendering system initialized successfully
```

### 5. Per-Frame Logs (verbose)
```
[DEBUG] [VR] Frame time: predicted=12345678, should render=1
[DEBUG] [VR] Frame begun successfully
[DEBUG] [VR] Eye 0 pose: pos=(0.000, 1.500, 0.000) rot=(0.000, 0.000, 0.000)
[DEBUG] [VR] Rendering eye 0...
[DEBUG] [VR] Acquired swapchain image 0 for eye 0 (texture ID: 42)
[DEBUG] [VR] Bound FBO 1 with texture 42 for eye 0
[DEBUG] [VR] Eye 0 rendered successfully
[DEBUG] [VR] Eye 1 pose: pos=(0.064, 1.500, 0.000) rot=(0.000, 0.000, 0.000)
[DEBUG] [VR] Rendering eye 1...
[DEBUG] [VR] Acquired swapchain image 1 for eye 1 (texture ID: 43)
[DEBUG] [VR] Bound FBO 2 with texture 43 for eye 1
[DEBUG] [VR] Eye 1 rendered successfully
[DEBUG] [VR] Frame submitted successfully
[DEBUG] [VR] Frame ended successfully
```

## Common Issues

### Issue: "Failed to initialize OpenXR"
**Cause**: SteamVR not running or not set as OpenXR runtime
**Solution**:
1. Start SteamVR first
2. Verify runtime: `cat ~/.config/openxr/1/active_runtime.json`
3. Set SteamVR as runtime: SteamVR Settings → Developer → Set as OpenXR Runtime

### Issue: "Required OpenGL extension not available"
**Cause**: GPU drivers don't support OpenXR's OpenGL integration
**Solution**:
1. Update GPU drivers to latest version
2. Ensure GPU supports OpenGL 4.3+
3. Check `glxinfo | grep "OpenGL version"`

### Issue: Black screen in HMD
**Cause**: Framebuffer setup issue or swapchain mismatch
**Solution**:
1. Check logs for "Framebuffer incomplete" errors
2. Verify swapchain texture IDs in logs are non-zero
3. Check OpenGL errors: look for GL_INVALID_OPERATION

### Issue: "xrWaitFrame failed"
**Cause**: OpenXR session lost or SteamVR crashed
**Solution**:
1. Restart SteamVR
2. Check SteamVR logs: `~/.steam/steam/logs/vrserver.txt`
3. Reconnect HMD USB/DisplayPort

### Issue: Low FPS / stuttering
**Cause**: Rendering too slow for HMD refresh rate (90Hz/120Hz/144Hz)
**Solution**:
1. Lower game graphics settings
2. Reduce resolution: Edit `viewSizeX` and `viewSizeY` in config
3. Check CPU/GPU usage with `htop` / `nvidia-smi`
4. Disable debug logging (use L_INFO instead of L_DEBUG)

### Issue: Head tracking not working
**Cause**: VR cameras not receiving HMD pose
**Solution**:
1. Check logs for "xrLocateViews failed"
2. Verify poses are changing: `rot=(x, y, z)` should update when moving head
3. Ensure SteamVR is tracking (green icons, not gray)
4. Check lighthouse base stations are visible to HMD

## Performance Benchmarks

### Target Performance (Valve Index)
- Native resolution: 2468×2740 per eye
- Refresh rate: 90Hz (11.1ms per frame)
- Target frame time: < 11ms total (< 5.5ms per eye)

### Expected Performance
With RTX 2060 / RX 5700 XT class GPU:
- Simple scenes: 90 FPS sustained
- Medium complexity: 60-90 FPS (reprojection may occur)
- Complex scenes: 45-60 FPS (motion smoothing recommended)

## Debugging Tips

### Enable Verbose Logging
```bash
./spring --VRMode=1 --LogLevel=DEBUG --LogSections=VR
```

### Monitor OpenXR Calls
```bash
# Set OpenXR API layer for validation
export XR_ENABLE_API_LAYERS=XR_APILAYER_LUNARG_core_validation
./spring --VRMode=1
```

### Check OpenGL State
Add to code temporarily:
```cpp
GLenum err;
while ((err = glGetError()) != GL_NO_ERROR) {
    LOG_L(L_ERROR, "[VR] OpenGL error: 0x%X", err);
}
```

### Profile Frame Timing
```bash
# GPU profiling
nvidia-smi dmon -s pucvmet -i 0
# Or for AMD
radeontop
```

## Contact/Support
- Check engine logs in `infolog.txt`
- SteamVR logs: `~/.steam/steam/logs/vrserver.txt`
- OpenXR logs: Check system journal `journalctl -xe | grep -i openxr`
