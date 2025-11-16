/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/VR/OpenXRRenderer.h"

#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/UniformConstants.h"

#include "Game/Game.h"
#include "Game/Camera.h"

#include "System/Log/ILog.h"
#include "System/Matrix44f.h"
#include "System/SpringMath.h"
#include "System/float3.h"
#include "System/float4.h"

#include <SDL2/SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <GL/glx.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

constexpr float kZNear = 0.05f;
constexpr float kZFar  = 65536.0f;
constexpr float kMetersToWorld = 1.0f;

struct Quaternion {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
};

Quaternion ToQuaternion(const XrQuaternionf& q)
{
	return {q.x, q.y, q.z, q.w};
}

Quaternion Multiply(const Quaternion& a, const Quaternion& b)
{
	return {
		(a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y),
		(a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
		(a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w),
		(a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z)
	};
}

Quaternion Conjugate(const Quaternion& q)
{
	return {-q.x, -q.y, -q.z, q.w};
}

float3 RotateVector(const Quaternion& q, const float3& v)
{
	const Quaternion vec{v.x, v.y, v.z, 0.0f};
	const Quaternion qConj = Conjugate(q);
	const Quaternion result = Multiply(Multiply(q, vec), qConj);
	return {result.x, result.y, result.z};
}

CMatrix44f QuaternionToMatrix(const Quaternion& q)
{
	const float xx = q.x * q.x;
	const float yy = q.y * q.y;
	const float zz = q.z * q.z;
	const float xy = q.x * q.y;
	const float xz = q.x * q.z;
	const float yz = q.y * q.z;
	const float wx = q.w * q.x;
	const float wy = q.w * q.y;
	const float wz = q.w * q.z;

	CMatrix44f m;
	m.LoadIdentity();

	m[0]  = 1.0f - 2.0f * (yy + zz);
	m[1]  = 2.0f * (xy + wz);
	m[2]  = 2.0f * (xz - wy);

	m[4]  = 2.0f * (xy - wz);
	m[5]  = 1.0f - 2.0f * (xx + zz);
	m[6]  = 2.0f * (yz + wx);

	m[8]  = 2.0f * (xz + wy);
	m[9]  = 2.0f * (yz - wx);
	m[10] = 1.0f - 2.0f * (xx + yy);

	return m;
}

float3 ToFloat3(const XrVector3f& v)
{
	return {v.x, v.y, v.z};
}

const char* SessionStateToString(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: break;
	}
	return "UNKNOWN";
}

} // namespace

namespace VR {

struct OpenXRRenderer::CameraState {
	CMatrix44f viewMatrix;
	CMatrix44f projMatrix;
	CMatrix44f clipMatrix;
	float4 frustumScales;
	float aspectRatio = 1.0f;
};

struct OpenXRRenderer::GlobalViewState {
	int viewPosX = 0;
	int viewPosY = 0;
	int viewSizeX = 1;
	int viewSizeY = 1;
	float pixelX = 1.0f;
	float pixelY = 1.0f;
	float aspectRatio = 1.0f;
};

struct OpenXRRenderer::SwapchainBuffers {
	std::vector<unsigned int> framebuffers;
	std::vector<unsigned int> depthBuffers;
};

OpenXRRenderer::OpenXRRenderer()
{}

OpenXRRenderer::~OpenXRRenderer()
{
	Shutdown();
}

bool OpenXRRenderer::Initialize(CGlobalRendering& gr)
{
	globalRendering = &gr;
	if (instance != XR_NULL_HANDLE)
		return true;

	// Enumerate available instance extensions for diagnostics
	uint32_t extensionCount = 0;
	xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	std::vector<XrExtensionProperties> extProps(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
	if (extensionCount > 0) {
		xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extProps.data());
		for (const auto& ep : extProps) {
			LOG_L(L_INFO, "[VR] Runtime extension available: %s specVersion=%u", ep.extensionName, ep.extensionVersion);
		}
	}

	bool haveDebugUtils = false;
	for (const auto& ep : extProps) {
		if (std::strcmp(ep.extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
			haveDebugUtils = true; break;
		}
	}

	std::vector<const char*> extensions; extensions.reserve(4);
	extensions.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	if (haveDebugUtils) {
		extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
		LOG_L(L_INFO, "[VR] Enabling XR_EXT_debug_utils extension");
	}

	char applicationName[XR_MAX_APPLICATION_NAME_SIZE] = {};
	std::snprintf(applicationName, sizeof(applicationName), "RecoilEngineVR");

	XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	std::memcpy(createInfo.applicationInfo.applicationName, applicationName, sizeof(applicationName));
	createInfo.applicationInfo.applicationVersion = 1;
	std::snprintf(createInfo.applicationInfo.engineName, sizeof(createInfo.applicationInfo.engineName), "RecoilEngine");
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	createInfo.enabledExtensionCount = extensions.size();
	createInfo.enabledExtensionNames = extensions.data();

	const XrResult createResult = xrCreateInstance(&createInfo, &instance);
	if (createResult != XR_SUCCESS) {
		LogXrError("xrCreateInstance", createResult);
		instance = XR_NULL_HANDLE;
		return false;
	}

	// Set up debug messenger if available
	if (haveDebugUtils) {
		reinterpret_cast<PFN_xrVoidFunction&>(pfnCreateDebugUtilsMessengerEXT) = nullptr;
		reinterpret_cast<PFN_xrVoidFunction&>(pfnDestroyDebugUtilsMessengerEXT) = nullptr;
		if (xrGetInstanceProcAddr(instance, "xrCreateDebugUtilsMessengerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateDebugUtilsMessengerEXT)) == XR_SUCCESS &&
		    xrGetInstanceProcAddr(instance, "xrDestroyDebugUtilsMessengerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyDebugUtilsMessengerEXT)) == XR_SUCCESS &&
		    pfnCreateDebugUtilsMessengerEXT != nullptr && pfnDestroyDebugUtilsMessengerEXT != nullptr) {
			XrDebugUtilsMessengerCreateInfoEXT dbgInfo{XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
			dbgInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
			dbgInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
			dbgInfo.userData = nullptr;
			dbgInfo.userCallback = [](XrDebugUtilsMessageSeverityFlagsEXT severity,
			XrDebugUtilsMessageTypeFlagsEXT type,
			const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
			void* userData) -> XrBool32 {
				LOG_L(L_INFO, "[VR][XR_DEBUG] %s: %s", callbackData->functionName, callbackData->message);
				return XR_FALSE;
			};
			if (pfnCreateDebugUtilsMessengerEXT(instance, &dbgInfo, &debugMessenger) != XR_SUCCESS) {
				LOG_L(L_WARNING, "[VR] Failed to create XR debug messenger");
			} else {
				LOG_L(L_INFO, "[VR] XR debug messenger created");
			}
		}
	}

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	const XrResult sysRes = xrGetSystem(instance, &systemInfo, &systemId);
	if (sysRes != XR_SUCCESS) {
		LogXrError("xrGetSystem", sysRes);
		return false;
	}

	reinterpret_cast<PFN_xrVoidFunction&>(pfnGetOpenGLGraphicsRequirementsKHR) = nullptr;
	const XrResult procRes = xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetOpenGLGraphicsRequirementsKHR));
	if (procRes != XR_SUCCESS || pfnGetOpenGLGraphicsRequirementsKHR == nullptr) {
		LogXrError("xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)", procRes);
		return false;
	}

	XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	const XrResult grRes = pfnGetOpenGLGraphicsRequirementsKHR(instance, systemId, &requirements);
	if (grRes != XR_SUCCESS) {
		LogXrError("xrGetOpenGLGraphicsRequirementsKHR", grRes);
		return false;
	}
	LOG_L(L_INFO, "[VR] OpenGL graphics requirements: min=%u.%u.%u max=%u.%u.%u", XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported), XR_VERSION_PATCH(requirements.minApiVersionSupported), XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported), XR_VERSION_PATCH(requirements.maxApiVersionSupported));

	// Query system properties for debug
	XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
	if (xrGetSystemProperties(instance, systemId, &sysProps) == XR_SUCCESS) {
		LOG_L(L_INFO, "[VR] System properties: systemName='%s' vendorId=%u maxWidth=%u maxHeight=%u maxLayers=%u", sysProps.systemName, sysProps.vendorId, sysProps.graphicsProperties.maxSwapchainImageWidth, sysProps.graphicsProperties.maxSwapchainImageHeight, sysProps.graphicsProperties.maxLayerCount);
	}

	if (!CreateSession(gr))
		return false;

	if (!CreateReferenceSpace())
		return false;

	if (!CreateSwapchainResources()) {
		LOG_L(L_ERROR, "[VR] CreateSwapchainResources failed");
		return false;
	}

	// Immediately poll events to transition session to READY and begin it.
	for (int i = 0; i < 8 && !sessionRunning; ++i) {
		PollEvents();
	}
	if (!sessionRunning) {
		LOG_L(L_WARNING, "[VR] Session not running after initial event polling (state=%d)", (int)sessionState);
	} else {
		LOG_L(L_INFO, "[VR] Session running after initialization (state=%d)", (int)sessionState);
	}

	LOG_L(L_INFO, "[VR] OpenXR initialized (%ux%u, views=%u)", swapchainWidth, swapchainHeight, viewCount);
	return true;
}

void OpenXRRenderer::Shutdown()
{
	sessionRunning = false;

	DestroySwapchainResources();

	if (referenceSpace != XR_NULL_HANDLE) {
		xrDestroySpace(referenceSpace);
		referenceSpace = XR_NULL_HANDLE;
	}

	if (session != XR_NULL_HANDLE) {
		xrDestroySession(session);
		session = XR_NULL_HANDLE;
	}

	if (instance != XR_NULL_HANDLE) {
		xrDestroyInstance(instance);
		instance = XR_NULL_HANDLE;
	}
}

bool OpenXRRenderer::CreateSession(CGlobalRendering& gr)
{
	SDL_Window* window = gr.GetWindow();
	auto context = gr.GetContext();
	if (window == nullptr || context == nullptr) {
		LOG_L(L_ERROR, "[VR] Missing SDL window or GL context for OpenXR session");
		return false;
	}

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
		LOG_L(L_ERROR, "[VR] SDL_GetWindowWMInfo failed: %s", SDL_GetError());
		return false;
	}

	#ifdef _WIN32
	XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
	binding.hDC = wmInfo.info.win.hdc;
	binding.hGLRC = static_cast<HGLRC>(context);
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &binding;
	#elif defined(__linux__)
	XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
	binding.xDisplay = wmInfo.info.x11.display;
	binding.glxDrawable = wmInfo.info.x11.window;
	binding.glxContext = static_cast<GLXContext>(context);
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &binding;
	#else
	#error "Unsupported platform for OpenXR OpenGL binding"
	#endif

	sessionInfo.systemId = systemId;

	const XrResult createSessionRes = xrCreateSession(instance, &sessionInfo, &session);
	if (createSessionRes != XR_SUCCESS) {
		LogXrError("xrCreateSession", createSessionRes);
		return false;
	}

	uint32_t blendCount = 0;
	if (xrEnumerateEnvironmentBlendModes(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendCount, nullptr) != XR_SUCCESS || blendCount == 0) {
		LOG_L(L_WARNING, "[VR] Failed to enumerate environment blend modes, defaulting to OPAQUE");
		environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	} else {
		std::vector<XrEnvironmentBlendMode> blendModes(blendCount);
		xrEnumerateEnvironmentBlendModes(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendCount, &blendCount, blendModes.data());
		environmentBlendMode = blendModes.front();
	}

	return true;
}

bool OpenXRRenderer::CreateReferenceSpace()
{
	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
	spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

	const XrResult spaceRes = xrCreateReferenceSpace(session, &spaceInfo, &referenceSpace);
	if (spaceRes != XR_SUCCESS) {
		LogXrError("xrCreateReferenceSpace", spaceRes);
		return false;
	}
	return true;
}

bool OpenXRRenderer::CreateSwapchainResources()
{
	uint32_t viewCountOutput = 0;
	if (xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCountOutput, nullptr) != XR_SUCCESS || viewCountOutput == 0) {
		LOG_L(L_ERROR, "[VR] Failed to enumerate view configuration views");
		return false;
	}

	viewCount = viewCountOutput;
	viewConfigurationViews.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
	if (xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCountOutput, viewConfigurationViews.data()) != XR_SUCCESS) {
		LOG_L(L_ERROR, "[VR] Failed to query view configuration view details");
		return false;
	}

	views.assign(viewCount, {XR_TYPE_VIEW});

	swapchainWidth = viewConfigurationViews[0].recommendedImageRectWidth;
	swapchainHeight = viewConfigurationViews[0].recommendedImageRectHeight;
	const uint32_t sampleCount = std::max(1u, viewConfigurationViews[0].recommendedSwapchainSampleCount);

	uint32_t formatCount = 0;
	if (xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr) != XR_SUCCESS || formatCount == 0) {
		LOG_L(L_ERROR, "[VR] Failed to enumerate swapchain formats");
		return false;
	}

	std::vector<int64_t> formats(formatCount);
	xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
	for (size_t i = 0; i < formats.size(); ++i) {
		LOG_L(L_INFO, "[VR] Swapchain format[%zu]=%lld", i, (long long)formats[i]);
	}

	const std::array<int64_t, 3> preferredFormats = {
		GL_SRGB8_ALPHA8,
		GL_RGBA8,
		GL_RGBA16F
	};

	colorSwapchainFormat = 0;
	for (int64_t preferred : preferredFormats) {
		if (std::find(formats.begin(), formats.end(), preferred) != formats.end()) {
			colorSwapchainFormat = preferred;
			break;
		}
	}

	if (colorSwapchainFormat == 0) {
		colorSwapchainFormat = formats.front();
		LOG_L(L_WARNING, "[VR] Using runtime-provided swapchain format %lld", static_cast<long long>(colorSwapchainFormat));
	}

	XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	swapchainInfo.arraySize = viewCount;
	swapchainInfo.faceCount = 1;
	swapchainInfo.mipCount = 1;
	swapchainInfo.sampleCount = sampleCount;
	swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainInfo.format = colorSwapchainFormat;
	swapchainInfo.width = swapchainWidth;
	swapchainInfo.height = swapchainHeight;

	const XrResult swapRes = xrCreateSwapchain(session, &swapchainInfo, &colorSwapchain);
	if (swapRes != XR_SUCCESS) {
		LogXrError("xrCreateSwapchain", swapRes);
		return false;
	}

	uint32_t swapchainImageCount = 0;
	if (xrEnumerateSwapchainImages(colorSwapchain, 0, &swapchainImageCount, nullptr) != XR_SUCCESS || swapchainImageCount == 0) {
		LOG_L(L_ERROR, "[VR] Failed to enumerate swapchain images");
		return false;
	}

	colorImages.assign(swapchainImageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
	const XrResult enumerateImagesRes = xrEnumerateSwapchainImages(colorSwapchain, swapchainImageCount, &swapchainImageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(colorImages.data()));
	if (enumerateImagesRes != XR_SUCCESS) {
		LogXrError("xrEnumerateSwapchainImages", enumerateImagesRes);
		return false;
	}

	swapchainBuffers.resize(swapchainImageCount);
	return EnsureFramebuffers();
}

void OpenXRRenderer::DestroySwapchainResources()
{
	if (!swapchainBuffers.empty()) {
		for (auto& buffers : swapchainBuffers) {
			if (!buffers.framebuffers.empty())
				glDeleteFramebuffers(buffers.framebuffers.size(), buffers.framebuffers.data());
			if (!buffers.depthBuffers.empty())
				glDeleteRenderbuffers(buffers.depthBuffers.size(), buffers.depthBuffers.data());
		}
	}
	swapchainBuffers.clear();
	colorImages.clear();

	if (colorSwapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(colorSwapchain);
		colorSwapchain = XR_NULL_HANDLE;
	}
}

bool OpenXRRenderer::EnsureFramebuffers()
{
	LOG_L(L_INFO, "[VR] EnsureFramebuffers begin (swapchainImages=%zu, viewCount=%u)", swapchainBuffers.size(), viewCount);
	// Verify required GL capabilities for layered framebuffer attachments.
	GLint maxArrayLayers = 0;
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
	if (maxArrayLayers < static_cast<GLint>(viewCount)) {
		LOG_L(L_ERROR, "[VR] GL_MAX_ARRAY_TEXTURE_LAYERS=%d insufficient for viewCount=%u; disabling VR", (int)maxArrayLayers, viewCount);
		return false;
	}

	const char* glVersionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	if (glVersionStr == nullptr) {
		LOG_L(L_ERROR, "[VR] Failed to query GL_VERSION; disabling VR");
		return false;
	}

	for (size_t idx = 0; idx < swapchainBuffers.size(); ++idx) {
		auto& buffers = swapchainBuffers[idx];
		buffers.framebuffers.resize(viewCount, 0);
		buffers.depthBuffers.resize(viewCount, 0);

		glGenFramebuffers(viewCount, buffers.framebuffers.data());
		glGenRenderbuffers(viewCount, buffers.depthBuffers.data());

		for (uint32_t eye = 0; eye < viewCount; ++eye) {
			glBindFramebuffer(GL_FRAMEBUFFER, buffers.framebuffers[eye]);
			if (GLenum e = glGetError(); e != GL_NO_ERROR) LOG_L(L_ERROR, "[VR] GL error 0x%x after glBindFramebuffer", e);
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorImages[idx].image, 0, eye);
			GLenum fbTexErr = glGetError();
			if (fbTexErr != GL_NO_ERROR) {
				LOG_L(L_ERROR, "[VR] glFramebufferTextureLayer error 0x%x for image=%u layer=%u (GL_VERSION=%s)", fbTexErr, colorImages[idx].image, eye, glVersionStr);
				return false;
			}

			glBindRenderbuffer(GL_RENDERBUFFER, buffers.depthBuffers[eye]);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, swapchainWidth, swapchainHeight);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buffers.depthBuffers[eye]);

			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				LOG_L(L_ERROR, "[VR] Incomplete framebuffer for swapchain image %u (status=0x%x)", (unsigned)idx, (unsigned)status);
				return false;
			}
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	LogGlErrors("ensure-framebuffers");
	LOG_L(L_INFO, "[VR] EnsureFramebuffers completed successfully");
	return true;
}

void OpenXRRenderer::PollEvents()
{
	if (instance == XR_NULL_HANDLE)
		return;

	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(instance, &event) == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const auto& stateChanged = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
			sessionState = stateChanged.state;
			LOG_L(L_INFO, "[VR] Session state changed: %s", SessionStateToString(sessionState));

			if (sessionState == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				const XrResult beginRes = xrBeginSession(session, &beginInfo);
				sessionRunning = (beginRes == XR_SUCCESS);
				if (beginRes != XR_SUCCESS) {
					LogXrError("xrBeginSession", beginRes);
				}
			} else if (sessionState == XR_SESSION_STATE_STOPPING) {
				const XrResult endRes = xrEndSession(session);
				if (endRes != XR_SUCCESS)
					LogXrError("xrEndSession", endRes);
				sessionRunning = false;
			} else if (sessionState == XR_SESSION_STATE_EXITING || sessionState == XR_SESSION_STATE_LOSS_PENDING) {
				sessionRunning = false;
			}
		} break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
			LOG_L(L_WARNING, "[VR] Instance loss pending");
			break;
		}
		case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
			LOG_L(L_WARNING, "[VR] OpenXR events lost");
			break;
		}
		default: break;
		}

		event = {XR_TYPE_EVENT_DATA_BUFFER};
	}
}

bool OpenXRRenderer::AcquireSwapchainImage(uint32_t& index)
{
	XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	const XrResult acquireRes = xrAcquireSwapchainImage(colorSwapchain, &acquireInfo, &index);
	if (acquireRes != XR_SUCCESS) {
		LogXrError("xrAcquireSwapchainImage", acquireRes);
		return false;
	}

	XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	waitInfo.timeout = XR_INFINITE_DURATION;
	const XrResult waitRes = xrWaitSwapchainImage(colorSwapchain, &waitInfo);
	if (waitRes != XR_SUCCESS) {
		LogXrError("xrWaitSwapchainImage", waitRes);
		return false;
	}
	return true;
}

void OpenXRRenderer::ReleaseSwapchainImage()
{
	XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	const XrResult releaseRes = xrReleaseSwapchainImage(colorSwapchain, &releaseInfo);
	if (releaseRes != XR_SUCCESS)
		LogXrError("xrReleaseSwapchainImage", releaseRes);
}

OpenXRRenderer::CameraState OpenXRRenderer::CaptureCameraState() const
{
	CameraState state;
	state.viewMatrix = camera->GetViewMatrix();
	state.projMatrix = camera->GetProjectionMatrix();
	state.clipMatrix = camera->GetClipControlMatrix();
	state.frustumScales = camera->GetFrustumScales();
	state.aspectRatio = camera->GetAspectRatio();
	return state;
}

void OpenXRRenderer::RestoreCameraState(const CameraState& state) const
{
	camera->SetViewMatrix(state.viewMatrix);
	camera->SetProjMatrix(state.projMatrix);
	camera->SetClipCtrlMatrix(state.clipMatrix);
	camera->SetFrustumScales(state.frustumScales);
	camera->SetAspectRatio(state.aspectRatio);
	camera->UpdateFrustum();
	camera->LoadMatrices();
}

OpenXRRenderer::GlobalViewState OpenXRRenderer::CaptureGlobalViewState() const
{
	GlobalViewState state;
	state.viewPosX = globalRendering->viewPosX;
	state.viewPosY = globalRendering->viewPosY;
	state.viewSizeX = globalRendering->viewSizeX;
	state.viewSizeY = globalRendering->viewSizeY;
	state.pixelX = globalRendering->pixelX;
	state.pixelY = globalRendering->pixelY;
	state.aspectRatio = globalRendering->aspectRatio;
	return state;
}

void OpenXRRenderer::RestoreGlobalViewState(const GlobalViewState& state) const
{
	globalRendering->viewPosX = state.viewPosX;
	globalRendering->viewPosY = state.viewPosY;
	globalRendering->viewSizeX = state.viewSizeX;
	globalRendering->viewSizeY = state.viewSizeY;
	globalRendering->pixelX = state.pixelX;
	globalRendering->pixelY = state.pixelY;
	globalRendering->aspectRatio = state.aspectRatio;
}

void OpenXRRenderer::UpdateUniforms() const
{
	UniformConstants::GetInstance().Update();
}

bool OpenXRRenderer::BeginFrame(XrFrameState& frameState)
{
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	const XrResult waitRes = xrWaitFrame(session, &waitInfo, &frameState);
	if (waitRes != XR_SUCCESS) {
		LogXrError("xrWaitFrame", waitRes);
		return false;
	}

	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	const XrResult beginRes = xrBeginFrame(session, &beginInfo);
	if (beginRes != XR_SUCCESS) {
		LogXrError("xrBeginFrame", beginRes);
		return false;
	}
	return true;
}

void OpenXRRenderer::EndFrame(const XrFrameState& frameState, const std::vector<XrCompositionLayerProjectionView>& projectionViews)
{
	const XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION, nullptr, 0, referenceSpace, static_cast<uint32_t>(projectionViews.size()), projectionViews.data()};

	const XrCompositionLayerBaseHeader* layers[] = {
		reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
	};

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = environmentBlendMode;
	endInfo.layerCount = projectionViews.empty() ? 0u : 1u;
	endInfo.layers = projectionViews.empty() ? nullptr : layers;

	const XrResult endRes = xrEndFrame(session, &endInfo);
	if (endRes != XR_SUCCESS)
		LogXrError("xrEndFrame", endRes);
}

CMatrix44f OpenXRRenderer::BuildViewMatrix(const CMatrix44f& baseView, const XrPosef& pose) const
{
	const Quaternion xrQuat = ToQuaternion(pose.orientation);
	const Quaternion alignQuat = []() {
		const float halfAngle = -math::HALFPI * 0.5f;
		return Quaternion{std::sin(halfAngle), 0.0f, 0.0f, std::cos(halfAngle)};
	}();

	const Quaternion correctedQuat = Multiply(Multiply(alignQuat, xrQuat), Conjugate(alignQuat));
	CMatrix44f worldFromView = QuaternionToMatrix(correctedQuat);

	const float3 xrPosition = ToFloat3(pose.position);
	const float3 correctedPosition = RotateVector(alignQuat, xrPosition) * kMetersToWorld;
	worldFromView.SetPos(correctedPosition);

	// worldFromView transforms view->world; invert for world->view
	CMatrix44f hmdViewMatrix = worldFromView.InvertAffine();
	// Compose: first apply base (game) view transform, then HMD (local head) transform.
	// Since both are world->view transforms, we post-multiply baseView by HMD local adjustment.
	// NOTE: If baseView already includes player camera pitch/yaw, we treat HMD orientation as relative.
	return (hmdViewMatrix * baseView);
}

CMatrix44f OpenXRRenderer::BuildProjectionMatrix(const XrFovf& fov) const
{
	const float left = std::tan(fov.angleLeft);
	const float right = std::tan(fov.angleRight);
	const float up = std::tan(fov.angleUp);
	const float down = std::tan(fov.angleDown);

	const float width = right - left;
	const float height = up - down;

	CMatrix44f proj = CMatrix44f::Zero();

	proj[0] = 2.0f / width;
	proj[5] = 2.0f / height;
	proj[8] = (right + left) / width;
	proj[9] = (up + down) / height;
	proj[10] = -(kZFar + kZNear) / (kZFar - kZNear);
	proj[11] = -1.0f;
	proj[14] = -(2.0f * kZFar * kZNear) / (kZFar - kZNear);

	return proj;
}

void OpenXRRenderer::DrawDebugOverlay() const
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glBegin(GL_LINES);
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex3f(-0.2f, 0.0f, -0.5f);
	glVertex3f(0.2f, 0.0f, -0.5f);

	glColor3f(0.0f, 1.0f, 0.0f);
	glVertex3f(0.0f, -0.2f, -0.5f);
	glVertex3f(0.0f, 0.2f, -0.5f);

	glColor3f(0.0f, 0.4f, 1.0f);
	glVertex3f(0.0f, 0.0f, -0.5f);
	glVertex3f(0.0f, 0.0f, 0.1f);
	glEnd();

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

	glEnable(GL_DEPTH_TEST);
	LogGlErrors("debug-overlay");
}

void OpenXRRenderer::LogGlErrors(const char* stage) const
{
	GLenum err = GL_NO_ERROR;
	while ((err = glGetError()) != GL_NO_ERROR) {
		LOG_L(L_WARNING, "[VR] OpenGL error 0x%x at %s", err, stage);
	}
}

void OpenXRRenderer::LogXrError(const char* stage, long long result) const
{
	if (instance == XR_NULL_HANDLE) {
		LOG_L(L_ERROR, "[VR] %s failed with code %lld", stage, result);
		return;
	}

	char buffer[XR_MAX_RESULT_STRING_SIZE] = {};
	xrResultToString(instance, static_cast<XrResult>(result), buffer);
	LOG_L(L_ERROR, "[VR] %s failed: %s (%lld)", stage, buffer, result);
}

bool OpenXRRenderer::RenderFrame(CGame& game)
{
	if (!sessionRunning || colorSwapchain == XR_NULL_HANDLE)
		return false; // no active XR session; fall back to desktop

	LOG_L(L_INFO, "[VR] RenderFrame begin (state=%s running=%d)", SessionStateToString(sessionState), (int)sessionRunning);

	XrFrameState frameState{XR_TYPE_FRAME_STATE};
	if (!BeginFrame(frameState))
		return false;
	LOG_L(L_INFO, "[VR] FrameState predictedDisplayTime=%lld shouldRender=%d", (long long)frameState.predictedDisplayTime, (int)frameState.shouldRender);

	if (!frameState.shouldRender) {
		EndFrame(frameState, {});
		return false;
	}

	XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = frameState.predictedDisplayTime;
	locateInfo.space = referenceSpace;

	XrViewState viewState{XR_TYPE_VIEW_STATE};
	uint32_t viewCountOutput = viewCount;
	const XrResult locateRes = xrLocateViews(session, &locateInfo, &viewState, viewCount, &viewCountOutput, views.data());
	if (locateRes != XR_SUCCESS) {
		LogXrError("xrLocateViews", locateRes);
		EndFrame(frameState, {});
		return false;
	}
	LOG_L(L_INFO, "[VR] xrLocateViews ok (viewCount=%u flags=0x%x orientationValid=%d positionValid=%d)", viewCountOutput, viewState.viewStateFlags, (int)((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0), (int)((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0));

	uint32_t colorIndex = 0;
	if (!AcquireSwapchainImage(colorIndex)) {
		EndFrame(frameState, {});
		return false;
	}
	LOG_L(L_INFO, "[VR] Acquired swapchain image index=%u (texture=%u)", colorIndex, colorImages[colorIndex].image);

	glBindTexture(GL_TEXTURE_2D, colorImages[colorIndex].image);
	GLint texW = 0, texH = 0, texIF = 0;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &texIF);
	LOG_L(L_INFO, "[VR] Swapchain texture level0 width=%d height=%d internalFormat=0x%x", texW, texH, texIF);
	glBindTexture(GL_TEXTURE_2D, 0);

	auto cameraState = CaptureCameraState();
	auto globalState = CaptureGlobalViewState();

	CMatrix44f baseView = camera->GetViewMatrix();

	std::vector<XrCompositionLayerProjectionView> projectionViews(viewCountOutput, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

	for (uint32_t eye = 0; eye < viewCountOutput; ++eye) {
		glBindFramebuffer(GL_FRAMEBUFFER, swapchainBuffers[colorIndex].framebuffers[eye]);
		GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
			LOG_L(L_ERROR, "[VR] Eye %u framebuffer incomplete (status=0x%x)", eye, fbStatus);
		}
		GLenum preErr = glGetError();
		if (preErr != GL_NO_ERROR) LOG_L(L_WARNING, "[VR] Eye %u pre-clear GL error 0x%x", eye, preErr);
		glViewport(0, 0, swapchainWidth, swapchainHeight);
		// Distinct clear color per eye for visibility debugging
		if (eye == 0)
			glClearColor(0.15f, 0.02f, 0.02f, 1.0f); // left eye reddish
		else
			glClearColor(0.02f, 0.15f, 0.02f, 1.0f); // right eye greenish
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GLenum clearErr = glGetError();
		if (clearErr != GL_NO_ERROR) LOG_L(L_ERROR, "[VR] Eye %u glClear error 0x%x", eye, clearErr);
		LOG_L(L_INFO, "[VR] Eye %u cleared (rgba=%s)", eye, eye==0?"red-ish":"green-ish");

		globalRendering->viewPosX = 0;
		globalRendering->viewPosY = 0;
		globalRendering->viewSizeX = swapchainWidth;
		globalRendering->viewSizeY = swapchainHeight;
		globalRendering->aspectRatio = float(swapchainWidth) / float(std::max(1u, swapchainHeight));
		globalRendering->pixelX = 1.0f / std::max(1u, swapchainWidth);
		globalRendering->pixelY = 1.0f / std::max(1u, swapchainHeight);

		const auto& view = views[eye];
		LOG_L(L_INFO, "[VR] Eye %u pose position=(%.3f,%.3f,%.3f) orientation=(%.3f,%.3f,%.3f,%.3f)", eye, view.pose.position.x, view.pose.position.y, view.pose.position.z, view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z, view.pose.orientation.w);
		const CMatrix44f eyeView = BuildViewMatrix(baseView, view.pose);
		const CMatrix44f eyeProj = BuildProjectionMatrix(view.fov);

		camera->SetViewMatrix(eyeView);
		camera->SetProjMatrix(eyeProj);
		camera->SetFrustumScales(float4{0.0f, 0.0f, kZNear, kZFar});
		camera->SetAspectRatio(globalRendering->aspectRatio);
		camera->UpdateFrustum();
		camera->LoadMatrices();
		camera->UpdateLoadViewport(0, 0, swapchainWidth, swapchainHeight);

		UpdateUniforms();

		game.RenderSceneContent();
		// Ensure all issued GL work for this eye is flushed to the swapchain image
		glFlush();
		DrawDebugOverlay();

		projectionViews[eye].pose = view.pose;
		projectionViews[eye].fov = view.fov;
		projectionViews[eye].subImage.swapchain = colorSwapchain;
		projectionViews[eye].subImage.imageRect.offset = {0, 0};
		projectionViews[eye].subImage.imageRect.extent = {static_cast<int32_t>(swapchainWidth), static_cast<int32_t>(swapchainHeight)};
		projectionViews[eye].subImage.imageArrayIndex = eye;

		LogGlErrors("vr-eye");
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	RestoreGlobalViewState(globalState);
	RestoreCameraState(cameraState);

	ReleaseSwapchainImage();
	EndFrame(frameState, projectionViews);
	LOG_L(L_INFO, "[VR] xrEndFrame submitted (layers=%zu blendMode=%d)", projectionViews.size(), (int)environmentBlendMode);

	return true;
}

} // namespace VR
