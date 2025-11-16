/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/GL/myGL.h"

#include "WorldDrawer.h"
#include "Sim/Units/UnitDefHandler.h"
#include "Sim/Features/FeatureDefHandler.h"
#include "Sim/Weapons/WeaponDefHandler.h"
#include "Rendering/Env/CubeMapHandler.h"
#include "Rendering/Env/GrassDrawer.h"
#include "Rendering/Env/IGroundDecalDrawer.h"
#include "Rendering/Env/ISky.h"
#include "Rendering/Env/SunLighting.h"
#include "Rendering/Env/WaterRendering.h"
#include "Rendering/Env/MapRendering.h"
#include "Rendering/Env/IWater.h"
#include "Rendering/CommandDrawer.h"
#include "Rendering/DebugColVolDrawer.h"
#include "Rendering/DebugVisibilityDrawer.h"
#include "Rendering/LineDrawer.h"
#include "Rendering/LuaObjectDrawer.h"
#include "Rendering/Features/FeatureDrawer.h"
#if defined(USE_VR)
#include "Rendering/VR/VRSystem.h"
#include "Rendering/UniformConstants.h"
#endif
#if defined(USE_VR)
namespace {

// Thread-local storage for the current VR framebuffer
// This is needed because some drawing functions unbind the framebuffer
thread_local GLuint g_currentVRFramebuffer = 0;

void CheckGLError(const char* label) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		LOG_L(L_ERROR, "[VR] OpenGL Error at %s: 0x%x", label, err);
	}
}

void DrawVRTestCube()
{
	// No rotation - keep cube stationary

	const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
#ifdef GL_TEXTURE_2D
	const GLboolean textureEnabled = glIsEnabled(GL_TEXTURE_2D);
#endif

#ifdef GL_TEXTURE_2D
	glDisable(GL_TEXTURE_2D);
#endif
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);  // Disable face culling to see all sides

	glBegin(GL_QUADS);
	// Front Face
	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex3f(-1.0f, -1.0f, 1.0f);
	glVertex3f(1.0f, -1.0f, 1.0f);
	glVertex3f(1.0f, 1.0f, 1.0f);
	glVertex3f(-1.0f, 1.0f, 1.0f);
	// Back Face
	glColor3f(0.0f, 1.0f, 0.0f);
	glVertex3f(-1.0f, -1.0f, -1.0f);
	glVertex3f(-1.0f, 1.0f, -1.0f);
	glVertex3f(1.0f, 1.0f, -1.0f);
	glVertex3f(1.0f, -1.0f, -1.0f);
	// Top Face
	glColor3f(0.0f, 0.0f, 1.0f);
	glVertex3f(-1.0f, 1.0f, -1.0f);
	glVertex3f(-1.0f, 1.0f, 1.0f);
	glVertex3f(1.0f, 1.0f, 1.0f);
	glVertex3f(1.0f, 1.0f, -1.0f);
	// Bottom Face
	glColor3f(1.0f, 1.0f, 0.0f);
	glVertex3f(-1.0f, -1.0f, -1.0f);
	glVertex3f(1.0f, -1.0f, -1.0f);
	glVertex3f(1.0f, -1.0f, 1.0f);
	glVertex3f(-1.0f, -1.0f, 1.0f);
	// Right face
	glColor3f(1.0f, 0.0f, 1.0f);
	glVertex3f(1.0f, -1.0f, -1.0f);
	glVertex3f(1.0f, 1.0f, -1.0f);
	glVertex3f(1.0f, 1.0f, 1.0f);
	glVertex3f(1.0f, -1.0f, 1.0f);
	// Left Face
	glColor3f(0.0f, 1.0f, 1.0f);
	glVertex3f(-1.0f, -1.0f, -1.0f);
	glVertex3f(-1.0f, -1.0f, 1.0f);
	glVertex3f(-1.0f, 1.0f, 1.0f);
	glVertex3f(-1.0f, 1.0f, -1.0f);
	glEnd();

	if (depthEnabled)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);

	if (blendEnabled)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);

#ifdef GL_TEXTURE_2D
	if (textureEnabled)
		glEnable(GL_TEXTURE_2D);
	else
		glDisable(GL_TEXTURE_2D);
#endif
}

}
#endif

#include "Rendering/Env/Particles/ProjectileDrawer.h"
#include <algorithm>
#include "Rendering/Units/UnitDrawer.h"
#include "Rendering/IPathDrawer.h"
#include "Rendering/DepthBufferCopy.h"
#include "Rendering/SmoothHeightMeshDrawer.h"
#include "Rendering/InMapDrawView.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Map/InfoTexture/IInfoTextureHandler.h"
#include "Rendering/Models/IModelParser.h"
#include "Rendering/Models/3DModelVAO.hpp"
#include "Rendering/Models/ModelsLock.h"
#include "Rendering/Shaders/ShaderHandler.h"
#include "Rendering/Textures/ColorMap.h"
#include "Rendering/Textures/3DOTextureHandler.h"
#include "Rendering/Textures/S3OTextureHandler.h"
#include "Map/BaseGroundDrawer.h"
#include "Map/ReadMap.h"
#include "Game/Camera.h"
#include "System/float4.h"
#include "System/Quaternion.h"
#include "Game/SelectedUnitsHandler.h"
#include "Game/Game.h"
#include "Game/GlobalUnsynced.h"
#include "Game/LoadScreen.h"
#include "Game/UI/CommandColors.h"
#include "Game/UI/GuiHandler.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/TimeProfiler.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"
#include "System/Config/ConfigHandler.h"
#include "System/LoadLock.h"

CONFIG(bool, PreloadModels).defaultValue(true).description("The engine will preload all models");

void CWorldDrawer::InitPre() const
{
	LuaObjectDrawer::Init();

	CColorMap::InitStatic();

	// these need to be loaded before featureHandler is created
	// (maps with features have their models loaded at startup)
	S3DModelVAO::Init();
	modelLoader.Init();

	loadscreen->SetLoadMessage("Creating Unit Textures");
	textureHandler3DO.Init();
	textureHandlerS3O.Init();

	loadscreen->SetLoadMessage("Creating Sky");

	ISky::SetSky();
	sunLighting->Init();

	CFeatureDrawer::InitStatic();
}

void CWorldDrawer::InitPost() const
{
	char buf[512] = {0};

	CModelsLock::SetThreadSafety(true);
	const bool preloadMode = configHandler->GetBool("PreloadModels");
	{
		loadscreen->SetLoadMessage("Loading Models");

		if (preloadMode) {
			for (const auto& def : unitDefHandler->GetUnitDefsVec()) {
				def.PreloadModel();
			}

			for (const auto& def : featureDefHandler->GetFeatureDefsVec()) {
				def.PreloadModel();
			}

			for (const auto& def : weaponDefHandler->GetWeaponDefsVec()) {
				def.PreloadModel();
			}
		}
	}
	auto lock = CLoadLock::GetUniqueLock();
	{
		loadscreen->SetLoadMessage("Creating ShadowHandler");
		shadowHandler.Init();
	}
	{
		// SMFGroundDrawer accesses InfoTextureHandler, create it first
		loadscreen->SetLoadMessage("Creating InfoTextureHandler");
		IInfoTextureHandler::Create();
	}
	try {
		loadscreen->SetLoadMessage("Creating GroundDrawer");
		readMap->InitGroundDrawer();
	} catch (const content_error& e) {
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "[WorldDrawer::%s] caught exception \"%s\"", __func__, e.what());
	}

	{
		loadscreen->SetLoadMessage("Creating GrassDrawer");
		grassDrawer = new CGrassDrawer();
	}
	{
		inMapDrawerView = new CInMapDrawView();
		pathDrawer = IPathDrawer::GetInstance();
	}
	{
		DepthBufferCopy::Init();
	}
	{
		IGroundDecalDrawer::Init();
	}
	{
		loadscreen->SetLoadMessage("Creating ProjectileDrawer & UnitDrawer");

		CProjectileDrawer::InitStatic();
		CUnitDrawer::InitStatic();
		// see ::InitPre
		// CFeatureDrawer::InitStatic();
	}

	// rethrow to force exit
	if (buf[0] != 0)
		throw content_error(buf);

	{
		loadscreen->SetLoadMessage("Creating Water");
		IWater::SetWater(-1);
	}
	{
		// ISky::GetSky()->SetupFog();
	}
	lock = {}; //unlock
	{
		loadscreen->SetLoadMessage("Finalizing Models");
		modelLoader.DrainPreloadFutures(0);
		auto& mv = S3DModelVAO::GetInstance();
		if (preloadMode) {
			{
				auto lock = CLoadLock::GetUniqueLock();
				mv.UploadVBOs();
			}
			mv.SetSafeToDeleteVectors();
			modelLoader.LogErrors();
			CModelsLock::SetThreadSafety(false); //all models are already preloaded
		}
	}
}


void CWorldDrawer::Kill()
{
	infoTextureHandler = nullptr;

	IWater::KillWater();
	ISky::KillSky();
	spring::SafeDelete(grassDrawer);
	spring::SafeDelete(pathDrawer);
	shadowHandler.Kill();
	spring::SafeDelete(inMapDrawerView);

	CFeatureDrawer::KillStatic(gu->globalReload);
	CUnitDrawer::KillStatic(gu->globalReload); // depends on unitHandler, cubeMapHandler
	CProjectileDrawer::KillStatic(gu->globalReload);

	S3DModelVAO::Kill();
	modelLoader.Kill();

	textureHandler3DO.Kill();
	textureHandlerS3O.Kill();

	readMap->KillGroundDrawer();
	IGroundDecalDrawer::FreeInstance();
	DepthBufferCopy::Kill();
	LuaObjectDrawer::Kill();
	SmoothHeightMeshDrawer::FreeInstance();

	numUpdates = 0;
}




void CWorldDrawer::Update(bool newSimFrame)
{
	SCOPED_TIMER("Update::WorldDrawer");

	LuaObjectDrawer::Update(numUpdates == 0);
	readMap->UpdateDraw(numUpdates == 0);

	if (globalRendering->drawGround) {
		ZoneScopedN("GroundDrawer::Update");
		(readMap->GetGroundDrawer())->Update();
	}
	// XXX: done in CGame, needs to get updated even when !doDrawWorld
	// (it updates unitdrawpos which is used for maximized minimap too)
	// unitDrawer->Update();
	// lineDrawer.UpdateLineStipple();
	CUnitDrawer::UpdateStatic();
	CFeatureDrawer::UpdateStatic();
	projectileDrawer->UpdateDrawFlags();

	if (newSimFrame) {
		projectileDrawer->UpdateTextures();

		{
			SCOPED_TIMER("Update::WorldDrawer::{Sky,Water}");

			ISky::GetSky()->Update();
			IWater::GetWater()->Update();
		}

		// once every simframe is frequent enough here
		// NB: errors will not be logged until frame 0
		modelLoader.LogErrors();
	}

	numUpdates += 1;
}



void CWorldDrawer::GenerateIBLTextures() const
{

	if (shadowHandler.ShadowsLoaded()) {
		SCOPED_TIMER("Draw::World::CreateShadows");
		SCOPED_GL_DEBUGGROUP("Draw::World::CreateShadows");

		game->SetDrawMode(CGame::gameShadowDraw);
		shadowHandler.CreateShadows();
		game->SetDrawMode(CGame::gameNormalDraw);
	}

	{
		SCOPED_TIMER("Draw::World::UpdateReflTex");
		SCOPED_GL_DEBUGGROUP("Draw::World::UpdateReflTex");
		cubeMapHandler.UpdateReflectionTexture();
	}

	SCOPED_GL_DEBUGGROUP("Draw::World::UpdateMisc");
	bool sunDirUpd = ISky::GetSky()->GetLight()->Update();
	bool sunLightUpd = sunLighting->IsUpdated();
	bool skyUpd = ISky::GetSky()->IsUpdated();
	bool waterUpd = waterRendering->IsUpdated();

	if (sunDirUpd) {
		SCOPED_TIMER("Draw::World::UpdateSpecTex");
		cubeMapHandler.UpdateSpecularTexture();
	}
	if (sunDirUpd || skyUpd) {
		SCOPED_TIMER("Draw::World::UpdateSkyTex");
		ISky::GetSky()->UpdateSkyTexture();
	}
	if (sunDirUpd || sunLightUpd || waterUpd) {
		SCOPED_TIMER("Draw::World::UpdateShadingTex");
		readMap->UpdateShadingTexture();
	}
}

void CWorldDrawer::ResetMVPMatrices() const
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, 1, 0, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}



void CWorldDrawer::DrawEyeScene() const
{
	SCOPED_TIMER("Draw::World");
	SCOPED_GL_DEBUGGROUP("Draw::World");

	const auto& sky = ISky::GetSky();
	glClearColor(sky->fogColor.x, sky->fogColor.y, sky->fogColor.z, 0.0f);
	CheckGLError("glClearColor in DrawEyeScene");
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	CheckGLError("glClear in DrawEyeScene");

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	CheckGLError("GL state setup in DrawEyeScene");

	DrawOpaqueObjects();
	CheckGLError("DrawOpaqueObjects");
	
	// Rebind VR framebuffer if it was unbound
	if (g_currentVRFramebuffer != 0) {
		GLint fb = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if (fb == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, g_currentVRFramebuffer);
			CheckGLError("Rebind VR FB after DrawOpaqueObjects");
		}
	}
	
	DrawAlphaObjects();
	CheckGLError("DrawAlphaObjects");
	
	// Rebind VR framebuffer if it was unbound
	if (g_currentVRFramebuffer != 0) {
		GLint fb = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if (fb == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, g_currentVRFramebuffer);
			CheckGLError("Rebind VR FB after DrawAlphaObjects");
		}
	}
	
	{
		SCOPED_TIMER("Draw::World::DrawWorld");
		SCOPED_GL_DEBUGGROUP("Draw::World::DrawWorld");
		eventHandler.DrawWorld();
		CheckGLError("eventHandler.DrawWorld");
	}

	// Rebind VR framebuffer if it was unbound
	if (g_currentVRFramebuffer != 0) {
		GLint fb = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if (fb == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, g_currentVRFramebuffer);
			CheckGLError("Rebind VR FB after DrawWorld");
		}
	}

	DrawMiscObjects();
	CheckGLError("DrawMiscObjects");
	
	// Rebind VR framebuffer if it was unbound
	if (g_currentVRFramebuffer != 0) {
		GLint fb = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if (fb == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, g_currentVRFramebuffer);
			CheckGLError("Rebind VR FB after DrawMiscObjects");
		}
	}
	
	DrawBelowWaterOverlay();
	CheckGLError("DrawBelowWaterOverlay");
	
	// Rebind VR framebuffer if it was unbound
	if (g_currentVRFramebuffer != 0) {
		GLint fb = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if (fb == 0) {
			glBindFramebuffer(GL_FRAMEBUFFER, g_currentVRFramebuffer);
			CheckGLError("Rebind VR FB after DrawBelowWaterOverlay");
		}
	}

	glDisable(GL_FOG);
	CheckGLError("glDisable GL_FOG");
}


void CWorldDrawer::Draw() const
{
#if defined(USE_VR)
	vr::OpenXRSystem* vrSystem = vr::GetOpenXRSystem();
	const bool vrReady = (vrSystem != nullptr && vrSystem->IsInitialized() && vrSystem->IsSessionRunning());

	if (vrReady && vrSystem->BeginFrame()) {
		const float3 basePos = camera->GetPos();
		const float3 baseRot = camera->GetRot();
		const float baseVFOV = camera->GetVFOV();
		const float baseNear = camera->GetNearPlaneDist();
		const float baseFar = camera->GetFarPlaneDist();
		const int baseViewPosX = globalRendering->viewPosX;
		const int baseViewPosY = globalRendering->viewPosY;
		const int baseViewSizeX = globalRendering->viewSizeX;
		const int baseViewSizeY = globalRendering->viewSizeY;
		const float baseAspect = globalRendering->aspectRatio;
		const float basePixelX = globalRendering->pixelX;
		const float basePixelY = globalRendering->pixelY;

		const CQuaternion baseOrientation = CQuaternion::FromEulerYPR(baseRot);
		bool renderedStereo = vrSystem->RenderEyes(
			[&](const vr::EyeRenderTarget& eye) {
				if (eye.framebuffer == 0 || eye.width <= 0 || eye.height <= 0) {
					LOG_L(L_WARNING, "[VR] Invalid eye target: fb=%u size=%dx%d", eye.framebuffer, eye.width, eye.height);
					return;
				}

				static int logCounter = 0;
				if (logCounter < 2) {
					LOG_L(L_INFO, "[VR] Eye %d: Rendering to fb=%u size=%dx%d",
						eye.eyeIndex, eye.framebuffer, eye.width, eye.height);
					logCounter++;
				}

				glBindFramebuffer(GL_FRAMEBUFFER, eye.framebuffer);
				g_currentVRFramebuffer = eye.framebuffer;  // Store for rebinding if needed
				CheckGLError("glBindFramebuffer");

				// Check framebuffer status
				GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
					LOG_L(L_ERROR, "[VR] Framebuffer not complete: 0x%x", fbStatus);
					return;
				}
				


				// Update globalRendering viewport for this eye
				globalRendering->viewPosX = 0;
				globalRendering->viewPosY = 0;
				globalRendering->viewSizeX = eye.width;
				globalRendering->viewSizeY = eye.height;
				globalRendering->aspectRatio = static_cast<float>(eye.width) / eye.height;
				globalRendering->pixelX = 1.0f / eye.width;
				globalRendering->pixelY = 1.0f / eye.height;

				// Calculate eye position and orientation in world space
				CQuaternion eyeRot = baseOrientation * eye.relativeOrientation;
				eyeRot.Normalize();
				
				float3 eyePos = basePos + baseOrientation.Rotate(eye.eyeOffsetWorld);
				
				// Build view and projection matrices manually for VR
				// The camera's Update() method interferes with VR tracking
				
				// Build projection matrix from VR frustum
				CMatrix44f projMatrix = CMatrix44f::PerspProj(
					eye.frustumLeft, eye.frustumRight,
					eye.frustumBottom, eye.frustumTop,
					eye.nearPlane, eye.farPlane
				);
				
				// Build view matrix from eye position and orientation
				CMatrix44f viewMatrix = eyeRot.Inverse().ToRotMatrix();
				viewMatrix.Translate(-eyePos.x, -eyePos.y, -eyePos.z);
				
				// Set the matrices directly on the camera
				camera->SetPos(eyePos);
				camera->SetRot(eyeRot.ToEulerYPR());
				camera->SetProjMatrix(projMatrix);
				camera->SetViewMatrix(viewMatrix);
				camera->UpdateLoadViewport(0, 0, eye.width, eye.height);
				
				CheckGLError("camera matrix setup");

				// Set viewport
				glViewport(0, 0, eye.width, eye.height);
				CheckGLError("glViewport");

				// Load camera matrices into OpenGL (required by DrawEyeScene)
				camera->LoadMatrices();
				CheckGLError("camera LoadMatrices");

				// Render the actual game scene
				DrawEyeScene();
				CheckGLError("DrawEyeScene");

				// Ensure all rendering is complete before releasing the image
				glFinish();  // Wait for GPU to complete
				CheckGLError("glFinish");
			},
			*camera
		);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		g_currentVRFramebuffer = 0;  // Clear VR framebuffer tracking

		// restore base rendering state
		globalRendering->viewPosX = baseViewPosX;
		globalRendering->viewPosY = baseViewPosY;
		globalRendering->viewSizeX = baseViewSizeX;
		globalRendering->viewSizeY = baseViewSizeY;
		globalRendering->aspectRatio = baseAspect;
		globalRendering->pixelX = basePixelX;
		globalRendering->pixelY = basePixelY;

		camera->SetPos(basePos);
		camera->SetRot(baseRot);
		camera->SetVFOV(baseVFOV);
		camera->SetFrustumScales(float4(0.0f, 0.0f, baseNear, baseFar));
		camera->Update();
		camera->UpdateLoadViewport(baseViewPosX, baseViewPosY, baseViewSizeX, baseViewSizeY);

		vrSystem->EndFrame();

		if (renderedStereo)
			return;

		LOG_L(L_WARNING, "[VR] RenderEyes callback produced no output");
	}
#endif

	camera->Update();
	DrawEyeScene();
}

void CWorldDrawer::DrawOpaqueObjects() const
{
	CBaseGroundDrawer* gd = readMap->GetGroundDrawer();

	if (globalRendering->drawGround) {
		{
			SCOPED_TIMER("Draw::World::Terrain");
			SCOPED_GL_DEBUGGROUP("Draw::World::Terrain");
			gd->Draw(DrawPass::Normal);
			depthBufferCopy->MakeDepthBufferCopy();
		}
		{
			eventHandler.DrawPreDecals();
			SCOPED_TIMER("Draw::World::Decals");
			SCOPED_GL_DEBUGGROUP("Draw::World::Decals");
			groundDecals->Draw();
			projectileDrawer->DrawGroundFlashes();
		}
		{
			SCOPED_TIMER("Draw::World::Foliage");
			SCOPED_GL_DEBUGGROUP("Draw::World::Foliage");
			grassDrawer->Draw();
		}
		smoothHeightMeshDrawer->Draw(1.0f);
	}

	// not an opaque rendering, but makes sense to run after the terrain was rendered
	{
		const auto& sky = ISky::GetSky();
		sky->Draw();
	}

	selectedUnitsHandler.Draw();
	eventHandler.DrawWorldPreUnit();

	{
		SCOPED_TIMER("Draw::World::Models::Opaque");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Opaque");
		unitDrawer->Draw(false);
		featureDrawer->Draw(false);
	}
	{
		SCOPED_TIMER("Draw::World::Models::Projectiles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Projectiles");
		projectileDrawer->DrawOpaque(false);
	}
	{
		SCOPED_TIMER("Draw::OpaqueObjects::Debug");
		SCOPED_GL_DEBUGGROUP("Draw::OpaqueObjects::Debug");
		DebugColVolDrawer::Draw();
		DebugVisibilityDrawer::DrawWorld();
		pathDrawer->DrawAll();
	}
}

void CWorldDrawer::DrawAlphaObjects() const
{
	// transparent objects
	glEnable(GL_BLEND);
	CheckGLError("glEnable(GL_BLEND)");
	
	glDepthFunc(GL_LEQUAL);
	CheckGLError("glDepthFunc(GL_LEQUAL)");

	static const double belowPlaneEq[4] = {0.0f, -1.0f, 0.0f, 0.0f};
	static const double abovePlaneEq[4] = {0.0f,  1.0f, 0.0f, 0.0f};

	const bool hasWaterRendering = globalRendering->drawWater && readMap->HasVisibleWater();

	{
		SCOPED_TIMER("Draw::World::Models::Alpha");
		SCOPED_GL_DEBUGGROUP("Draw::World::Models::Alpha");
		// clip in model-space
		if (hasWaterRendering) {
			// Ensure we're in modelview mode for glClipPlane
			glMatrixMode(GL_MODELVIEW);
			CheckGLError("glMatrixMode(GL_MODELVIEW) before clip");
			
			glPushMatrix();
			CheckGLError("glPushMatrix before clip");
			
			glLoadIdentity();
			CheckGLError("glLoadIdentity before clip");
			
			glClipPlane(GL_CLIP_PLANE3, belowPlaneEq);
			CheckGLError("glClipPlane");
			
			glPopMatrix();
			CheckGLError("glPopMatrix after clip");
			
			glEnable(GL_CLIP_PLANE3);
			CheckGLError("glEnable(GL_CLIP_PLANE3)");
		}

		// draw alpha-objects below water surface (farthest)
		unitDrawer->DrawAlphaPass(false);
		CheckGLError("unitDrawer->DrawAlphaPass");
		
		featureDrawer->DrawAlphaPass(false);
		CheckGLError("featureDrawer->DrawAlphaPass");
	}
	{
		SCOPED_TIMER("Draw::World::Particles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Particles");
		projectileDrawer->DrawAlpha(!hasWaterRendering, true, false, false);

		if (hasWaterRendering)
			glDisable(GL_CLIP_PLANE3);
	}

	if (!hasWaterRendering)
		return;

	// draw water (in-between)
	// TODO: Water rendering has issues with VR asymmetric frustum - temporarily disabled
	// The water renderer likely needs updates to handle asymmetric projection matrices
	#if 0
	{
		SCOPED_TIMER("Draw::World::Water");
		SCOPED_GL_DEBUGGROUP("Draw::World::Water");

		const auto& water = IWater::GetWater();
		{
			ZoneScopedN("Draw::World::Water::UpdateWater");
			water->UpdateWater(game);
			CheckGLError("water->UpdateWater");
		}
		water->Draw();
		CheckGLError("water->Draw");
		
		eventHandler.DrawWaterPost();
		CheckGLError("eventHandler.DrawWaterPost");
	}
	
	CheckGLError("After water rendering block");
	#endif

	// TODO: Second alpha pass disabled since water rendering is disabled in VR
	#if 0
	{
		SCOPED_TIMER("Draw::World::Models::Alpha");
		SCOPED_GL_DEBUGGROUP("Draw::World::Alpha");
		
		// Ensure we're in modelview mode for glClipPlane
		glMatrixMode(GL_MODELVIEW);
		CheckGLError("glMatrixMode(GL_MODELVIEW) before clip 2");
		
		glPushMatrix();
		CheckGLError("glPushMatrix before clip 2");
		
		glLoadIdentity();
		CheckGLError("glLoadIdentity before clip 2");
		
		glClipPlane(GL_CLIP_PLANE3, abovePlaneEq);
		CheckGLError("glClipPlane 2");
		
		glPopMatrix();
		CheckGLError("glPopMatrix after clip 2");
		
		glEnable(GL_CLIP_PLANE3);
		CheckGLError("glEnable(GL_CLIP_PLANE3) 2");

		// draw alpha-objects above water surface (closest)
		unitDrawer->DrawAlphaPass(false);
		CheckGLError("unitDrawer->DrawAlphaPass 2");
		
		featureDrawer->DrawAlphaPass(false);
		CheckGLError("featureDrawer->DrawAlphaPass 2");
	}
	{
		SCOPED_TIMER("Draw::World::Particles");
		SCOPED_GL_DEBUGGROUP("Draw::World::Particles");
		projectileDrawer->DrawAlpha(true, false, false, false);

		glDisable(GL_CLIP_PLANE3);
	}
	#endif
}

void CWorldDrawer::DrawMiscObjects() const
{

	{
		// note: duplicated in CMiniMap::DrawWorldStuff()
		commandDrawer->DrawLuaQueuedUnitSetCommands();

		if (cmdColors.AlwaysDrawQueue() || guihandler->GetQueueKeystate()) {
			selectedUnitsHandler.DrawCommands();
		}
	}

	// either draw from here, or make {Dyn,Bump}Water use blending
	// pro: icons are drawn only once per frame, not every pass
	// con: looks somewhat worse for underwater / obscured icons
	if (!CUnitDrawer::UseScreenIcons())
		unitDrawer->DrawUnitIcons();

	lineDrawer.DrawAll();
	cursorIcons.Draw();

	mouse->DrawSelectionBox();
	guihandler->DrawMapStuff(false);

	if (globalRendering->drawMapMarks && !game->hideInterface) {
		inMapDrawerView->Draw();
	}
}



void CWorldDrawer::DrawBelowWaterOverlay() const
{

	if (!globalRendering->drawWater)
		return;
	if (mapRendering->voidWater)
		return;
	if (camera->GetPos().y >= 0.0f)
		return;

	{
		glEnableClientState(GL_VERTEX_ARRAY);

		const float3& cpos = camera->GetPos();
		const float vr = camera->GetFarPlaneDist() * 0.5f;

		glDepthMask(GL_FALSE);
		glDisable(GL_TEXTURE_2D);
		glColor4f(0.0f, 0.5f, 0.3f, 0.50f);

		{
			const float3 verts[] = {
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z - vr)
			};

			glVertexPointer(3, GL_FLOAT, 0, verts);
			glDrawArrays(GL_QUADS, 0, 4);
		}

		{
			const float3 verts[] = {
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr,  -vr, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z + vr),
				float3(cpos.x - vr,  -vr, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z + vr),
				float3(cpos.x + vr,  -vr, cpos.z + vr),
				float3(cpos.x + vr, 0.0f, cpos.z - vr),
				float3(cpos.x + vr,  -vr, cpos.z - vr),
				float3(cpos.x - vr, 0.0f, cpos.z - vr),
				float3(cpos.x - vr,  -vr, cpos.z - vr),
			};

			glVertexPointer(3, GL_FLOAT, 0, verts);
			glDrawArrays(GL_QUAD_STRIP, 0, 10);
		}

		glDepthMask(GL_TRUE);
		glDisableClientState(GL_VERTEX_ARRAY);
	}

	{
		// draw water-coloration quad in raw screenspace
		ResetMVPMatrices();

		glEnableClientState(GL_VERTEX_ARRAY);
		glDisable(GL_TEXTURE_2D);
		glColor4f(0.0f, 0.2f, 0.8f, 0.333f);

		const float3 verts[] = {
			float3(0.0f, 0.0f, -1.0f),
			float3(1.0f, 0.0f, -1.0f),
			float3(1.0f, 1.0f, -1.0f),
			float3(0.0f, 1.0f, -1.0f),
		};

		glVertexPointer(3, GL_FLOAT, 0, verts);
		glDrawArrays(GL_QUADS, 0, 4);
		glDisableClientState(GL_VERTEX_ARRAY);
	}
}
