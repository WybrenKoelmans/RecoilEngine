#include "VRSystem.h"
#include "Rendering/GL/myGL.h"  // Include GLAD for OpenGL functions
#include "System/Log/ILog.h"
#include "System/SpringMath.h"
#include <cstring>
#include <vector>

#ifdef USE_VR
#ifdef _WIN32
#include <windows.h>  // For wglGetCurrentDC, wglGetCurrentContext
#elif defined(__linux__)
#include <GL/glx.h>
#endif
#endif

CVRSystem* g_VRSystem = nullptr;

CVRSystem::CVRSystem()
	: initialized(false)
	, active(false)
	, sessionRunning(false)
	, leftJoystick(0.0f, 0.0f)
	, rightJoystick(0.0f, 0.0f)
	, leftGrip(false)
	, rightGrip(false)
	, leftTrigger(false)
	, rightTrigger(false)
#ifdef USE_VR
	, instance(XR_NULL_HANDLE)
	, systemId(XR_NULL_SYSTEM_ID)
	, session(XR_NULL_HANDLE)
	, playSpace(XR_NULL_HANDLE)
	, sessionState(XR_SESSION_STATE_UNKNOWN)
	, viewConfigType(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
	, actionSet(XR_NULL_HANDLE)
	, joystickLeftAction(XR_NULL_HANDLE)
	, joystickRightAction(XR_NULL_HANDLE)
	, gripLeftAction(XR_NULL_HANDLE)
	, gripRightAction(XR_NULL_HANDLE)
	, triggerLeftAction(XR_NULL_HANDLE)
	, triggerRightAction(XR_NULL_HANDLE)
	, leftHandPath(XR_NULL_PATH)
	, rightHandPath(XR_NULL_PATH)
	, leftHandSpace(XR_NULL_HANDLE)
	, rightHandSpace(XR_NULL_HANDLE)
#endif
{
	for (int i = 0; i < EYE_COUNT; i++) {
		eyeData[i].framebuffer = 0;
		eyeData[i].colorTexture = 0;
		eyeData[i].depthTexture = 0;
		eyeData[i].width = 0;
		eyeData[i].height = 0;
	}
}

CVRSystem::~CVRSystem()
{
	Shutdown();
}

bool CVRSystem::Initialize()
{
#ifndef USE_VR
	LOG_L(L_WARNING, "[VR] VR support not compiled in - USE_VR not defined");
	return false;
#else
	LOG_L(L_INFO, "[VR] Initializing OpenXR VR system...");
	
	if (!CreateInstance()) {
		LOG_L(L_ERROR, "[VR] Failed to create OpenXR instance");
		return false;
	}
	
	if (!CreateSystem()) {
		LOG_L(L_ERROR, "[VR] Failed to get OpenXR system");
		return false;
	}
	
	initialized = true;
	
	LOG_L(L_INFO, "[VR] OpenXR VR system initialized successfully (session will start when game loads)");
	return true;
#endif
}

void CVRSystem::Shutdown()
{
#ifdef USE_VR
	if (!initialized)
		return;
		
	LOG_L(L_INFO, "[VR] Shutting down OpenXR VR system...");
	
	// Clean up framebuffers
	for (int i = 0; i < EYE_COUNT; i++) {
		if (eyeData[i].framebuffer) {
			glDeleteFramebuffers(1, &eyeData[i].framebuffer);
			eyeData[i].framebuffer = 0;
		}
		if (eyeData[i].depthTexture) {
			glDeleteTextures(1, &eyeData[i].depthTexture);
			eyeData[i].depthTexture = 0;
		}
	}
	
	// Clean up swapchains
	for (int i = 0; i < EYE_COUNT; i++) {
		if (swapchains[i].handle != XR_NULL_HANDLE) {
			xrDestroySwapchain(swapchains[i].handle);
			swapchains[i].handle = XR_NULL_HANDLE;
		}
	}
	
	if (playSpace != XR_NULL_HANDLE) {
		xrDestroySpace(playSpace);
		playSpace = XR_NULL_HANDLE;
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
	active = false;
#endif
}

bool CVRSystem::StartSession()
{
#ifndef USE_VR
	return false;
#else
	if (!initialized) {
		LOG_L(L_ERROR, "[VR] Cannot start session - VR system not initialized");
		return false;
	}
	
	if (sessionRunning) {
		LOG_L(L_WARNING, "[VR] Session already running");
		return true;
	}
	
	LOG_L(L_INFO, "[VR] Starting OpenXR session...");
	
	if (!CreateSession()) {
		LOG_L(L_ERROR, "[VR] Failed to create OpenXR session");
		return false;
	}
	
	if (!CreateReferenceSpace()) {
		LOG_L(L_ERROR, "[VR] Failed to create reference space");
		return false;
	}
	
	if (!CreateSwapchains()) {
		LOG_L(L_ERROR, "[VR] Failed to create swapchains");
		return false;
	}
	
	CreateFramebuffers();
	
	// Create and attach controller actions
	if (!CreateActions()) {
		LOG_L(L_WARNING, "[VR] Failed to create actions, controller input will not be available");
	}
	
	// Begin the session
	XrSessionBeginInfo sessionBeginInfo{XR_TYPE_SESSION_BEGIN_INFO};
	sessionBeginInfo.primaryViewConfigurationType = viewConfigType;
	XrResult result = xrBeginSession(session, &sessionBeginInfo);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrBeginSession failed with result %d", result);
		return false;
	}
	
	// Attach action set after session begins
	if (!AttachActionSet()) {
		LOG_L(L_WARNING, "[VR] Failed to attach action set, controller input will not be available");
	}
	
	sessionRunning = true;
	active = true;
	
	LOG_L(L_INFO, "[VR] OpenXR session started successfully");
	return true;
#endif
}

void CVRSystem::StopSession()
{
#ifdef USE_VR
	if (!sessionRunning)
		return;
		
	LOG_L(L_INFO, "[VR] Stopping OpenXR session...");
	
	// Clean up framebuffers
	for (int i = 0; i < EYE_COUNT; i++) {
		if (eyeData[i].framebuffer) {
			glDeleteFramebuffers(1, &eyeData[i].framebuffer);
			eyeData[i].framebuffer = 0;
		}
		if (eyeData[i].depthTexture) {
			glDeleteTextures(1, &eyeData[i].depthTexture);
			eyeData[i].depthTexture = 0;
		}
	}
	
	// Clean up swapchains
	for (int i = 0; i < EYE_COUNT; i++) {
		if (swapchains[i].handle != XR_NULL_HANDLE) {
			xrDestroySwapchain(swapchains[i].handle);
			swapchains[i].handle = XR_NULL_HANDLE;
		}
	}
	
	if (playSpace != XR_NULL_HANDLE) {
		xrDestroySpace(playSpace);
		playSpace = XR_NULL_HANDLE;
	}
	
	if (session != XR_NULL_HANDLE) {
		xrDestroySession(session);
		session = XR_NULL_HANDLE;
	}
	
	sessionRunning = false;
	active = false;
	
	LOG_L(L_INFO, "[VR] OpenXR session stopped");
#endif
}

#ifdef USE_VR

bool CVRSystem::CreateInstance()
{
	// Enumerate available extensions for debugging
	uint32_t extensionCount = 0;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	if (extensionCount > 0) {
		std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
		xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
		LOG_L(L_INFO, "[VR] Available OpenXR extensions:");
		for (const auto& ext : extensions) {
			LOG_L(L_INFO, "[VR]   - %s", ext.extensionName);
		}
	} else {
		LOG_L(L_WARNING, "[VR] No OpenXR extensions enumerated - runtime may not be properly installed");
	}
	
	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	strcpy(createInfo.applicationInfo.applicationName, "RecoilEngine");
	createInfo.applicationInfo.applicationVersion = 1;
	strcpy(createInfo.applicationInfo.engineName, "RecoilEngine");
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);  // Use OpenXR 1.0 for SteamVR 2.13.7 compatibility
	
	const char* extensions[] = {
		XR_KHR_OPENGL_ENABLE_EXTENSION_NAME
	};
	createInfo.enabledExtensionCount = 1;
	createInfo.enabledExtensionNames = extensions;
	
	LOG_L(L_INFO, "[VR] Attempting to create OpenXR instance with API version 1.0 (for SteamVR compatibility)");
	
	XrResult result = xrCreateInstance(&createInfo, &instance);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrCreateInstance failed with result %d", result);
		if (result == XR_ERROR_RUNTIME_UNAVAILABLE) {
			LOG_L(L_ERROR, "[VR] OpenXR runtime not available - make sure SteamVR is running and your headset is connected");
			LOG_L(L_ERROR, "[VR] Also check: SteamVR Settings -> Developer -> Set SteamVR as OpenXR Runtime");
		} else if (result == XR_ERROR_EXTENSION_NOT_PRESENT) {
			LOG_L(L_ERROR, "[VR] Required OpenXR extension not present: %s", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		}
		return false;
	}
	
	LOG_L(L_INFO, "[VR] OpenXR instance created successfully");
	return true;
}

bool CVRSystem::CreateSystem()
{
	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	
	XrResult result = xrGetSystem(instance, &systemInfo, &systemId);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrGetSystem failed with result %d", result);
		return false;
	}
	
	// Get view configuration views
	uint32_t viewCount;
	xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, 0, &viewCount, nullptr);
	viewConfigs.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
	xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, viewCount, &viewCount, viewConfigs.data());
	
	views.resize(viewCount, {XR_TYPE_VIEW});
	
	LOG_L(L_INFO, "[VR] HMD view count: %u", viewCount);
	for (uint32_t i = 0; i < viewCount; i++) {
		LOG_L(L_INFO, "[VR]   Eye %u: Recommended size %ux%u, Max size %ux%u", 
			i,
			viewConfigs[i].recommendedImageRectWidth,
			viewConfigs[i].recommendedImageRectHeight,
			viewConfigs[i].maxImageRectWidth,
			viewConfigs[i].maxImageRectHeight);
	}
	
	return true;
}

bool CVRSystem::CreateSession()
{
	// Get OpenGL requirements
	XrGraphicsRequirementsOpenGLKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	
	PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetOpenGLGraphicsRequirementsKHR = nullptr;
	xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR));
	
	if (pfnGetOpenGLGraphicsRequirementsKHR) {
		XrResult result = pfnGetOpenGLGraphicsRequirementsKHR(instance, systemId, &graphicsRequirements);
		if (XR_FAILED(result)) {
			LOG_L(L_WARNING, "[VR] Failed to get OpenGL graphics requirements");
		}
	}
	
	// Create session with OpenGL binding
	XrGraphicsBindingOpenGLWin32KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
	
#ifdef _WIN32
	graphicsBinding.hDC = wglGetCurrentDC();
	graphicsBinding.hGLRC = wglGetCurrentContext();
#elif defined(__linux__)
	// For Linux with X11
	XrGraphicsBindingOpenGLXlibKHR graphicsBindingXlib{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
	graphicsBindingXlib.xDisplay = glXGetCurrentDisplay();
	graphicsBindingXlib.glxContext = glXGetCurrentContext();
	graphicsBindingXlib.glxDrawable = glXGetCurrentDrawable();
	
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBindingXlib;
	sessionInfo.systemId = systemId;
#else
	#error "Platform not supported for OpenXR OpenGL binding"
#endif

#ifdef _WIN32
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;
#endif
	
	XrResult result = xrCreateSession(instance, &sessionInfo, &session);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] xrCreateSession failed with result %d", result);
		return false;
	}
	
	return true;
}

bool CVRSystem::CreateReferenceSpace()
{
	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	
	XrResult result = xrCreateReferenceSpace(session, &spaceInfo, &playSpace);
	if (XR_FAILED(result)) {
		LOG_L(L_WARNING, "[VR] Failed to create STAGE reference space, trying LOCAL");
		spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		result = xrCreateReferenceSpace(session, &spaceInfo, &playSpace);
		if (XR_FAILED(result)) {
			LOG_L(L_ERROR, "[VR] xrCreateReferenceSpace failed with result %d", result);
			return false;
		}
	}
	
	return true;
}

bool CVRSystem::CreateSwapchains()
{
	// Query supported swapchain formats from the runtime
	uint32_t formatCount = 0;
	xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
	if (formatCount == 0) {
		LOG_L(L_ERROR, "[VR] No swapchain formats available from runtime");
		return false;
	}
	
	std::vector<int64_t> formats(formatCount);
	xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
	
	// Log available formats for debugging
	LOG_L(L_INFO, "[VR] Available swapchain formats (%u):", formatCount);
	for (uint32_t i = 0; i < formatCount && i < 10; i++) {
		LOG_L(L_INFO, "[VR]   Format %u: 0x%llx", i, (unsigned long long)formats[i]);
	}
	
	// Select an appropriate format
	// Prefer SRGB formats for proper color space handling
	// Common OpenGL formats: GL_RGBA8 (0x8058), GL_SRGB8_ALPHA8 (0x8C43), GL_RGBA16F (0x881A)
	int64_t selectedFormat = formats[0]; // Default to first format
	
	// Try to find SRGB8_ALPHA8 first, then RGBA8, otherwise use first available
	for (int64_t fmt : formats) {
		if (fmt == GL_SRGB8_ALPHA8) {
			selectedFormat = fmt;
			break;
		} else if (fmt == GL_RGBA8 && selectedFormat == formats[0]) {
			selectedFormat = fmt;
		}
	}
	
	LOG_L(L_INFO, "[VR] Selected swapchain format: 0x%llx", (unsigned long long)selectedFormat);
	
	for (int i = 0; i < EYE_COUNT && i < static_cast<int>(viewConfigs.size()); i++) {
		XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainInfo.format = selectedFormat;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = viewConfigs[i].recommendedImageRectWidth;
		swapchainInfo.height = viewConfigs[i].recommendedImageRectHeight;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;
		
		XrResult result = xrCreateSwapchain(session, &swapchainInfo, &swapchains[i].handle);
		if (XR_FAILED(result)) {
			LOG_L(L_ERROR, "[VR] xrCreateSwapchain failed for eye %d with result %d", i, result);
			return false;
		}
		
		swapchains[i].width = swapchainInfo.width;
		swapchains[i].height = swapchainInfo.height;
		
		// Enumerate swapchain images
		uint32_t imageCount;
		xrEnumerateSwapchainImages(swapchains[i].handle, 0, &imageCount, nullptr);
		swapchains[i].images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
		xrEnumerateSwapchainImages(swapchains[i].handle, imageCount, &imageCount,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains[i].images.data()));
		
		LOG_L(L_INFO, "[VR] Created swapchain for eye %d: %dx%d with %u images",
			i, swapchains[i].width, swapchains[i].height, imageCount);
	}
	
	return true;
}

void CVRSystem::CreateFramebuffers()
{
	for (int i = 0; i < EYE_COUNT; i++) {
		// Create framebuffer
		glGenFramebuffers(1, &eyeData[i].framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, eyeData[i].framebuffer);
		
		// Create depth texture
		glGenTextures(1, &eyeData[i].depthTexture);
		glBindTexture(GL_TEXTURE_2D, eyeData[i].depthTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 
			swapchains[i].width, swapchains[i].height, 0, 
			GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 
			GL_TEXTURE_2D, eyeData[i].depthTexture, 0);
		
		eyeData[i].width = swapchains[i].width;
		eyeData[i].height = swapchains[i].height;
		
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		
		LOG_L(L_INFO, "[VR] Created framebuffer for eye %d", i);
	}
}

void CVRSystem::WaitGetPoses()
{
	if (!initialized || !sessionRunning || !session)
		return;
	
	// Poll OpenXR events
	PollEvents();
		
	// Wait for next frame
	XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
	xrWaitFrame(session, &frameWaitInfo, &frameState);
	
	// Begin frame
	XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	xrBeginFrame(session, &frameBeginInfo);
	
	// Check if we should render
	if (!frameState.shouldRender) {
		// End frame without rendering
		XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
		frameEndInfo.displayTime = frameState.predictedDisplayTime;
		frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		frameEndInfo.layerCount = 0;
		frameEndInfo.layers = nullptr;
		xrEndFrame(session, &frameEndInfo);
		return;
	}
	
	// Locate views
	XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	viewLocateInfo.viewConfigurationType = viewConfigType;
	viewLocateInfo.displayTime = frameState.predictedDisplayTime;
	viewLocateInfo.space = playSpace;
	
	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCount = static_cast<uint32_t>(views.size());
	xrLocateViews(session, &viewLocateInfo, &viewState, viewCount, &viewCount, views.data());
	
	UpdateEyePoses();
}

void CVRSystem::UpdateEyePoses()
{
	for (int i = 0; i < EYE_COUNT && i < static_cast<int>(views.size()); i++) {
		const XrView& view = views[i];
		
		// Extract position
		eyeData[i].position.x = view.pose.position.x;
		eyeData[i].position.y = view.pose.position.y;
		eyeData[i].position.z = view.pose.position.z;
		
		// Extract orientation and compute direction vectors
		const XrQuaternionf& q = view.pose.orientation;
		
		// Convert quaternion to forward/up/right vectors
		eyeData[i].forward.x = 2.0f * (q.x * q.z - q.w * q.y);
		eyeData[i].forward.y = 2.0f * (q.y * q.z + q.w * q.x);
		eyeData[i].forward.z = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
		
		eyeData[i].up.x = 2.0f * (q.x * q.y + q.w * q.z);
		eyeData[i].up.y = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
		eyeData[i].up.z = 2.0f * (q.y * q.z - q.w * q.x);
		
		eyeData[i].right.x = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
		eyeData[i].right.y = 2.0f * (q.x * q.y - q.w * q.z);
		eyeData[i].right.z = 2.0f * (q.x * q.z + q.w * q.y);
		
		// Build projection matrix from FOV
		// Note: near/far will be overridden by game camera values when used
		const XrFovf& fov = view.fov;
		const float nearZ = 1.0f;  // placeholder, will use camera frustum values
		const float farZ = 65536.0f;  // placeholder, will use camera frustum values
		
		const float tanLeft = tanf(fov.angleLeft);
		const float tanRight = tanf(fov.angleRight);
		const float tanDown = tanf(fov.angleDown);
		const float tanUp = tanf(fov.angleUp);
		
		const float tanAngleWidth = tanRight - tanLeft;
		const float tanAngleHeight = tanUp - tanDown;
		
		float proj[16] = {0};
		proj[0] = 2.0f / tanAngleWidth;
		proj[4] = 0.0f;
		proj[8] = (tanRight + tanLeft) / tanAngleWidth;
		proj[12] = 0.0f;
		
		proj[1] = 0.0f;
		proj[5] = 2.0f / tanAngleHeight;
		proj[9] = (tanUp + tanDown) / tanAngleHeight;
		proj[13] = 0.0f;
		
		proj[2] = 0.0f;
		proj[6] = 0.0f;
		proj[10] = -(farZ + nearZ) / (farZ - nearZ);
		proj[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
		
		proj[3] = 0.0f;
		proj[7] = 0.0f;
		proj[11] = -1.0f;
		proj[15] = 0.0f;
		
		eyeData[i].projectionMatrix = CMatrix44f(proj);
		
		// Build view matrix
		// View matrix transforms from world space to eye space
		// We need to invert the pose transformation
		float viewMat[16];
		
		// Rotation part (transpose of rotation matrix from quaternion)
		viewMat[0] = eyeData[i].right.x;
		viewMat[1] = eyeData[i].up.x;
		viewMat[2] = -eyeData[i].forward.x;
		viewMat[3] = 0.0f;
		
		viewMat[4] = eyeData[i].right.y;
		viewMat[5] = eyeData[i].up.y;
		viewMat[6] = -eyeData[i].forward.y;
		viewMat[7] = 0.0f;
		
		viewMat[8] = eyeData[i].right.z;
		viewMat[9] = eyeData[i].up.z;
		viewMat[10] = -eyeData[i].forward.z;
		viewMat[11] = 0.0f;
		
		// Translation part
		viewMat[12] = -eyeData[i].position.dot(eyeData[i].right);
		viewMat[13] = -eyeData[i].position.dot(eyeData[i].up);
		viewMat[14] = eyeData[i].position.dot(eyeData[i].forward);
		viewMat[15] = 1.0f;
		
		eyeData[i].viewMatrix = CMatrix44f(viewMat);
	}
}

void CVRSystem::SetupEyeCamera(Eye eye)
{
	if (!initialized || eye >= EYE_COUNT)
		return;
		
	// Acquire swapchain image
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	uint32_t imageIndex;
	xrAcquireSwapchainImage(swapchains[eye].handle, &acquireInfo, &imageIndex);
	
	// Wait for swapchain image to be ready
	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	xrWaitSwapchainImage(swapchains[eye].handle, &waitInfo);
	
	// Store color texture from swapchain
	eyeData[eye].colorTexture = swapchains[eye].images[imageIndex].image;
	
	// Bind framebuffer and attach swapchain image
	glBindFramebuffer(GL_FRAMEBUFFER, eyeData[eye].framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
		GL_TEXTURE_2D, eyeData[eye].colorTexture, 0);
	
	// Set viewport
	glViewport(0, 0, eyeData[eye].width, eyeData[eye].height);
	
	// Clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void CVRSystem::SubmitEyeTexture(Eye eye)
{
	if (!initialized || eye >= EYE_COUNT)
		return;
		
	// Release swapchain image
	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	xrReleaseSwapchainImage(swapchains[eye].handle, &releaseInfo);
}

void CVRSystem::Present()
{
	if (!initialized || !session)
		return;
		
	// Prepare projection layer
	XrCompositionLayerProjectionView projectionViews[EYE_COUNT];
	
	for (int i = 0; i < EYE_COUNT; i++) {
		projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projectionViews[i].next = nullptr;
		projectionViews[i].pose = views[i].pose;
		projectionViews[i].fov = views[i].fov;
		projectionViews[i].subImage.swapchain = swapchains[i].handle;
		projectionViews[i].subImage.imageRect.offset = {0, 0};
		projectionViews[i].subImage.imageRect.extent = {swapchains[i].width, swapchains[i].height};
		projectionViews[i].subImage.imageArrayIndex = 0;
	}
	
	XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	projectionLayer.space = playSpace;
	projectionLayer.viewCount = EYE_COUNT;
	projectionLayer.views = projectionViews;
	
	const XrCompositionLayerBaseHeader* layers[] = {
		reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer)
	};
	
	// End frame
	XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
	frameEndInfo.displayTime = frameState.predictedDisplayTime;
	frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	frameEndInfo.layerCount = 1;
	frameEndInfo.layers = layers;
	
	xrEndFrame(session, &frameEndInfo);
}

void CVRSystem::GetRecommendedRenderTargetSize(int32_t& width, int32_t& height) const
{
	if (viewConfigs.empty()) {
		width = 2016;
		height = 2240;
		return;
	}
	
	width = viewConfigs[0].recommendedImageRectWidth;
	height = viewConfigs[0].recommendedImageRectHeight;
}

void CVRSystem::PollEvents()
{
	XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(instance, &eventData) == XR_SUCCESS) {
		switch (eventData.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				XrEventDataSessionStateChanged* stateEvent =
					reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
				sessionState = stateEvent->state;
				LOG_L(L_INFO, "[VR] Session state changed to %d", sessionState);
				
				switch (sessionState) {
					case XR_SESSION_STATE_READY:
						LOG_L(L_INFO, "[VR] Session is READY");
						break;
					case XR_SESSION_STATE_SYNCHRONIZED:
						LOG_L(L_INFO, "[VR] Session is SYNCHRONIZED");
						break;
					case XR_SESSION_STATE_VISIBLE:
						LOG_L(L_INFO, "[VR] Session is VISIBLE");
						break;
					case XR_SESSION_STATE_FOCUSED:
						LOG_L(L_INFO, "[VR] Session is FOCUSED - HMD should display content now");
						break;
					case XR_SESSION_STATE_STOPPING:
						LOG_L(L_INFO, "[VR] Session is STOPPING");
						if (session != XR_NULL_HANDLE) {
							xrEndSession(session);
						}
						break;
					case XR_SESSION_STATE_EXITING:
						LOG_L(L_INFO, "[VR] Session is EXITING");
						sessionRunning = false;
						break;
					case XR_SESSION_STATE_LOSS_PENDING:
						LOG_L(L_WARNING, "[VR] Session loss pending");
						sessionRunning = false;
						break;
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				LOG_L(L_WARNING, "[VR] Instance loss pending");
				sessionRunning = false;
				break;
			default:
				break;
		}
		eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
	}
}

bool CVRSystem::CreateActions()
{
	// Create action set
	XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	strcpy(actionSetInfo.actionSetName, "gameplay");
	strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
	actionSetInfo.priority = 0;
	
	XrResult result = xrCreateActionSet(instance, &actionSetInfo, &actionSet);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create action set: %d", result);
		return false;
	}
	
	// Get hand paths
	xrStringToPath(instance, "/user/hand/left", &leftHandPath);
	xrStringToPath(instance, "/user/hand/right", &rightHandPath);
	
	// Create joystick actions
	XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
	actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
	strcpy(actionInfo.actionName, "joystick_left");
	strcpy(actionInfo.localizedActionName, "Left Joystick");
	actionInfo.countSubactionPaths = 1;
	actionInfo.subactionPaths = &leftHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &joystickLeftAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create left joystick action: %d", result);
		return false;
	}
	
	strcpy(actionInfo.actionName, "joystick_right");
	strcpy(actionInfo.localizedActionName, "Right Joystick");
	actionInfo.subactionPaths = &rightHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &joystickRightAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create right joystick action: %d", result);
		return false;
	}
	
	// Create grip actions
	actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
	strcpy(actionInfo.actionName, "grip_left");
	strcpy(actionInfo.localizedActionName, "Left Grip");
	actionInfo.subactionPaths = &leftHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &gripLeftAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create left grip action: %d", result);
		return false;
	}
	
	strcpy(actionInfo.actionName, "grip_right");
	strcpy(actionInfo.localizedActionName, "Right Grip");
	actionInfo.subactionPaths = &rightHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &gripRightAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create right grip action: %d", result);
		return false;
	}
	
	// Create trigger actions
	strcpy(actionInfo.actionName, "trigger_left");
	strcpy(actionInfo.localizedActionName, "Left Trigger");
	actionInfo.subactionPaths = &leftHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &triggerLeftAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create left trigger action: %d", result);
		return false;
	}
	
	strcpy(actionInfo.actionName, "trigger_right");
	strcpy(actionInfo.localizedActionName, "Right Trigger");
	actionInfo.subactionPaths = &rightHandPath;
	result = xrCreateAction(actionSet, &actionInfo, &triggerRightAction);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to create right trigger action: %d", result);
		return false;
	}
	
	LOG_L(L_INFO, "[VR] Created VR actions successfully");
	return true;
}

bool CVRSystem::AttachActionSet()
{
	// Suggest bindings for controllers
	XrPath interactionProfilePath;
	xrStringToPath(instance, "/interaction_profiles/valve/index_controller", &interactionProfilePath);
	
	std::vector<XrActionSuggestedBinding> bindings;
	
	// Left joystick
	XrPath path;
	xrStringToPath(instance, "/user/hand/left/input/thumbstick", &path);
	bindings.push_back({joystickLeftAction, path});
	
	// Right joystick
	xrStringToPath(instance, "/user/hand/right/input/thumbstick", &path);
	bindings.push_back({joystickRightAction, path});
	
	// Grips
	xrStringToPath(instance, "/user/hand/left/input/squeeze/click", &path);
	bindings.push_back({gripLeftAction, path});
	xrStringToPath(instance, "/user/hand/right/input/squeeze/click", &path);
	bindings.push_back({gripRightAction, path});
	
	// Triggers
	xrStringToPath(instance, "/user/hand/left/input/trigger/click", &path);
	bindings.push_back({triggerLeftAction, path});
	xrStringToPath(instance, "/user/hand/right/input/trigger/click", &path);
	bindings.push_back({triggerRightAction, path});
	
	XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggestedBindings.interactionProfile = interactionProfilePath;
	suggestedBindings.suggestedBindings = bindings.data();
	suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
	
	XrResult result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
	if (XR_FAILED(result)) {
		LOG_L(L_WARNING, "[VR] Failed to suggest interaction profile bindings: %d", result);
	}
	
	// Attach action set to session
	XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &actionSet;
	
	result = xrAttachSessionActionSets(session, &attachInfo);
	if (XR_FAILED(result)) {
		LOG_L(L_ERROR, "[VR] Failed to attach action sets: %d", result);
		return false;
	}
	
	LOG_L(L_INFO, "[VR] Attached action sets successfully");
	return true;
}

void CVRSystem::UpdateControllerInput()
{
	if (!sessionRunning || !session || actionSet == XR_NULL_HANDLE)
		return;
	
	// Sync actions
	XrActiveActionSet activeActionSet{actionSet, XR_NULL_PATH};
	XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	
	XrResult result = xrSyncActions(session, &syncInfo);
	if (XR_FAILED(result)) {
		return;
	}
	
	// Get left joystick state
	XrActionStateVector2f joystickState{XR_TYPE_ACTION_STATE_VECTOR2F};
	XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
	getInfo.action = joystickLeftAction;
	getInfo.subactionPath = leftHandPath;
	
	result = xrGetActionStateVector2f(session, &getInfo, &joystickState);
	if (XR_SUCCEEDED(result) && joystickState.isActive) {
		leftJoystick.x = joystickState.currentState.x;
		leftJoystick.y = joystickState.currentState.y;
	} else {
		leftJoystick.x = 0.0f;
		leftJoystick.y = 0.0f;
	}
	
	// Get right joystick state
	getInfo.action = joystickRightAction;
	getInfo.subactionPath = rightHandPath;
	result = xrGetActionStateVector2f(session, &getInfo, &joystickState);
	if (XR_SUCCEEDED(result) && joystickState.isActive) {
		rightJoystick.x = joystickState.currentState.x;
		rightJoystick.y = joystickState.currentState.y;
	} else {
		rightJoystick.x = 0.0f;
		rightJoystick.y = 0.0f;
	}
	
	// Get grip states
	XrActionStateBoolean boolState{XR_TYPE_ACTION_STATE_BOOLEAN};
	getInfo.action = gripLeftAction;
	getInfo.subactionPath = leftHandPath;
	result = xrGetActionStateBoolean(session, &getInfo, &boolState);
	leftGrip = (XR_SUCCEEDED(result) && boolState.isActive && boolState.currentState);
	
	getInfo.action = gripRightAction;
	getInfo.subactionPath = rightHandPath;
	result = xrGetActionStateBoolean(session, &getInfo, &boolState);
	rightGrip = (XR_SUCCEEDED(result) && boolState.isActive && boolState.currentState);
	
	// Get trigger states
	getInfo.action = triggerLeftAction;
	getInfo.subactionPath = leftHandPath;
	result = xrGetActionStateBoolean(session, &getInfo, &boolState);
	leftTrigger = (XR_SUCCEEDED(result) && boolState.isActive && boolState.currentState);
	
	getInfo.action = triggerRightAction;
	getInfo.subactionPath = rightHandPath;
	result = xrGetActionStateBoolean(session, &getInfo, &boolState);
	rightTrigger = (XR_SUCCEEDED(result) && boolState.isActive && boolState.currentState);
}

#endif // USE_VR
