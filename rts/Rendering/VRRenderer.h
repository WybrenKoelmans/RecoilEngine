/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#ifdef USE_OPENXR

#include "System/Platform/OpenXRManager.h"
#include <cstdint>

class CCamera;
class CWorldDrawer;

/**
 * @brief VR stereo rendering coordinator
 * 
 * Manages the complete VR rendering pipeline:
 * - Owns OpenXR session through OpenXRManager
 * - Creates per-eye framebuffers with depth renderbuffers
 * - Coordinates frame timing (BeginFrame/EndFrame)
 * - Renders each eye by switching active camera and calling WorldDrawer
 * - Submits final images to OpenXR compositor
 */
class CVRRenderer {
public:
	CVRRenderer(CWorldDrawer* wd);
	~CVRRenderer();

	/**
	 * Initialize VR system and create rendering resources.
	 * @return true if successful, false on error (engine should exit)
	 */
	bool Initialize();
	
	/**
	 * Shutdown VR system and release resources.
	 */
	void Shutdown();
	
	/**
	 * Check if VR is initialized and ready to render.
	 */
	bool IsInitialized() const { return initialized; }
	
	/**
	 * Complete VR frame rendering (both eyes).
	 * This replaces the normal Game::Draw() path when VR is enabled.
	 * @return true if frame was rendered successfully
	 */
	bool RenderFrame();

private:
	/**
	 * Begin VR frame - get HMD pose and eye transforms.
	 * @return true if frame should be rendered
	 */
	bool BeginFrame();
	
	/**
	 * Render single eye view.
	 * @param eyeIndex 0 for left eye, 1 for right eye
	 */
	void RenderEye(int eyeIndex);
	
	/**
	 * Submit rendered images to compositor and end frame.
	 */
	void EndFrame();
	
	/**
	 * Create OpenGL framebuffer objects for VR rendering.
	 * @return true if successful
	 */
	bool CreateFramebuffers();
	
	/**
	 * Destroy OpenGL framebuffer objects.
	 */
	void DestroyFramebuffers();
	
	/**
	 * Bind framebuffer for rendering specific eye.
	 * @param eyeIndex 0 for left, 1 for right
	 * @param textureId OpenGL texture ID from OpenXR swapchain
	 */
	void BindEyeFramebuffer(int eyeIndex, uint32_t textureId);

private:
	COpenXRManager openxrManager;
	CWorldDrawer* worldDrawer;
	
	// Per-eye framebuffer objects
	struct EyeFramebuffer {
		uint32_t fbo;           // OpenGL framebuffer object
		uint32_t depthBuffer;   // OpenGL depth renderbuffer
		uint32_t width;
		uint32_t height;
	};
	
	EyeFramebuffer eyeFBOs[2];
	
	bool initialized;
	bool frameInProgress;
};

#endif // USE_OPENXR
