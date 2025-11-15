#ifndef VR_SYSTEM_H
#define VR_SYSTEM_H

#include "System/float3.h"
#include "System/type2.h"
#include "System/Matrix44f.h"
#include <vector>

#ifdef USE_VR
// Include platform headers before OpenXR to get required types
#ifdef _WIN32
#include <windows.h>  // Must come before OpenXR for HDC, HGLRC types
#define XR_USE_PLATFORM_WIN32
#elif defined(__linux__)
#define XR_USE_PLATFORM_XLIB
#endif
// Define platform-specific graphics API bindings before including OpenXR
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

class CVRSystem {
public:
	enum Eye {
		LEFT_EYE = 0,
		RIGHT_EYE = 1,
		EYE_COUNT = 2
	};

	struct EyeRenderData {
		CMatrix44f viewMatrix;
		CMatrix44f projectionMatrix;
		float3 position;
		float3 forward;
		float3 up;
		float3 right;
		uint32_t framebuffer;
		uint32_t colorTexture;
		uint32_t depthTexture;
		int32_t width;
		int32_t height;
	};

	CVRSystem();
	~CVRSystem();

	bool Initialize();
	void Shutdown();
	
	// Start/stop VR session (call StartSession after game is loaded)
	bool StartSession();
	void StopSession();
	
	bool IsActive() const { return active; }
	bool IsInitialized() const { return initialized; }
	bool IsSessionRunning() const { return sessionRunning; }
	
	// Call at start of frame to get latest HMD pose
	void WaitGetPoses();
	
	// Setup camera for rendering specific eye
	void SetupEyeCamera(Eye eye);
	
	// Submit rendered eye texture to compositor
	void SubmitEyeTexture(Eye eye);
	
	// Present both eyes to HMD
	void Present();
	
	// Get eye render data for manual camera setup
	const EyeRenderData& GetEyeData(Eye eye) const { return eyeData[eye]; }
	
	// Get recommended render target size for one eye
	void GetRecommendedRenderTargetSize(int32_t& width, int32_t& height) const;
	
	// Get controller input state
	void UpdateControllerInput();
	float2 GetLeftJoystick() const { return leftJoystick; }
	float2 GetRightJoystick() const { return rightJoystick; }
	bool GetGripButton(bool leftHand) const { return leftHand ? leftGrip : rightGrip; }
	bool GetTriggerButton(bool leftHand) const { return leftHand ? leftTrigger : rightTrigger; }

private:
	bool CreateInstance();
	bool CreateSystem();
	bool CreateSession();
	bool CreateSwapchains();
	bool CreateReferenceSpace();
	void CreateFramebuffers();
	void UpdateEyePoses();
	void PollEvents();
	
#ifdef USE_VR
	XrInstance instance;
	XrSystemId systemId;
	XrSession session;
	XrSpace playSpace;
	XrSessionState sessionState;
	
	XrViewConfigurationType viewConfigType;
	std::vector<XrView> views;
	std::vector<XrViewConfigurationView> viewConfigs;
	
	struct SwapchainData {
		XrSwapchain handle;
		int32_t width;
		int32_t height;
		std::vector<XrSwapchainImageOpenGLKHR> images;
	};
	
	SwapchainData swapchains[EYE_COUNT];
	XrFrameState frameState;
#endif
	
	bool initialized;
	bool active;
	bool sessionRunning;
	
	EyeRenderData eyeData[EYE_COUNT];
	
	float3 hmdPosition;
	float3 hmdForward;
	float3 hmdUp;
	float3 hmdRight;
	
	// Controller input state
	float2 leftJoystick;
	float2 rightJoystick;
	bool leftGrip;
	bool rightGrip;
	bool leftTrigger;
	bool rightTrigger;
	
#ifdef USE_VR
	XrActionSet actionSet;
	XrAction joystickLeftAction;
	XrAction joystickRightAction;
	XrAction gripLeftAction;
	XrAction gripRightAction;
	XrAction triggerLeftAction;
	XrAction triggerRightAction;
	XrPath leftHandPath;
	XrPath rightHandPath;
	XrSpace leftHandSpace;
	XrSpace rightHandSpace;
	
	bool CreateActions();
	bool AttachActionSet();
#endif
};

extern CVRSystem* g_VRSystem;

#endif // VR_SYSTEM_H
