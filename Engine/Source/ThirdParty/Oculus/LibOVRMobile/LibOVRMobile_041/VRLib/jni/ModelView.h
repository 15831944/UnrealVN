/************************************************************************************

Filename    :   ModelView.h
Content     :   Basic viewing and movement in a scene.
Created     :   December 19, 2013
Authors     :   John Carmack

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#ifndef MODELVIEW_H
#define MODELVIEW_H

#include "ModelFile.h"
#include "App.h"		// VrFrame
#include "DebugLines.h"

namespace OVR
{

//-----------------------------------------------------------------------------------
// ModelInScene
// 
class ModelInScene
{
public:
			ModelInScene() :
				Definition( NULL )
				{}

	void	SetModelFile( const ModelFile * mf );
	void	AnimateJoints( const float timeInSeconds );

	ModelState			State;		// passed to rendering code
	const ModelFile	*	Definition;	// will not be freed by OvrSceneView
};

//-----------------------------------------------------------------------------------
// OvrSceneView
// 
class OvrSceneView
{
public:
	OvrSceneView();

	// The default view will be located at the origin, looking down the -Z axis,
	// with +X to the right and +Y up.
	// Increasing yaw looks to the left (rotation around Y axis).

	// loads the default GL shader programs
	ModelGlPrograms GetDefaultGLPrograms();

	// Blocking load of a scene from the filesystem.
	// This model will be freed when a new world model is set.
	void		LoadWorldModel( const char * sceneFileName, const MaterialParms & materialParms );

	// Set an already loaded scene, which will not be freed when a new
	// world model is set.
	void		SetWorldModel( ModelFile & model );

	// Allow movement inside the scene based on the joypad.
	// Sets the timeWarpParms for smooth joypad turning while dropping frames.
	void		Frame(const VrViewParms viewParms_, const VrFrame vrFrame,
			ovrMatrix4f & timeWarpParmsExternalVelocity );

	// Issues GL calls and returns the MVP for the eye, as needed by AppInterface DrawEyeVIew
	Matrix4f	DrawEyeView( const int eye, const float fovDegrees ) const;

	// Returns the new modelIndex
	int			AddModel( ModelInScene * model );
	void		RemoveModelIndex( int index );

	// Passed on to world model
	SurfaceDef *			FindNamedSurface( const char *name ) const;
	const ModelTexture *	FindNamedTexture( const char *name ) const;
	const ModelTag *		FindNamedTag( const char *name ) const;
	Bounds3f				GetBounds() const;


	// Derived from state after last Frame()
	Vector3f	GetFootPos() const { return FootPos; }

	// WARNING: this does not take into account the head model, it is just footpos + eyeheight
	Vector3f	CenterEyePos() const;
	Vector3f	Forward() const;
	Matrix4f 	CenterViewMatrix() const;
	Matrix4f 	ViewMatrixForEye( const int eye ) const;	// includes InterpupillaryDistance
	Matrix4f 	MvpForEye( const int eye, const float fovDegrees ) const;
	Matrix4f 	ProjectionMatrixForEye( const int eye, const float fovDegrees ) const;

	static Vector3f HeadModelOffset( float EyeRoll, float EyePitch, float EyeYaw, float HeadModelLength, float HeadModelAngle );

	void		UpdateViewMatrix(const VrFrame vrFrame );
	void		UpdateSceneModels( const VrFrame vrFrame );

	// Entries can be NULL.
	// None of these will be directly freed by OvrSceneView.
	Array<ModelInScene *>	Models;

	// This is built up out of Models each frame, and used for
	// rendering both eyes
	Array<ModelState>		RenderModels;

	// The only ModelInScene that OvrSceneView actually owns.
	bool					FreeWorldModelOnChange;
	ModelInScene			WorldModel;
	long long				SceneId;		// for network identification

	GlProgram				ProgVertexColor;
	GlProgram				ProgSingleTexture;
	GlProgram				ProgLightMapped;
	GlProgram				ProgReflectionMapped;
	GlProgram				ProgSkinnedVertexColor;
	GlProgram				ProgSkinnedSingleTexture;
	GlProgram				ProgSkinnedLightMapped;
	GlProgram				ProgSkinnedReflectionMapped;
	bool					LoadedPrograms;

	ModelGlPrograms			GlPrograms;

	// Updated each Frame()
	VrViewParms				ViewParms;

	// 3.0 m/s by default.  Different apps may want different move speeds
    float					MoveSpeed;

	// For small scenes with 16 bit depth buffers, it is useful to
	// keep the ratio as small as possible.
	float					Znear;
	float					Zfar;

	// Position tracking test
	Vector3f				ImuToEyeCenter;

	// Angle offsets in radians
	float					YawOffset;		// added on top of the sensor reading
	float					PitchOffset;	// only applied if the tracking sensor isn't active

	// Applied one frame later to avoid bounce-back from async time warp yaw velocity prediction.
	float					YawVelocity;

	// Allow smooth transition from head model to position tracking experiments
    Vector3f				LastHeadModelOffset;
    Vector3f				LatchedHeadModelOffset;

    // Calculated in Frame()
    Matrix4f 				ViewMatrix;
    float					EyeYaw;         // Rotation around Y, CCW positive when looking at RHS (X,Z) plane.
    float					EyePitch;       // Pitch. If sensor is plugged in, only read from sensor.
    float					EyeRoll;        // Roll, only accessible from Sensor.

    // Modified by joypad movement and collision detection
    Vector3f				FootPos;
};

}	// namespace OVR

#endif // MODELVIEW_H
