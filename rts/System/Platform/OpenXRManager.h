/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef OPENXR_MANAGER_H
#define OPENXR_MANAGER_H

#include "System/Matrix44f.h"
#include "System/float3.h"

#ifdef USE_OPENXR

// Platform-specific defines MUST be before including openxr.h
#ifdef _WIN32
	#include <windows.h>
	#define XR_USE_PLATFORM_WIN32
#else
	#define XR_USE_PLATFORM_XLIB
#endif

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <array>

/**
 * @brief OpenXR VR Manager
 * 
 * Handles OpenXR initialization, session management, and frame rendering
 * for SteamVR-compatible VR headsets (Valve Index).
 * 
 * Uses OpenXR API 1.0 with seated reference space (XR_REFERENCE_SPACE_TYPE_LOCAL).
 */
class COpenXRManager {
public:
	struct EyeRenderData {
		uint32_t swapchainImageIndex;
		uint32_t swapchainWidth;
		uint32_t swapchainHeight;
		CMatrix44f viewMatrix;
		CMatrix44f projectionMatrix;
		float3 position;
		float3 orientation; // euler angles in radians
	};

	COpenXRManager();
	~COpenXRManager();

	/**
	 * @brief Initialize OpenXR instance, system, and session
	 * @return true on success, false on failure (logs fatal error)
	 */
	bool Initialize();

	/**
	 * @brief Shutdown and cleanup all OpenXR resources
	 */
	void Shutdown();

	/**
	 * @brief Poll OpenXR events and handle session state changes
	 * 
	 * CRITICAL: Must be called every frame before BeginFrame().
	 * Handles session lifecycle events including READY, STOPPING, EXITING.
	 */
	void PollEvents();

	/**
	 * @brief Begin a new VR frame
	 * 
	 * Calls xrWaitFrame and xrBeginFrame, obtains HMD pose and per-eye view data.
	 * Must be called before RenderEye().
	 * 
	 * @return true if frame should be rendered, false if session not ready
	 */
	bool BeginFrame();

	/**
	 * @brief Get render data for specified eye
	 * @param eyeIndex 0 for left eye, 1 for right eye
	 * @return Eye render data including matrices and swapchain info
	 */
	const EyeRenderData& GetEyeRenderData(int eyeIndex) const;

	/**
	 * @brief Acquire swapchain image for rendering to specified eye
	 * @param eyeIndex 0 for left eye, 1 for right eye
	 * @return OpenGL texture ID, or 0 on failure
	 */
	uint32_t AcquireSwapchainImage(int eyeIndex);

	/**
	 * @brief Release swapchain image after rendering
	 * @param eyeIndex 0 for left eye, 1 for right eye
	 */
	void ReleaseSwapchainImage(int eyeIndex);

	/**
	 * @brief End frame and submit to compositor
	 * 
	 * Calls xrEndFrame with both eye layers.
	 * Must be called after rendering both eyes.
	 */
	void EndFrame();

	/**
	 * @brief Get HMD transform matrix (world space)
	 * 
	 * This is the base HMD position/orientation before eye offsets.
	 * Used to apply HMD tracking on top of player camera position.
	 * 
	 * @return 4x4 transform matrix
	 */
	const CMatrix44f& GetHMDTransform() const { return hmdTransform; }

	/**
	 * @brief Check if OpenXR session is active and ready for rendering
	 */
	bool IsSessionActive() const { return sessionRunning; }

	/**
	 * @brief Get recommended eye resolution
	 */
	void GetRecommendedResolution(uint32_t& width, uint32_t& height) const;

private:
	bool CreateInstance();
	bool CreateSystem();
	bool CreateSession();
	bool CreateReferenceSpace();
	bool CreateSwapchains();
	void DestroySwapchains();

	bool EnumerateViewConfigurations();
	bool LocateViews();
	
	void UpdateHMDTransform();
	void CreateViewMatrixFromPose(const XrPosef& pose, CMatrix44f& outMat);
	void CreateProjectionMatrixFromFov(const XrFovf& fov, float nearZ, float farZ, CMatrix44f& outMat);
	void ConvertXrPose(const XrPosef& xrPose, float3& outPos, float3& outRot);

	// OpenXR handles
	XrInstance instance;
	XrSystemId systemId;
	XrSession session;
	XrSpace localSpace;

	// Swapchain data
	struct SwapchainData {
		XrSwapchain swapchain;
		std::vector<XrSwapchainImageOpenGLKHR> images;
		uint32_t width;
		uint32_t height;
	};
	std::array<SwapchainData, 2> swapchains; // left and right eye

	// Frame state
	XrFrameState frameState;
	std::array<XrView, 2> views;
	std::array<EyeRenderData, 2> eyeRenderData;
	
	CMatrix44f hmdTransform;
	
	bool initialized;
	bool sessionRunning;
	bool shouldRender;

	// View configuration
	XrViewConfigurationType viewConfigType;
	std::vector<XrViewConfigurationView> viewConfigs;
};

#endif // USE_OPENXR

#endif // OPENXR_MANAGER_H
