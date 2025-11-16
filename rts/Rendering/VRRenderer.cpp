/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VRRenderer.h"

#ifdef USE_OPENXR

#include "WorldDrawer.h"
#include "Game/Camera.h"
#include "Game/CameraHandler.h"
#include "System/Log/ILog.h"
#include "System/Matrix44f.h"
#include "Rendering/GL/myGL.h"

CVRRenderer::CVRRenderer()
	: initialized(false)
	, frameInProgress(false)
{
	for (int i = 0; i < 2; ++i) {
		eyeFBOs[i].fbo = 0;
		eyeFBOs[i].depthBuffer = 0;
		eyeFBOs[i].width = 0;
		eyeFBOs[i].height = 0;
	}
	
	LOG_L(L_INFO, "[VR] VR Renderer created");
}

CVRRenderer::~CVRRenderer()
{
	Shutdown();
}

bool CVRRenderer::Initialize()
{
	LOG_L(L_INFO, "[VR] Initializing VR renderer...");
	
	// Initialize OpenXR
	if (!openxrManager.Initialize()) {
		LOG_L(L_ERROR, "[VR] Failed to initialize OpenXR - cannot start VR mode");
		return false;
	}
	
	// Create framebuffers
	if (!CreateFramebuffers()) {
		LOG_L(L_ERROR, "[VR] Failed to create framebuffers");
		openxrManager.Shutdown();
		return false;
	}
	
	initialized = true;
	LOG_L(L_INFO, "[VR] VR renderer initialization complete");
	return true;
}

void CVRRenderer::Shutdown()
{
	if (!initialized)
		return;
	
	LOG_L(L_INFO, "[VR] Shutting down VR renderer...");
	
	DestroyFramebuffers();
	openxrManager.Shutdown();
	
	initialized = false;
	LOG_L(L_INFO, "[VR] VR renderer shutdown complete");
}

bool CVRRenderer::CreateFramebuffers()
{
	LOG_L(L_DEBUG, "[VR] Creating framebuffers...");
	
	// Get recommended resolution from OpenXR
	uint32_t width, height;
	openxrManager.GetRecommendedResolution(width, height);
	
	for (int eye = 0; eye < 2; ++eye) {
		eyeFBOs[eye].width = width;
		eyeFBOs[eye].height = height;
		
		// Create depth renderbuffer
		glGenRenderbuffers(1, &eyeFBOs[eye].depthBuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, eyeFBOs[eye].depthBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
		
		if (glGetError() != GL_NO_ERROR) {
			LOG_L(L_ERROR, "[VR] Failed to create depth buffer for eye %d", eye);
			return false;
		}
		
		// Create framebuffer object (will bind color texture later)
		glGenFramebuffers(1, &eyeFBOs[eye].fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, eyeFBOs[eye].fbo);
		
		// Attach depth buffer
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, eyeFBOs[eye].depthBuffer);
		
		if (glGetError() != GL_NO_ERROR) {
			LOG_L(L_ERROR, "[VR] Failed to attach depth buffer for eye %d", eye);
			return false;
		}
		
		LOG_L(L_INFO, "[VR] Eye %d framebuffer created: %ux%u (FBO=%u, depth=%u)",
			eye, width, height, eyeFBOs[eye].fbo, eyeFBOs[eye].depthBuffer);
	}
	
	// Unbind
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	
	return true;
}

void CVRRenderer::DestroyFramebuffers()
{
	for (int eye = 0; eye < 2; ++eye) {
		if (eyeFBOs[eye].fbo != 0) {
			glDeleteFramebuffers(1, &eyeFBOs[eye].fbo);
			eyeFBOs[eye].fbo = 0;
		}
		
		if (eyeFBOs[eye].depthBuffer != 0) {
			glDeleteRenderbuffers(1, &eyeFBOs[eye].depthBuffer);
			eyeFBOs[eye].depthBuffer = 0;
		}
	}
	
	LOG_L(L_DEBUG, "[VR] Framebuffers destroyed");
}

bool CVRRenderer::RenderFrame()
{
	if (!initialized) {
		LOG_L(L_ERROR, "[VR] Cannot render - VR not initialized");
		return false;
	}
	
	// Begin VR frame
	if (!BeginFrame())
		return false;
	
	// Render both eyes
	RenderEye(0); // left eye
	RenderEye(1); // right eye
	
	// Submit to compositor
	EndFrame();
	
	return true;
}

bool CVRRenderer::BeginFrame()
{
	if (!openxrManager.BeginFrame()) {
		LOG_L(L_WARNING, "[VR] OpenXR BeginFrame returned false (runtime not ready)");
		return false;
	}
	
	frameInProgress = true;
	LOG_L(L_DEBUG, "[VR] Frame begun successfully");
	return true;
}

void CVRRenderer::RenderEye(int eyeIndex)
{
	LOG_L(L_DEBUG, "[VR] Rendering eye %d...", eyeIndex);
	
	// Get eye render data from OpenXR
	const auto& eyeData = openxrManager.GetEyeRenderData(eyeIndex);
	
	// Acquire swapchain image from OpenXR
	const uint32_t textureId = openxrManager.AcquireSwapchainImage(eyeIndex);
	
	// Bind our FBO with the OpenXR texture as color attachment
	BindEyeFramebuffer(eyeIndex, textureId);
	
	// Get VR camera for this eye
	const unsigned int vrCamType = (eyeIndex == 0) ? CCamera::CAMTYPE_VR_LEFT : CCamera::CAMTYPE_VR_RIGHT;
	CCamera* vrCamera = CCameraHandler::GetCamera(vrCamType);
	
	// Get player camera (the base camera that player controllers update)
	CCamera* playerCamera = CCameraHandler::GetCamera(CCamera::CAMTYPE_PLAYER);
	
	// Apply VR transform: inherit player camera state + apply HMD/eye matrices
	vrCamera->ApplyVRTransform(playerCamera, eyeData.viewMatrix, eyeData.projectionMatrix);
	
	// Update viewport for this eye's resolution
	vrCamera->UpdateLoadViewport(0, 0, eyeData.swapchainWidth, eyeData.swapchainHeight);
	
	// Switch to VR camera as active
	CCameraHandler::SetActiveCamera(vrCamType);
	
	// Clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	// Render the world using existing rendering path
	// WorldDrawer::Draw() will use the active camera (our VR camera)
	CWorldDrawer::Draw();
	
	// Release swapchain image back to OpenXR
	openxrManager.ReleaseSwapchainImage(eyeIndex);
	
	LOG_L(L_DEBUG, "[VR] Eye %d rendered successfully", eyeIndex);
}

void CVRRenderer::EndFrame()
{
	// Restore player camera as active
	CCameraHandler::SetActiveCamera(CCamera::CAMTYPE_PLAYER);
	
	// Submit frame to compositor
	openxrManager.EndFrame();
	
	frameInProgress = false;
	LOG_L(L_DEBUG, "[VR] Frame ended successfully");
}

void CVRRenderer::BindEyeFramebuffer(int eyeIndex, uint32_t textureId)
{
	glBindFramebuffer(GL_FRAMEBUFFER, eyeFBOs[eyeIndex].fbo);
	
	// Attach OpenXR swapchain texture as color attachment
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
	
	// Verify framebuffer completeness
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		LOG_L(L_ERROR, "[VR] Framebuffer incomplete for eye %d: 0x%X", eyeIndex, status);
	}
	
	LOG_L(L_DEBUG, "[VR] Bound FBO %u with texture %u for eye %d",
		eyeFBOs[eyeIndex].fbo, textureId, eyeIndex);
}

#endif // USE_OPENXR
