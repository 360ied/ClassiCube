#ifndef CC_CAMERA_H
#define CC_CAMERA_H
#include "Vectors.h"

/* Represents a camera, may be first or third person.
   Copyright 2014-2017 ClassicalSharp | Licensed under BSD-3
*/
struct PickedPos;

/* Tilt effect applied to the camera. */
struct Matrix Camera_TiltM;
/* Bobbing offset of camera from player's eye. */
float Camera_BobbingVer, Camera_BobbingHor;

struct Camera {
	/* Whether this camera is third person. (i.e. not allowed when -thirdperson in MOTD) */
	bool IsThirdPerson;
	/* Calculates the current projection matrix of this camera. */
	void (*GetProjection)(struct Matrix* proj);
	/* Calculates the current modelview matrix of this camera. */
	void (*GetView)(struct Matrix* view);

	/* Returns the current orientation of the camera. */
	Vector2 (*GetOrientation)(void);
	/* Returns the current interpolated position of the camera. */
	Vector3 (*GetPosition)(float t);

	void (*UpdateMouse)(void);
	/* Called when user closes all menus, and is interacting with camera again. */
	/* Typically, this is used to move mouse cursor to centre of the window. */
	void (*RegrabMouse)(void);

	/* Calculates selected block in the world, based on camera's current state */
	void (*GetPickedBlock)(struct PickedPos* pos);
	/* Zooms the camera in or out when scrolling mouse wheel. */
	bool (*Zoom)(float amount);
};

/* Camera user is currently using. */
struct Camera* Camera_Active;
/* Initialises the list of cameras. */
void Camera_Init(void);
/* Switches to the next camera in the list. */
void Camera_CycleActive(void);
#endif
