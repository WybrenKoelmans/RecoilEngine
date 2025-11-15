#pragma once

#include "System/float3.h"
#include "System/Matrix44f.h"
#include "System/Quaternion.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

struct CCamera;
class CGlobalRendering;

#if defined(USE_VR)

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_OPENGL
#endif

#if defined(_WIN32)
#include <unknwn.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vr {

constexpr uint32_t kInvalidSwapchainIndex = std::numeric_limits<uint32_t>::max();

struct EyeRenderTarget {
	int eyeIndex = 0;
	int width = 0;
	int height = 0;
	unsigned int framebuffer = 0;
	unsigned int colorTexture = 0;
	unsigned int depthBuffer = 0;
	CQuaternion absoluteOrientation;   // orientation reported by OpenXR (engine space)
	CQuaternion relativeOrientation;   // orientation delta relative to initial head pose
	float3 eyeOffsetWorld = float3(ZeroVector);
	CMatrix44f projectionMatrix;
	float frustumLeft = 0.0f;
	float frustumRight = 0.0f;
	float frustumBottom = 0.0f;
	float frustumTop = 0.0f;
	float nearPlane = 0.0f;
	float farPlane = 0.0f;
};

class OpenXRSystem {
public:
	OpenXRSystem() = default;
	~OpenXRSystem();

	bool Initialize(CGlobalRendering& globalRendering);
	void Shutdown();

	void PollEvents();

	void SetGameActive(bool active);
	bool ShouldBlockDesktopSwap() const;

	bool FrameSubmittedThisTick() const { return frameSubmitted; }
	void ResetFrameSubmissionFlag() { frameSubmitted = false; }

	bool BeginFrame();
	using EyeRenderCallback = std::function<void(const EyeRenderTarget&)>;
	bool RenderEyes(const EyeRenderCallback& callback, const CCamera& baseCamera);
	void EndFrame();

	bool IsInitialized() const { return initialized; }
	bool IsSessionRunning() const { return sessionRunning; }

	const CQuaternion& GetReferenceOrientation() const { return referenceOrientation; }
	void RecenterReferenceOrientation(const CQuaternion& currentHeadOrientation);


private:
	bool InitializeInstance();
	bool InitializeSystem();
	bool InitializeSession(CGlobalRendering& globalRendering);
	bool InitializeSwapchains();

	void DestroySwapchains();
	void DestroySession();
	void DestroyInstance();

	bool AcquireSwapchainImages();
	void ReleaseSwapchainImages();

	EyeRenderTarget BuildEyeTarget(int eyeIndex, const CCamera& baseCamera);
	CQuaternion ConvertOrientation(const CQuaternion& raw) const;

private:
	bool initialized = false;
	bool sessionRunning = false;
	bool frameBegun = false;
	bool frameSubmitted = false;
	bool gameActive = false;
	bool blockDesktopSwap = false;

	CQuaternion referenceOrientation; // stored when session becomes ready

	XrInstance instance = nullptr;
	XrSession session = nullptr;
	XrSpace appSpace = nullptr;
	XrSpace viewSpace = nullptr;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrViewConfigurationType viewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	uint32_t viewCount = 0;
	uint32_t activeViewCount = 0;
	PFN_xrGetOpenGLGraphicsRequirementsKHR getGraphicsRequirements = nullptr;

	struct SwapchainData {
		XrSwapchain handle = nullptr;
		int width = 0;
		int height = 0;
		std::vector<unsigned int> colorTextures;
		std::vector<unsigned int> framebuffers;
		std::vector<unsigned int> depthBuffers;
		uint32_t activeImageIndex = kInvalidSwapchainIndex;
	};

	std::vector<SwapchainData> swapchains;

	std::vector<struct XrView> views;
	std::vector<struct XrCompositionLayerProjectionView> projectionViews;
	std::vector<XrViewConfigurationView> viewConfigurationViews;
	XrCompositionLayerProjection projectionLayer {};
	XrViewConfigurationProperties viewConfigProps {};
	XrEnvironmentBlendMode environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

	uint64_t predictedDisplayTime = 0;
	bool referenceOrientationSet = false;
};

OpenXRSystem* GetOpenXRSystem();
bool EnsureOpenXRSystem(CGlobalRendering& globalRendering);
void ShutdownOpenXRSystem();

} // namespace vr

#else // !defined(USE_VR)

namespace vr {

struct EyeRenderTarget {
	int eyeIndex = 0;
};

class OpenXRSystem {
public:
	bool Initialize(CGlobalRendering&) { return false; }
	void Shutdown() {}
	void PollEvents() {}
	void SetGameActive(bool) {}
	bool ShouldBlockDesktopSwap() const { return false; }
	bool FrameSubmittedThisTick() const { return false; }
	void ResetFrameSubmissionFlag() {}
	bool BeginFrame() { return false; }
	using EyeRenderCallback = std::function<void(const EyeRenderTarget&)>;
	bool RenderEyes(const EyeRenderCallback&, const CCamera&) { return false; }
	void EndFrame() {}
	bool IsInitialized() const { return false; }
	bool IsSessionRunning() const { return false; }
	const CQuaternion& GetReferenceOrientation() const { return referenceOrientation; }
	void RecenterReferenceOrientation(const CQuaternion&) {}

private:
	CQuaternion referenceOrientation;
};

inline OpenXRSystem* GetOpenXRSystem() { return nullptr; }
inline bool EnsureOpenXRSystem(CGlobalRendering&) { return false; }
inline void ShutdownOpenXRSystem() {}

} // namespace vr

#endif
