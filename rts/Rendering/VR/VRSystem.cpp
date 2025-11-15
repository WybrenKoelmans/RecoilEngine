#include "Rendering/VR/VRSystem.h"

#include "System/Log/ILog.h"

#if defined(USE_VR)

#include "Game/Camera.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"
#include "Sim/Misc/GlobalConstants.h"
#include "System/Config/ConfigHandler.h"
#include "System/Exceptions.h"
#include "System/SpringMath.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr XrVersion kRequiredOpenXRVersion = XR_MAKE_VERSION(1, 0, 0);
constexpr const char* kApplicationName = "RecoilEngine";
constexpr const char* kEngineName = "RecoilEngine";

}

namespace vr {

namespace {

static const char* XrSessionStateToString(XrSessionState state)
{
	switch (state) {
		case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
		case XR_SESSION_STATE_IDLE: return "IDLE";
		case XR_SESSION_STATE_READY: return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
		case XR_SESSION_STATE_STOPPING: return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING: return "EXITING";
		default: return "?";
	}
}

static bool CheckXrResult(XrResult res, const char* what)
{
	if (XR_SUCCEEDED(res))
		return true;

	char buffer[XR_MAX_RESULT_STRING_SIZE] = {0};
	xrResultToString(XR_NULL_HANDLE, res, buffer);
	LOG_L(L_ERROR, "[OpenXR] %s failed: %s", what, buffer);
	return false;
}

static bool GetSDLOpenGLBindings(SDL_Window* window, XrGraphicsBindingOpenGLWin32KHR& binding)
{
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);

	if (SDL_GetWindowWMInfo(window, &wmInfo) != SDL_TRUE) {
		LOG_L(L_ERROR, "[OpenXR] SDL_GetWindowWMInfo failed: %s", SDL_GetError());
		return false;
	}

	binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	binding.next = nullptr;
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();

	if (binding.hDC == nullptr || binding.hGLRC == nullptr) {
		LOG_L(L_ERROR, "[OpenXR] Unable to fetch current OpenGL context for XR session");
		return false;
	}

	return true;
}

static CQuaternion FromXr(const XrQuaternionf& q)
{
	return CQuaternion(q.x, q.y, q.z, q.w);
}

static float3 FromXr(const XrVector3f& v)
{
	constexpr float metersToElmos = static_cast<float>(SQUARE_SIZE);
	return float3(v.x, v.y, -v.z) * metersToElmos; // flip Z to match engine forward and scale to engine units
}

static XrPosef IdentityPose()
{
	XrPosef pose{};
	pose.orientation.w = 1.0f;
	pose.position = {0.0f, 0.0f, 0.0f};
	return pose;
}

}

OpenXRSystem::~OpenXRSystem()
{
	Shutdown();
}

bool OpenXRSystem::Initialize(CGlobalRendering& globalRendering)
{
	if (initialized)
		return true;

	if (!InitializeInstance())
		return false;
	if (!InitializeSystem())
		return false;
	if (!InitializeSession(globalRendering))
		return false;
	if (!InitializeSwapchains())
		return false;

	projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	projectionLayer.next = nullptr;
	projectionLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	projectionLayer.space = appSpace;

	initialized = true;
	LOG_L(L_INFO, "[OpenXR] Initialized successfully");
	return true;
}

void OpenXRSystem::Shutdown()
{
	if (!initialized)
		return;

	ReleaseSwapchainImages();
	DestroySwapchains();
	DestroySession();
	DestroyInstance();

	swapchains.clear();
	views.clear();
	projectionViews.clear();

	referenceOrientation = CQuaternion();
	initialized = false;
	sessionRunning = false;
	frameBegun = false;
	frameSubmitted = false;
	gameActive = false;
	blockDesktopSwap = false;
}

void OpenXRSystem::PollEvents()
{
	if (!initialized)
		return;

	XrEventDataBuffer eventBuffer{XR_TYPE_EVENT_DATA_BUFFER};

	while (xrPollEvent(instance, &eventBuffer) == XR_SUCCESS) {
		switch (eventBuffer.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				auto& stateChanged = *reinterpret_cast<XrEventDataSessionStateChanged*>(&eventBuffer);
				LOG_L(L_INFO, "[OpenXR] Session state changed: %s", XrSessionStateToString(stateChanged.state));

				if (stateChanged.state == XR_SESSION_STATE_READY) {
					referenceOrientationSet = false;
					XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
					beginInfo.primaryViewConfigurationType = viewConfigType;

					if (CheckXrResult(xrBeginSession(session, &beginInfo), "xrBeginSession")) {
						sessionRunning = true;
					}
				} else if (stateChanged.state == XR_SESSION_STATE_STOPPING) {
					CheckXrResult(xrEndSession(session), "xrEndSession");
					sessionRunning = false;
					referenceOrientationSet = false;
				} else if (stateChanged.state == XR_SESSION_STATE_EXITING || stateChanged.state == XR_SESSION_STATE_LOSS_PENDING) {
					sessionRunning = false;
					referenceOrientationSet = false;
				}

				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				LOG_L(L_DEBUG, "[OpenXR] Reference space change pending");
				break;
			}
			default:
				LOG_L(L_DEBUG, "[OpenXR] Ignored event type %u", eventBuffer.type);
				break;
		}

		eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};
	}

	blockDesktopSwap = (gameActive && sessionRunning);
}

void OpenXRSystem::SetGameActive(bool active)
{
	gameActive = active;
	blockDesktopSwap = (active && sessionRunning);
}

bool OpenXRSystem::ShouldBlockDesktopSwap() const
{
	return blockDesktopSwap && frameSubmitted;
}

bool OpenXRSystem::BeginFrame()
{
	if (!initialized || !sessionRunning)
		return false;

	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState frameState{XR_TYPE_FRAME_STATE};

	if (!CheckXrResult(xrWaitFrame(session, &waitInfo, &frameState), "xrWaitFrame"))
		return false;

	predictedDisplayTime = frameState.predictedDisplayTime;

	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	if (!CheckXrResult(xrBeginFrame(session, &beginInfo), "xrBeginFrame"))
		return false;

	frameSubmitted = false;

	if (!AcquireSwapchainImages()) {
		LOG_L(L_WARNING, "[OpenXR] AcquireSwapchainImages failed; submitting empty frame");

		XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
		endInfo.displayTime = predictedDisplayTime;
		endInfo.environmentBlendMode = environmentBlendMode;
		endInfo.layerCount = 0;
		endInfo.layers = nullptr;

		const bool submitted = CheckXrResult(xrEndFrame(session, &endInfo), "xrEndFrame(empty)");
		frameSubmitted = submitted;
		ReleaseSwapchainImages();
		return false;
	}

	frameBegun = true;
	return true;
}

bool OpenXRSystem::RenderEyes(const EyeRenderCallback& callback, const CCamera& baseCamera)
{
	if (!frameBegun)
		return false;

	bool renderedAny = false;

	const uint32_t eyeCount = std::min(activeViewCount, static_cast<uint32_t>(swapchains.size()));

	for (uint32_t eye = 0; eye < eyeCount; ++eye) {
		EyeRenderTarget target = BuildEyeTarget(eye, baseCamera);
		if (target.framebuffer == 0)
			continue;

		callback(target);
		renderedAny = true;
	}

	return renderedAny;
}

void OpenXRSystem::EndFrame()
{
	if (!frameBegun)
		return;

	frameBegun = false;

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = predictedDisplayTime;
	endInfo.environmentBlendMode = environmentBlendMode;
	const XrCompositionLayerBaseHeader* layerPtrs[1] = {nullptr};

	if (!projectionViews.empty() && activeViewCount > 0) {
		projectionLayer.space = appSpace;
		projectionLayer.viewCount = activeViewCount;
		projectionLayer.views = projectionViews.data();
		projectionLayer.layerFlags = 0;
		if (environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)
			projectionLayer.layerFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

		layerPtrs[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer);
		endInfo.layerCount = 1;
		endInfo.layers = layerPtrs;
	} else {
		endInfo.layerCount = 0;
		endInfo.layers = nullptr;
	}

	ReleaseSwapchainImages();
	const bool submitted = CheckXrResult(xrEndFrame(session, &endInfo), "xrEndFrame");

	frameSubmitted = submitted;
}

void OpenXRSystem::RecenterReferenceOrientation(const CQuaternion& currentHeadOrientation)
{
	referenceOrientation = currentHeadOrientation;
	if (!referenceOrientation.Normalized())
		referenceOrientation.Normalize();
}

bool OpenXRSystem::InitializeInstance()
{
	XrApplicationInfo appInfo{};
	std::strncpy(appInfo.applicationName, kApplicationName, XR_MAX_APPLICATION_NAME_SIZE - 1);
	std::strncpy(appInfo.engineName, kEngineName, XR_MAX_ENGINE_NAME_SIZE - 1);
	appInfo.applicationVersion = 1;
	appInfo.engineVersion = 1;
	appInfo.apiVersion = kRequiredOpenXRVersion;

	uint32_t layerCount = 0;
	if (!CheckXrResult(xrEnumerateApiLayerProperties(0, &layerCount, nullptr), "xrEnumerateApiLayerProperties"))
		return false;
	std::vector<XrApiLayerProperties> layers(layerCount, {XR_TYPE_API_LAYER_PROPERTIES});
	if (layerCount > 0)
		CheckXrResult(xrEnumerateApiLayerProperties(layerCount, &layerCount, layers.data()), "xrEnumerateApiLayerProperties");

	uint32_t extensionCount = 0;
	if (!CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr), "xrEnumerateInstanceExtensionProperties"))
		return false;
	std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
	if (extensionCount > 0)
		CheckXrResult(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()), "xrEnumerateInstanceExtensionProperties");

	bool hasOpenGL = false;
	bool hasWin32 = false;
	for (const auto& ext : extensions) {
		if (std::strcmp(ext.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0)
			hasOpenGL = true;
		if (std::strcmp(ext.extensionName, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME) == 0)
			hasWin32 = true;
	}

	std::vector<const char*> enabledExtensions;
	if (hasOpenGL)
		enabledExtensions.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	if (hasWin32)
		enabledExtensions.push_back(XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);

	if (!hasOpenGL) {
		LOG_L(L_ERROR, "[OpenXR] Required extension XR_KHR_opengl_enable not available");
		return false;
	}

	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.applicationInfo = appInfo;
	instanceInfo.enabledExtensionCount = enabledExtensions.size();
	instanceInfo.enabledExtensionNames = enabledExtensions.data();

	if (!CheckXrResult(xrCreateInstance(&instanceInfo, &instance), "xrCreateInstance"))
		return false;

	PFN_xrVoidFunction func = nullptr;
	if (!CheckXrResult(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", &func), "xrGetOpenGLGraphicsRequirementsKHR"))
		return false;
	getGraphicsRequirements = reinterpret_cast<PFN_xrGetOpenGLGraphicsRequirementsKHR>(func);

	referenceOrientationSet = false;

	return (instance != XR_NULL_HANDLE);
}

bool OpenXRSystem::InitializeSystem()
{
	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	if (!CheckXrResult(xrGetSystem(instance, &systemInfo, &systemId), "xrGetSystem"))
		return false;

	viewConfigProps = {XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
	if (!CheckXrResult(xrGetViewConfigurationProperties(instance, systemId, viewConfigType, &viewConfigProps), "xrGetViewConfigurationProperties"))
		return false;

	uint32_t viewCountOutput = 0;
	xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, 0, &viewCountOutput, nullptr);
	if (viewCountOutput == 0) {
		LOG_L(L_ERROR, "[OpenXR] No view configuration views available");
		return false;
	}

	std::vector<XrViewConfigurationView> viewConfigViews(viewCountOutput, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
	if (!CheckXrResult(xrEnumerateViewConfigurationViews(instance, systemId, viewConfigType, viewCountOutput, &viewCountOutput, viewConfigViews.data()), "xrEnumerateViewConfigurationViews"))
		return false;

	viewCount = viewCountOutput;
	activeViewCount = viewCount;
	viewConfigurationViews = viewConfigViews;

	uint32_t blendModeCount = 0;
	if (!CheckXrResult(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigType, 0, &blendModeCount, nullptr), "xrEnumerateEnvironmentBlendModes"))
		return false;

	std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
	if (!CheckXrResult(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigType, blendModeCount, &blendModeCount, blendModes.data()), "xrEnumerateEnvironmentBlendModes"))
		return false;

	auto pickBlendMode = [&]() -> XrEnvironmentBlendMode {
		for (const XrEnvironmentBlendMode mode : blendModes) {
			if (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
				return mode;
		}
		for (const XrEnvironmentBlendMode mode : blendModes) {
			if (mode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE)
				return mode;
		}
		for (const XrEnvironmentBlendMode mode : blendModes) {
			if (mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)
				return mode;
		}
		return XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;
	};

	environmentBlendMode = pickBlendMode();
	if (environmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM) {
		LOG_L(L_ERROR, "[OpenXR] No supported environment blend mode available");
		return false;
	}
	swapchains.resize(viewCount);
	views.resize(viewCount);
	projectionViews.resize(viewCount);
	for (uint32_t i = 0; i < viewCount; ++i) {
		views[i].type = XR_TYPE_VIEW;
		views[i].next = nullptr;
		projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projectionViews[i].next = nullptr;
	}

	return true;
}

bool OpenXRSystem::InitializeSession(CGlobalRendering& globalRendering)
{
	if (getGraphicsRequirements == nullptr) {
		LOG_L(L_ERROR, "[OpenXR] Graphics requirements function is null");
		return false;
	}

	XrGraphicsRequirementsOpenGLKHR graphicsReq{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	if (!CheckXrResult(getGraphicsRequirements(instance, systemId, &graphicsReq), "xrGetOpenGLGraphicsRequirementsKHR"))
		return false;

	XrGraphicsBindingOpenGLWin32KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
	if (!GetSDLOpenGLBindings(globalRendering.GetWindow(), graphicsBinding))
		return false;

	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = systemId;

	if (!CheckXrResult(xrCreateSession(instance, &sessionInfo, &session), "xrCreateSession"))
		return false;

	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace = IdentityPose();

	if (!CheckXrResult(xrCreateReferenceSpace(session, &spaceInfo, &appSpace), "xrCreateReferenceSpace"))
		return false;

	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!CheckXrResult(xrCreateReferenceSpace(session, &spaceInfo, &viewSpace), "xrCreateReferenceSpace(view)"))
		return false;

	return true;
}

bool OpenXRSystem::InitializeSwapchains()
{
	uint32_t formatCount = 0;
	if (!CheckXrResult(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats"))
		return false;

	std::vector<int64_t> formats(formatCount);
	if (!CheckXrResult(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()), "xrEnumerateSwapchainFormats"))
		return false;

	int64_t selectedFormat = 0;
	for (int64_t format : formats) {
		if (format == GL_SRGB8_ALPHA8 || format == GL_RGBA8) {
			selectedFormat = format;
			break;
		}
	}

	if (selectedFormat == 0) {
		selectedFormat = formats.empty() ? GL_RGBA8 : formats.front();
	}

	for (uint32_t eye = 0; eye < viewCount; ++eye) {
		auto& swapchain = swapchains[eye];

		XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainInfo.format = selectedFormat;
		swapchainInfo.sampleCount = viewConfigurationViews[eye].recommendedSwapchainSampleCount;
		swapchainInfo.width = std::max<uint32_t>(1, viewConfigurationViews[eye].recommendedImageRectWidth);
		swapchainInfo.height = std::max<uint32_t>(1, viewConfigurationViews[eye].recommendedImageRectHeight);
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		if (!CheckXrResult(xrCreateSwapchain(session, &swapchainInfo, &swapchain.handle), "xrCreateSwapchain"))
			return false;

		swapchain.width = swapchainInfo.width;
		swapchain.height = swapchainInfo.height;

		uint32_t imageCount = 0;
		CheckXrResult(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages");
		swapchain.colorTextures.resize(imageCount, 0);
		swapchain.framebuffers.resize(imageCount, 0);
		swapchain.depthBuffers.resize(imageCount, 0);
		swapchain.activeImageIndex = kInvalidSwapchainIndex;

		std::vector<XrSwapchainImageOpenGLKHR> xrImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
		if (!CheckXrResult(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(xrImages.data())), "xrEnumerateSwapchainImages"))
			return false;

		for (uint32_t i = 0; i < imageCount; ++i) {
			swapchain.colorTextures[i] = xrImages[i].image;

			GLuint depth = 0;
			glGenRenderbuffers(1, &depth);
			glBindRenderbuffer(GL_RENDERBUFFER, depth);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, swapchain.width, swapchain.height);
			swapchain.depthBuffers[i] = depth;

			GLuint fbo = 0;
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, swapchain.colorTextures[i], 0);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

			const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0 };
			glDrawBuffers(1, drawBuffers);
			glReadBuffer(GL_COLOR_ATTACHMENT0);

			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				LOG_L(L_ERROR, "[OpenXR] Swapchain framebuffer incomplete (eye %u, image %u, status 0x%x)", eye, i, status);
				return false;
			}

			swapchain.framebuffers[i] = fbo;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glReadBuffer(GL_BACK);

	return true;
}

void OpenXRSystem::DestroySwapchains()
{
	if (swapchains.empty())
		return;

	for (auto& swapchain : swapchains) {
		for (GLuint fbo : swapchain.framebuffers) {
			if (fbo == 0)
				continue;
			GLuint id = fbo;
			glDeleteFramebuffers(1, &id);
		}
		for (GLuint depth : swapchain.depthBuffers) {
			if (depth == 0)
				continue;
			GLuint id = depth;
			glDeleteRenderbuffers(1, &id);
		}

		swapchain.framebuffers.clear();
		swapchain.depthBuffers.clear();
		swapchain.colorTextures.clear();
		swapchain.activeImageIndex = kInvalidSwapchainIndex;

		if (swapchain.handle != XR_NULL_HANDLE) {
			xrDestroySwapchain(swapchain.handle);
			swapchain.handle = XR_NULL_HANDLE;
		}
	}
}

void OpenXRSystem::DestroySession()
{
	if (viewSpace != XR_NULL_HANDLE) {
		xrDestroySpace(viewSpace);
		viewSpace = XR_NULL_HANDLE;
	}

	if (appSpace != XR_NULL_HANDLE) {
		xrDestroySpace(appSpace);
		appSpace = XR_NULL_HANDLE;
	}

	if (session != XR_NULL_HANDLE) {
		xrDestroySession(session);
		session = XR_NULL_HANDLE;
	}
}

void OpenXRSystem::DestroyInstance()
{
	if (instance != XR_NULL_HANDLE) {
		xrDestroyInstance(instance);
		instance = XR_NULL_HANDLE;
	}
}

bool OpenXRSystem::AcquireSwapchainImages()
{
	if (swapchains.empty())
		return false;

	activeViewCount = 0;

	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCountOutput = viewCount;

	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = viewConfigType;
	locateInfo.displayTime = predictedDisplayTime;
	locateInfo.space = appSpace;

	if (!CheckXrResult(xrLocateViews(session, &locateInfo, &viewState, viewCount, &viewCountOutput, views.data()), "xrLocateViews"))
		return false;

	activeViewCount = std::min(viewCountOutput, viewCount);
	if (activeViewCount == 0) {
		LOG_L(L_WARNING, "[OpenXR] No views located this frame (viewCountOutput=%u)", viewCountOutput);
		return false;
	}

	if ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
		LOG_L(L_WARNING, "[OpenXR] View orientation not valid this frame");
	}

	if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0) {
		LOG_L(L_WARNING, "[OpenXR] View position not valid this frame");
	}

	for (uint32_t eye = 0; eye < activeViewCount; ++eye) {
		auto& swapchain = swapchains[eye];
		XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
		uint32_t imageIndex = 0;

		if (!CheckXrResult(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex), "xrAcquireSwapchainImage"))
			return false;

		XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
		waitInfo.timeout = XR_INFINITE_DURATION;

		if (!CheckXrResult(xrWaitSwapchainImage(swapchain.handle, &waitInfo), "xrWaitSwapchainImage"))
			return false;

		swapchain.activeImageIndex = imageIndex;

		projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projectionViews[eye].pose = views[eye].pose;
		projectionViews[eye].fov = views[eye].fov;
		projectionViews[eye].subImage.swapchain = swapchain.handle;
		projectionViews[eye].subImage.imageRect.offset = {0, 0};
		projectionViews[eye].subImage.imageRect.extent = {swapchain.width, swapchain.height};
		projectionViews[eye].subImage.imageArrayIndex = 0;

		static int acquireLogCounter = 0;
		if (acquireLogCounter < 120) {
			const XrVector3f& xrPos = views[eye].pose.position;
			LOG_L(L_INFO, "[OpenXR] Acquire eye %u img %u size=%ux%u pose(m)=<%.3f %.3f %.3f>",
				eye, imageIndex, swapchain.width, swapchain.height,
				xrPos.x, xrPos.y, xrPos.z);
			acquireLogCounter++;
		}
	}

	for (uint32_t eye = activeViewCount; eye < swapchains.size(); ++eye) {
		swapchains[eye].activeImageIndex = kInvalidSwapchainIndex;
	}

	return true;
}

void OpenXRSystem::ReleaseSwapchainImages()
{
	for (auto& swapchain : swapchains) {
		if (swapchain.handle == XR_NULL_HANDLE || swapchain.activeImageIndex == kInvalidSwapchainIndex)
			continue;

		XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		CheckXrResult(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo), "xrReleaseSwapchainImage");
		swapchain.activeImageIndex = kInvalidSwapchainIndex;
	}
}

CQuaternion OpenXRSystem::ConvertOrientation(const CQuaternion& raw) const
{
	CQuaternion q = raw;
	if (!q.Normalized())
		q.Normalize();

	static const CQuaternion flipY = CQuaternion::MakeFrom(math::PI, UpVector);
	q = flipY * q;
	q.AssertNaNs();
	q.Normalize();
	return q;
}

EyeRenderTarget OpenXRSystem::BuildEyeTarget(int eyeIndex, const CCamera& baseCamera)
{
	EyeRenderTarget target;
	if (eyeIndex < 0 || static_cast<uint32_t>(eyeIndex) >= activeViewCount)
		return target;

	auto& swapchain = swapchains[eyeIndex];
	const uint32_t imageIndex = swapchain.activeImageIndex;
	if (imageIndex == kInvalidSwapchainIndex)
		return target;

	target.eyeIndex = eyeIndex;
	target.width = swapchain.width;
	target.height = swapchain.height;
	target.framebuffer = (imageIndex < swapchain.framebuffers.size()) ? swapchain.framebuffers[imageIndex] : 0;
	target.colorTexture = (imageIndex < swapchain.colorTextures.size()) ? swapchain.colorTextures[imageIndex] : 0;
	target.depthBuffer = (imageIndex < swapchain.depthBuffers.size()) ? swapchain.depthBuffers[imageIndex] : 0;

	const XrView& view = views[eyeIndex];
	const CQuaternion rawOrientation = FromXr(view.pose.orientation);
	const CQuaternion engineOrientation = ConvertOrientation(rawOrientation);

	if (!referenceOrientationSet) {
		referenceOrientation = engineOrientation;
		referenceOrientationSet = true;
	}

	if (referenceOrientation.Normalized()) {
		CQuaternion delta = referenceOrientation.Inverse();
		delta = delta * engineOrientation;
		delta.Normalize();
		target.relativeOrientation = delta;
	} else {
		target.relativeOrientation = engineOrientation;
	}

	target.absoluteOrientation = engineOrientation;

	float nearPlane = baseCamera.GetNearPlaneDist();
	float farPlane = baseCamera.GetFarPlaneDist();

	const float tanLeft = std::tan(view.fov.angleLeft);
	const float tanRight = std::tan(view.fov.angleRight);
	const float tanUp = std::tan(view.fov.angleUp);
	const float tanDown = std::tan(view.fov.angleDown);

	const float left = nearPlane * tanLeft;
	const float right = nearPlane * tanRight;
	const float top = nearPlane * tanUp;
	const float bottom = nearPlane * tanDown;

	target.projectionMatrix = baseCamera.GetClipControlMatrix() * CMatrix44f::PerspProj(left, right, bottom, top, nearPlane, farPlane);
	target.frustumLeft = left;
	target.frustumRight = right;
	target.frustumBottom = bottom;
	target.frustumTop = top;
	target.nearPlane = nearPlane;
	target.farPlane = farPlane;

	const float3 eyeOffset = FromXr(view.pose.position);
	const CQuaternion baseOrientation = CQuaternion::FromEulerYPR(baseCamera.GetRot());
	const float3 offsetWorld = baseOrientation.Rotate(eyeOffset);
	target.eyeOffsetWorld = offsetWorld;

	return target;
}

OpenXRSystem* GetOpenXRSystem()
{
	static OpenXRSystem instance;
	return &instance;
}

bool EnsureOpenXRSystem(CGlobalRendering& globalRendering)
{
	return GetOpenXRSystem()->Initialize(globalRendering);
}

void ShutdownOpenXRSystem()
{
	GetOpenXRSystem()->Shutdown();
}

} // namespace vr

#endif // defined(USE_VR)
