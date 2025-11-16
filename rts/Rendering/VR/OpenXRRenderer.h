/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
	#ifndef XR_USE_PLATFORM_WIN32
	#define XR_USE_PLATFORM_WIN32 1
	#endif
#elif defined(__linux__)
	#ifndef XR_USE_PLATFORM_XLIB
	#define XR_USE_PLATFORM_XLIB 1
	#endif
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_OPENGL 1
#endif

#if defined(_WIN32)
	#include <windows.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class CGame;
class CGlobalRendering;
class CMatrix44f;
class UniformConstants;
struct float4;

namespace VR {

class OpenXRRenderer {
public:
	OpenXRRenderer();
	~OpenXRRenderer();

	bool Initialize(CGlobalRendering& globalRendering);
	void Shutdown();

	void PollEvents();
	bool RenderFrame(CGame& game);

	bool IsActive() const { return sessionRunning; }

private:
	struct CameraState;
	struct GlobalViewState;
	struct SwapchainBuffers;

	bool CreateSession(CGlobalRendering& globalRendering);
	bool CreateReferenceSpace();
	bool CreateSwapchainResources();

	void DestroySwapchainResources();

	bool AcquireSwapchainImage(uint32_t& index);
	void ReleaseSwapchainImage();

	CameraState CaptureCameraState() const;
	void RestoreCameraState(const CameraState& state) const;

	GlobalViewState CaptureGlobalViewState() const;
	void RestoreGlobalViewState(const GlobalViewState& state) const;

	bool EnsureFramebuffers();
	void DrawDebugOverlay() const;
	void UpdateUniforms() const;

	void LogGlErrors(const char* stage) const;
	void LogXrError(const char* stage, long long result) const;

	bool BeginFrame(XrFrameState& frameState);
	void EndFrame(const XrFrameState& frameState, const std::vector<XrCompositionLayerProjectionView>& projectionViews);

	CMatrix44f BuildViewMatrix(const CMatrix44f& baseView, const XrPosef& pose) const;
	CMatrix44f BuildProjectionMatrix(const XrFovf& fov) const;

private:
	CGlobalRendering* globalRendering = nullptr;

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace referenceSpace = XR_NULL_HANDLE;
	XrEnvironmentBlendMode environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

	// Debug utils extension (optional)
	XrDebugUtilsMessengerEXT debugMessenger = XR_NULL_HANDLE;
	PFN_xrCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessengerEXT = nullptr;
	PFN_xrDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessengerEXT = nullptr;

	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool sessionRunning = false;

	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;

	uint32_t viewCount = 0;
	std::vector<XrViewConfigurationView> viewConfigurationViews;
	std::vector<XrView> views;

	XrSwapchain colorSwapchain = XR_NULL_HANDLE;
	int64_t colorSwapchainFormat = 0;

	std::vector<XrSwapchainImageOpenGLKHR> colorImages;
	std::vector<SwapchainBuffers> swapchainBuffers;

	uint32_t swapchainWidth = 0;
	uint32_t swapchainHeight = 0;
};

} // namespace VR
