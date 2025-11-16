/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "OpenXRManager.h"

#ifdef USE_OPENXR

#include "System/Log/ILog.h"
#include "System/SpringMath.h"
#include <cstring>
#include <cmath>

// Platform-specific OpenGL context headers
#ifdef _WIN32
	#include <windows.h>
	#include <GL/gl.h>
	#include <GL/glext.h>
#else
	#include <GL/glx.h>
	#include <X11/Xlib.h>
	#include <GL/glext.h>
#endif

#define XR_CHECK(call, msg) \
	do { \
		XrResult result = call; \
		if (XR_FAILED(result)) { \
			LOG_L(L_ERROR, "[VR] %s failed with error code %d", msg, result); \
			return false; \
		} \
		LOG_L(L_DEBUG, "[VR] %s succeeded", msg); \
	} while(0)

#define XR_CHECK_VOID(call, msg) \
	do { \
		XrResult result = call; \
		if (XR_FAILED(result)) { \
			LOG_L(L_ERROR, "[VR] %s failed with error code %d", msg, result); \
			return; \
		} \
		LOG_L(L_DEBUG, "[VR] %s succeeded", msg); \
	} while(0)

COpenXRManager::COpenXRManager()
	: instance(XR_NULL_HANDLE)
	, systemId(XR_NULL_SYSTEM_ID)
	, session(XR_NULL_HANDLE)
	, localSpace(XR_NULL_HANDLE)
	, initialized(false)
	, sessionRunning(false)
	, shouldRender(false)
	, viewConfigType(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
{
	std::memset(&frameState, 0, sizeof(frameState));
	frameState.type = XR_TYPE_FRAME_STATE;
	
	for (int i = 0; i < 2; ++i) {
		views[i].type = XR_TYPE_VIEW;
		views[i].next = nullptr;
		swapchains[i].swapchain = XR_NULL_HANDLE;
	}
	
	hmdTransform.LoadIdentity();
	
	LOG_L(L_INFO, "[VR] OpenXR Manager created");
}

COpenXRManager::~COpenXRManager()
{
	Shutdown();
}

bool COpenXRManager::Initialize()
{
	LOG_L(L_INFO, "[VR] Initializing OpenXR for Valve Index HMD...");
	
	if (!CreateInstance()) return false;
	if (!CreateSystem()) return false;
	if (!EnumerateViewConfigurations()) return false;
	if (!CreateSession()) return false;
	if (!CreateReferenceSpace()) return false;
	if (!CreateSwapchains()) return false;
	
	initialized = true;
	LOG_L(L_INFO, "[VR] OpenXR initialization complete");
	return true;
}

void COpenXRManager::Shutdown()
{
	if (!initialized)
		return;
	
	LOG_L(L_INFO, "[VR] Shutting down OpenXR...");
	
	// End session if still running
	if (sessionRunning && session != XR_NULL_HANDLE) {
		XrResult result = xrEndSession(session);
		if (XR_SUCCEEDED(result)) {
			LOG_L(L_INFO, "[VR] Session ended during shutdown");
		}
		sessionRunning = false;
	}
	
	DestroySwapchains();
	
	if (localSpace != XR_NULL_HANDLE) {
		xrDestroySpace(localSpace);
		localSpace = XR_NULL_HANDLE;
	}
	
	if (session != XR_NULL_HANDLE) {
		xrDestroySession(session);
		session = XR_NULL_HANDLE;
	}
	
	if (instance != XR_NULL_HANDLE) {
		xrDestroyInstance(instance);
		instance = XR_NULL_HANDLE;
	}
	
	initialized = false;
	sessionRunning = false;
	LOG_L(L_INFO, "[VR] OpenXR shutdown complete");
}

bool COpenXRManager::CreateInstance()
{
	LOG_L(L_DEBUG, "[VR] Creating OpenXR instance...");
	
	// Check for OpenGL extension
	uint32_t extensionCount = 0;
	XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
		"xrEnumerateInstanceExtensionProperties (count)");
	
	std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
	XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()),
		"xrEnumerateInstanceExtensionProperties (data)");
	
	bool hasOpenGLExtension = false;
	for (const auto& ext : extensions) {
		if (strcmp(ext.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0) {
			hasOpenGLExtension = true;
			LOG_L(L_DEBUG, "[VR] Found required extension: %s", ext.extensionName);
			break;
		}
	}
	
	if (!hasOpenGLExtension) {
		LOG_L(L_ERROR, "[VR] Required OpenGL extension not available");
		return false;
	}
	
	// Create instance
	const char* enabledExtensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	
	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	createInfo.enabledExtensionCount = 1;
	createInfo.enabledExtensionNames = enabledExtensions;
	strcpy(createInfo.applicationInfo.applicationName, "RecoilEngine");
	createInfo.applicationInfo.applicationVersion = 1;
	strcpy(createInfo.applicationInfo.engineName, "RecoilEngine");
	createInfo.applicationInfo.engineVersion = 1;
	// Use OpenXR API 1.0 for SteamVR compatibility (XR_MAKE_VERSION(1, 0, 0))
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	
	LOG_L(L_INFO, "[VR] Requesting OpenXR API version 1.0.0");
	XR_CHECK(xrCreateInstance(&createInfo, &instance), "xrCreateInstance");
	
	return true;
}

bool COpenXRManager::CreateSystem()
{
	LOG_L(L_DEBUG, "[VR] Getting OpenXR system...");
	
	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	
	XR_CHECK(xrGetSystem(instance, &systemInfo, &systemId), "xrGetSystem");
	
	// Log system properties
	XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
	if (XR_SUCCEEDED(xrGetSystemProperties(instance, systemId, &systemProps))) {
		LOG_L(L_INFO, "[VR] HMD System: %s", systemProps.systemName);
		LOG_L(L_INFO, "[VR] Max Layers: %u, Max Swapchain Size: %ux%u",
			systemProps.graphicsProperties.maxLayerCount,
			systemProps.graphicsProperties.maxSwapchainImageWidth,
			systemProps.graphicsProperties.maxSwapchainImageHeight);
	}
	
	return true;
}

bool COpenXRManager::EnumerateViewConfigurations()
{
	LOG_L(L_DEBUG, "[VR] Enumerating view configurations...");
	
	uint32_t viewConfigCount = 0;
	XR_CHECK(xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, 0, &viewConfigCount, nullptr),
		"xrEnumerateViewConfigurationViews (count)");
	
	if (viewConfigCount != 2) {
		LOG_L(L_ERROR, "[VR] Expected 2 views (stereo), got %u", viewConfigCount);
		return false;
	}
	
	viewConfigs.resize(viewConfigCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
	XR_CHECK(xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, viewConfigCount, &viewConfigCount, viewConfigs.data()),
		"xrEnumerateViewConfigurationViews (data)");
	
	for (uint32_t i = 0; i < viewConfigCount; ++i) {
		LOG_L(L_INFO, "[VR] Eye %u: Recommended resolution %ux%u, max %ux%u",
			i,
			viewConfigs[i].recommendedImageRectWidth, viewConfigs[i].recommendedImageRectHeight,
			viewConfigs[i].maxImageRectWidth, viewConfigs[i].maxImageRectHeight);
	}
	
	return true;
}

bool COpenXRManager::CreateSession()
{
	LOG_L(L_DEBUG, "[VR] Creating OpenXR session...");
	
	// Get OpenGL graphics requirements
	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR)),
		"xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)");
	
	XrGraphicsRequirementsOpenGLKHR graphicsReqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	XR_CHECK(pfnGetOpenGLGraphicsRequirementsKHR(instance, systemId, &graphicsReqs),
		"xrGetOpenGLGraphicsRequirementsKHR");
	
	LOG_L(L_INFO, "[VR] OpenGL version required: %d.%d - %d.%d",
		(int)XR_VERSION_MAJOR(graphicsReqs.minApiVersionSupported),
		(int)XR_VERSION_MINOR(graphicsReqs.minApiVersionSupported),
		(int)XR_VERSION_MAJOR(graphicsReqs.maxApiVersionSupported),
		(int)XR_VERSION_MINOR(graphicsReqs.maxApiVersionSupported));
	
	// Create session with OpenGL binding (platform-specific)
#ifdef _WIN32
	XrGraphicsBindingOpenGLWin32KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
	graphicsBinding.hDC = wglGetCurrentDC();
	graphicsBinding.hGLRC = wglGetCurrentContext();
	
	if (graphicsBinding.hDC == nullptr || graphicsBinding.hGLRC == nullptr) {
		LOG_L(L_ERROR, "[VR] Unable to get current OpenGL context (hDC=%p, hGLRC=%p)", 
			graphicsBinding.hDC, graphicsBinding.hGLRC);
		return false;
	}
#else
	// Linux/X11
	XrGraphicsBindingOpenGLXlibKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
	graphicsBinding.xDisplay = glXGetCurrentDisplay();
	graphicsBinding.glxContext = glXGetCurrentContext();
	graphicsBinding.glxDrawable = glXGetCurrentDrawable();
	graphicsBinding.visualid = 0; // not needed for existing context
	graphicsBinding.glxFBConfig = 0; // not needed for existing context
	
	if (graphicsBinding.xDisplay == nullptr || graphicsBinding.glxContext == nullptr) {
		LOG_L(L_ERROR, "[VR] Unable to get current OpenGL context");
		return false;
	}
#endif
	
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
	
	XR_CHECK(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession");
	
	// NOTE: Do NOT call xrBeginSession here!
	// The session must wait for XR_SESSION_STATE_READY event before beginning.
	// This will be handled by PollEvents() which must be called every frame.
	
	LOG_L(L_INFO, "[VR] Session created, waiting for READY state before beginning...");
	return true;
}

void COpenXRManager::PollEvents()
{
	if (instance == XR_NULL_HANDLE)
		return;
	
	XrEventDataBuffer eventBuffer{XR_TYPE_EVENT_DATA_BUFFER};
	
	while (xrPollEvent(instance, &eventBuffer) == XR_SUCCESS) {
		switch (eventBuffer.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				auto& stateChanged = *reinterpret_cast<XrEventDataSessionStateChanged*>(&eventBuffer);
				
				const char* stateStr = "UNKNOWN";
				switch (stateChanged.state) {
					case XR_SESSION_STATE_IDLE: stateStr = "IDLE"; break;
					case XR_SESSION_STATE_READY: stateStr = "READY"; break;
					case XR_SESSION_STATE_SYNCHRONIZED: stateStr = "SYNCHRONIZED"; break;
					case XR_SESSION_STATE_VISIBLE: stateStr = "VISIBLE"; break;
					case XR_SESSION_STATE_FOCUSED: stateStr = "FOCUSED"; break;
					case XR_SESSION_STATE_STOPPING: stateStr = "STOPPING"; break;
					case XR_SESSION_STATE_LOSS_PENDING: stateStr = "LOSS_PENDING"; break;
					case XR_SESSION_STATE_EXITING: stateStr = "EXITING"; break;
				}
				LOG_L(L_INFO, "[VR] Session state changed to: %s", stateStr);
				
				if (stateChanged.state == XR_SESSION_STATE_READY) {
					// Now we can begin the session
					XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
					beginInfo.primaryViewConfigurationType = viewConfigType;
					
					XrResult result = xrBeginSession(session, &beginInfo);
					if (XR_SUCCEEDED(result)) {
						sessionRunning = true;
						LOG_L(L_INFO, "[VR] Session successfully started");
					} else {
						LOG_L(L_ERROR, "[VR] xrBeginSession failed with error %d", result);
					}
				}
				else if (stateChanged.state == XR_SESSION_STATE_STOPPING) {
					if (sessionRunning) {
						XrResult result = xrEndSession(session);
						if (XR_SUCCEEDED(result)) {
							sessionRunning = false;
							LOG_L(L_INFO, "[VR] Session stopped");
						} else {
							LOG_L(L_ERROR, "[VR] xrEndSession failed with error %d", result);
						}
					}
				}
				else if (stateChanged.state == XR_SESSION_STATE_EXITING || 
				         stateChanged.state == XR_SESSION_STATE_LOSS_PENDING) {
					sessionRunning = false;
					LOG_L(L_WARNING, "[VR] Session exiting or loss pending");
				}
				
				break;
			}
			
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				LOG_L(L_DEBUG, "[VR] Reference space change pending");
				break;
			}
			
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				LOG_L(L_ERROR, "[VR] Instance loss pending - VR runtime shutting down");
				break;
			}
			
			default:
				LOG_L(L_DEBUG, "[VR] Unhandled event type %u", eventBuffer.type);
				break;
		}
		
		// Reset for next event
		eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};
	}
}

bool COpenXRManager::CreateReferenceSpace()
{
	LOG_L(L_DEBUG, "[VR] Creating reference space (LOCAL for seated VR)...");
	
	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
	spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};
	
	XR_CHECK(xrCreateReferenceSpace(session, &spaceInfo, &localSpace), "xrCreateReferenceSpace");
	
	return true;
}

bool COpenXRManager::CreateSwapchains()
{
	LOG_L(L_DEBUG, "[VR] Creating swapchains...");
	
	// Query supported swapchain formats
	uint32_t formatCount = 0;
	XR_CHECK(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr),
		"xrEnumerateSwapchainFormats (count)");
	
	std::vector<int64_t> formats(formatCount);
	XR_CHECK(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()),
		"xrEnumerateSwapchainFormats (data)");
	
	// Select the best supported format (prefer SRGB for gamma-correct rendering)
	int64_t selectedFormat = 0;
	for (int64_t format : formats) {
		LOG_L(L_DEBUG, "[VR] Available swapchain format: 0x%llX", format);
		if (format == GL_SRGB8_ALPHA8 || format == GL_RGBA8) {
			selectedFormat = format;
			LOG_L(L_INFO, "[VR] Selected swapchain format: 0x%llX", format);
			break;
		}
	}
	
	// Fallback to first available format if our preferred ones aren't available
	if (selectedFormat == 0 && !formats.empty()) {
		selectedFormat = formats[0];
		LOG_L(L_WARNING, "[VR] Using fallback swapchain format: 0x%llX", selectedFormat);
	}
	
	if (selectedFormat == 0) {
		LOG_L(L_ERROR, "[VR] No suitable swapchain format found");
		return false;
	}
	
	for (int eye = 0; eye < 2; ++eye) {
		const auto& viewConfig = viewConfigs[eye];
		
		XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainInfo.format = selectedFormat;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = viewConfig.recommendedImageRectWidth;
		swapchainInfo.height = viewConfig.recommendedImageRectHeight;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;
		
		XR_CHECK(xrCreateSwapchain(session, &swapchainInfo, &swapchains[eye].swapchain),
			"xrCreateSwapchain");
		
		swapchains[eye].width = swapchainInfo.width;
		swapchains[eye].height = swapchainInfo.height;
		
		// Enumerate swapchain images
		uint32_t imageCount = 0;
		XR_CHECK(xrEnumerateSwapchainImages(swapchains[eye].swapchain, 0, &imageCount, nullptr),
			"xrEnumerateSwapchainImages (count)");
		
		swapchains[eye].images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
		XR_CHECK(xrEnumerateSwapchainImages(swapchains[eye].swapchain, imageCount, &imageCount,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains[eye].images.data())),
			"xrEnumerateSwapchainImages (data)");
		
		LOG_L(L_INFO, "[VR] Eye %d swapchain created: %ux%u, %u images",
			eye, swapchainInfo.width, swapchainInfo.height, imageCount);
	}
	
	return true;
}

void COpenXRManager::DestroySwapchains()
{
	for (auto& swapchain : swapchains) {
		if (swapchain.swapchain != XR_NULL_HANDLE) {
			xrDestroySwapchain(swapchain.swapchain);
			swapchain.swapchain = XR_NULL_HANDLE;
		}
		swapchain.images.clear();
	}
}

bool COpenXRManager::BeginFrame()
{
	if (!sessionRunning) {
		// Session not started yet - PollEvents will handle starting it when runtime is ready
		static int logThrottle = 0;
		if (logThrottle++ % 120 == 0) {
			LOG_L(L_DEBUG, "[VR] Session not running yet, waiting for READY event from runtime...");
		}
		return false;
	}
	
	// Wait for next frame
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XR_CHECK(xrWaitFrame(session, &waitInfo, &frameState), "xrWaitFrame");
	
	LOG_L(L_DEBUG, "[VR] Frame time: predicted=%lld, should render=%d",
		frameState.predictedDisplayTime, frameState.shouldRender);
	
	// Begin frame
	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	XR_CHECK(xrBeginFrame(session, &beginInfo), "xrBeginFrame");
	
	shouldRender = frameState.shouldRender;
	
	// CRITICAL: We must continue the frame loop even when shouldRender is false
	// This allows OpenXR runtime to progress from SYNCHRONIZED -> VISIBLE -> FOCUSED
	// We still need to call xrEndFrame() even if we don't render anything
	
	// IMPORTANT: Always locate views, even when not rendering!
	// The runtime needs to see us calling xrLocateViews to know we're ready for rendering.
	// This is crucial for transitioning from SYNCHRONIZED -> VISIBLE.
	if (!LocateViews()) {
		// If view location fails, we definitely can't render
		shouldRender = false;
		LOG_L(L_WARNING, "[VR] LocateViews failed, cannot render this frame");
	} else {
		UpdateHMDTransform();
		
		if (shouldRender) {
			LOG_L(L_INFO, "[VR] Rendering this frame (shouldRender=true, likely in VISIBLE/FOCUSED state)");
		} else {
			// Throttle logging to avoid spam (log once per second at 60fps)
			static int throttle = 0;
			if (++throttle >= 60) {
				LOG_L(L_WARNING, "[VR] Not rendering (shouldRender=false). Session likely in SYNCHRONIZED state. Check if app window has focus or if another VR app is running.");
				throttle = 0;
			}
		}
	}
	
	// Return shouldRender to let caller know if they should render content
	// Caller must still call EndFrame() regardless of this return value
	return shouldRender;
}

bool COpenXRManager::LocateViews()
{
	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = viewConfigType;
	locateInfo.displayTime = frameState.predictedDisplayTime;
	locateInfo.space = localSpace;
	
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCount = 2;
	
	XrResult result = xrLocateViews(session, &locateInfo, &viewState, viewCount, &viewCount, views.data());
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrLocateViews failed with error %d", result);
		return false;
	}
	
	// Convert XR view data to engine format
	for (int eye = 0; eye < 2; ++eye) {
		auto& data = eyeRenderData[eye];
		const auto& view = views[eye];
		
		// Create view matrix from pose
		CreateViewMatrixFromPose(view.pose, data.viewMatrix);
		
		// Create projection matrix from FOV
		CreateProjectionMatrixFromFov(view.fov, 0.1f, 1000.0f, data.projectionMatrix);
		
		// Extract position and orientation
		ConvertXrPose(view.pose, data.position, data.orientation);
		
		data.swapchainWidth = swapchains[eye].width;
		data.swapchainHeight = swapchains[eye].height;
		
		LOG_L(L_DEBUG, "[VR] Eye %d pose: pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f)",
			eye, data.position.x, data.position.y, data.position.z,
			data.orientation.x, data.orientation.y, data.orientation.z);
	}
	
	return true;
}

void COpenXRManager::UpdateHMDTransform()
{
	// Use average position of both eyes for HMD center
	const float3 leftPos = eyeRenderData[0].position;
	const float3 rightPos = eyeRenderData[1].position;
	const float3 hmdPos = (leftPos + rightPos) * 0.5f;
	
	// Use left eye orientation (should be very similar to right)
	const float3& hmdRot = eyeRenderData[0].orientation;
	
	// Build HMD transform matrix
	hmdTransform.LoadIdentity();
	hmdTransform.Translate(hmdPos);
	hmdTransform.RotateEulerYXZ(hmdRot);
	
	LOG_L(L_DEBUG, "[VR] HMD transform: pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f)",
		hmdPos.x, hmdPos.y, hmdPos.z, hmdRot.x, hmdRot.y, hmdRot.z);
}

const COpenXRManager::EyeRenderData& COpenXRManager::GetEyeRenderData(int eyeIndex) const
{
	return eyeRenderData[eyeIndex];
}

uint32_t COpenXRManager::AcquireSwapchainImage(int eyeIndex)
{
	auto& swapchain = swapchains[eyeIndex];
	
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	uint32_t imageIndex = 0;
	
	XrResult result = xrAcquireSwapchainImage(swapchain.swapchain, &acquireInfo, &imageIndex);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrAcquireSwapchainImage failed for eye %d: %d", eyeIndex, result);
		return 0;
	}
	
	// Wait for image to be ready
	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	
	result = xrWaitSwapchainImage(swapchain.swapchain, &waitInfo);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrWaitSwapchainImage failed for eye %d: %d", eyeIndex, result);
		return 0;
	}
	
	eyeRenderData[eyeIndex].swapchainImageIndex = imageIndex;
	
	LOG_L(L_DEBUG, "[VR] Acquired swapchain image %u for eye %d (texture ID: %u)",
		imageIndex, eyeIndex, swapchain.images[imageIndex].image);
	
	return swapchain.images[imageIndex].image;
}

void COpenXRManager::ReleaseSwapchainImage(int eyeIndex)
{
	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	
	XrResult result = xrReleaseSwapchainImage(swapchains[eyeIndex].swapchain, &releaseInfo);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrReleaseSwapchainImage failed for eye %d: %d", eyeIndex, result);
	} else {
		LOG_L(L_DEBUG, "[VR] Released swapchain image for eye %d", eyeIndex);
	}
}

void COpenXRManager::EndFrame()
{
	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	
	// If we should render, submit layers. Otherwise submit empty frame.
	if (shouldRender) {
		std::vector<XrCompositionLayerProjectionView> projectionViews(2);
		
		for (int eye = 0; eye < 2; ++eye) {
			projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projectionViews[eye].next = nullptr;
			projectionViews[eye].pose = views[eye].pose;
			projectionViews[eye].fov = views[eye].fov;
			projectionViews[eye].subImage.swapchain = swapchains[eye].swapchain;
			projectionViews[eye].subImage.imageRect.offset = {0, 0};
			projectionViews[eye].subImage.imageRect.extent = {
				static_cast<int32_t>(swapchains[eye].width),
				static_cast<int32_t>(swapchains[eye].height)
			};
			projectionViews[eye].subImage.imageArrayIndex = 0;
		}
		
		XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		layer.space = localSpace;
		layer.viewCount = 2;
		layer.views = projectionViews.data();
		
		const XrCompositionLayerBaseHeader* layers[] = {
			reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
		};
		
		endInfo.layerCount = 1;
		endInfo.layers = layers;
		
		LOG_L(L_DEBUG, "[VR] Submitting frame with rendered layers");
	} else {
		// Submit empty frame to keep runtime happy during state transitions
		endInfo.layerCount = 0;
		endInfo.layers = nullptr;
		
		LOG_L(L_DEBUG, "[VR] Submitting empty frame (not rendering)");
	}
	
	XR_CHECK_VOID(xrEndFrame(session, &endInfo), "xrEndFrame");
}

void COpenXRManager::GetRecommendedResolution(uint32_t& width, uint32_t& height) const
{
	if (!viewConfigs.empty()) {
		width = viewConfigs[0].recommendedImageRectWidth;
		height = viewConfigs[0].recommendedImageRectHeight;
	} else {
		width = 2048;
		height = 2048;
	}
}

void COpenXRManager::CreateViewMatrixFromPose(const XrPosef& pose, CMatrix44f& outMat)
{
	// Convert quaternion to rotation matrix
	const auto& q = pose.orientation;
	const auto& p = pose.position;
	
	// Quaternion to matrix conversion
	float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
	float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
	float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
	float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
	
	// Build rotation matrix
	CMatrix44f rotMat;
	rotMat.m[0] = 1.0f - (yy + zz);
	rotMat.m[1] = xy + wz;
	rotMat.m[2] = xz - wy;
	rotMat.m[3] = 0.0f;
	
	rotMat.m[4] = xy - wz;
	rotMat.m[5] = 1.0f - (xx + zz);
	rotMat.m[6] = yz + wx;
	rotMat.m[7] = 0.0f;
	
	rotMat.m[8] = xz + wy;
	rotMat.m[9] = yz - wx;
	rotMat.m[10] = 1.0f - (xx + yy);
	rotMat.m[11] = 0.0f;
	
	rotMat.m[12] = 0.0f;
	rotMat.m[13] = 0.0f;
	rotMat.m[14] = 0.0f;
	rotMat.m[15] = 1.0f;
	
	// Invert to get view matrix (camera looks down -Z)
	outMat = rotMat.InvertAffine();
	
	// Apply translation
	outMat.m[12] = -(outMat.m[0] * p.x + outMat.m[4] * p.y + outMat.m[8] * p.z);
	outMat.m[13] = -(outMat.m[1] * p.x + outMat.m[5] * p.y + outMat.m[9] * p.z);
	outMat.m[14] = -(outMat.m[2] * p.x + outMat.m[6] * p.y + outMat.m[10] * p.z);
}

void COpenXRManager::CreateProjectionMatrixFromFov(const XrFovf& fov, float nearZ, float farZ, CMatrix44f& outMat)
{
	// OpenGL-style asymmetric projection matrix from FOV angles
	const float tanLeft = std::tan(fov.angleLeft);
	const float tanRight = std::tan(fov.angleRight);
	const float tanDown = std::tan(fov.angleDown);
	const float tanUp = std::tan(fov.angleUp);
	
	const float tanWidth = tanRight - tanLeft;
	const float tanHeight = tanUp - tanDown;
	
	outMat.m[0] = 2.0f / tanWidth;
	outMat.m[1] = 0.0f;
	outMat.m[2] = 0.0f;
	outMat.m[3] = 0.0f;
	
	outMat.m[4] = 0.0f;
	outMat.m[5] = 2.0f / tanHeight;
	outMat.m[6] = 0.0f;
	outMat.m[7] = 0.0f;
	
	outMat.m[8] = (tanRight + tanLeft) / tanWidth;
	outMat.m[9] = (tanUp + tanDown) / tanHeight;
	outMat.m[10] = -(farZ + nearZ) / (farZ - nearZ);
	outMat.m[11] = -1.0f;
	
	outMat.m[12] = 0.0f;
	outMat.m[13] = 0.0f;
	outMat.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
	outMat.m[15] = 0.0f;
}

void COpenXRManager::ConvertXrPose(const XrPosef& xrPose, float3& outPos, float3& outRot)
{
	// Position is straightforward
	outPos.x = xrPose.position.x;
	outPos.y = xrPose.position.y;
	outPos.z = xrPose.position.z;
	
	// Convert quaternion to euler angles
	const auto& q = xrPose.orientation;
	
	// Roll (x-axis rotation)
	float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
	float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
	outRot.x = std::atan2(sinr_cosp, cosr_cosp);
	
	// Pitch (y-axis rotation)
	float sinp = 2.0f * (q.w * q.y - q.z * q.x);
	if (std::abs(sinp) >= 1.0f)
		outRot.y = std::copysign(math::HALFPI, sinp);
	else
		outRot.y = std::asin(sinp);
	
	// Yaw (z-axis rotation)
	float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
	float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	outRot.z = std::atan2(siny_cosp, cosy_cosp);
}

#endif // USE_OPENXR
