/* iigo openvr battery monitor, creates an overlay on the right hand with current time and the lowest battery of an active controller */


/* System headers for any extra stuff we need. */
#include <stdbool.h>

/* Include CNFG (rawdraw) for generating a window and/or OpenGL context. */
#define CNFG_IMPLEMENTATION
#define CNFGOGL
#include "rawdraw_sf.h"

/* Include OpenVR header so we can interact with VR stuff. */
#undef EXTERN_C
#include "openvr_capi.h"

/* Stuff to get system time */
#include <stdio.h>
#include <time.h>
#include <string.h>

/* config file */
#include "config.h"

/* Mini-Osc Stuff */

#define MINIOSC_IMPLEMENTATION
#include "miniosc.h"

float heat = 0;
double headpat = 0;
int MuteSelf = -1;
time_t UnixTime;
time_t OldTimeUp;
time_t OldTimeDown;
clock_t Clocks;
clock_t OldClocksText;

/* Was the overlay assocated or not? */
int overlayAssociated;

/* all of the osc input to the program is handled using global varibles and modified by this function */
static void 
OSCInputManager( const char * address, const char * type, void ** parameters )
{
	if ( strcmp(address, "/avatar/parameters/HeadPat") == 0)
	{
		headpat = ((double)*((float*)parameters[0])); 
		if (headpat > 0.5) 
		{
			UnixTime = time(NULL);
			if (UnixTime > (OldTimeUp))
			{	
				if (heat < 2.0)
				{
					heat = heat + 0.1;
				}
				OldTimeUp = UnixTime;
			}
		}
	}
	/* kinda hacky but just resets the heat level when the facestate parameter is toggled */
	if ( strcmp(address, "/avatar/parameters/FaceState") == 0)
	{
		heat = 0;
	}

	/* Sets the MuteSelf int parameter -1 is uninitialized */
	if ( strcmp(address, "/avatar/parameters/MuteSelf") == 0)
	{
		if ( strcmp(type, ",F") == 0)
		{
			MuteSelf = 0;
		}
		if ( strcmp(type, ",T") == 0)
		{
			MuteSelf = 1;
		}
		overlayAssociated = 0;
	}
}


/* OpenVR Doesn't define these for some reason (I don't remember why) so we define the functions here. They are copy-pasted from the bottom of openvr_capi.h */
intptr_t VR_InitInternal( EVRInitError *peError, EVRApplicationType eType );
void VR_ShutdownInternal();
bool VR_IsHmdPresent();
intptr_t VR_GetGenericInterface( const char *pchInterfaceVersion, EVRInitError *peError );
bool VR_IsRuntimeInstalled();
const char * VR_GetVRInitErrorAsSymbol( EVRInitError error );
const char * VR_GetVRInitErrorAsEnglishDescription( EVRInitError error );

/* These are functions that rawdraw calls back into. */
void HandleKey( int keycode, int bDown ) { }
void HandleButton( int x, int y, int button, int bDown ) { }
void HandleMotion( int x, int y, int mask ) { }
void HandleDestroy() { }

/* This function was copy-pasted from cnovr. */
static void * 
CNOVRGetOpenVRFunctionTable( const char * interfacename )
{
	EVRInitError e;
	char fnTableName[128];
	int result1 = snprintf( fnTableName, 128, "FnTable:%s", interfacename );
	void * ret = (void *)VR_GetGenericInterface( fnTableName, &e );
	printf( "Getting System FnTable: %s = %p (%d)\n", fnTableName, ret, e );
	if( !ret )
	{
		exit( 1 );
	}
	return ret;
}

/* These are interfaces into OpenVR, they are basically function call tables. */
struct VR_IVRSystem_FnTable * oSystem;
struct VR_IVROverlay_FnTable * oOverlay;
struct VR_IVRApplications_FnTable * oApplications;
//struct VR_IVRScreenshots_FnTable * oScreenshots;
struct VR_IVRInput_FnTable * oInput;

/* The OpenVR Overlay handle. */
VROverlayHandle_t overlayID;

/* Returns the input if between 0 and 1, and if below 0 it returns 0, and if above 1 it returns. */
static float 
saturate(float d) 
{
  const float t = d < 0 ? 0 : d;
  return t > 1 ? 1 : t;
}

/* https://en.wikipedia.org/wiki/Smoothstep */
static float 
SmoothStep(float a, float b, float x)
{
	float t = saturate((x - a)/(b - a));
	return t*t*(3.0 - (2.0*t));
}

static HmdMatrix34_t 
EulerToHmdMatrix34_t(double XAngle, double YAngle, double ZAngle, double Trans1, double Trans2, double Trans3)
{
	HmdMatrix34_t transform = { 0 };
	float X = -XAngle*TAU/360;
	float Y = -YAngle*TAU/360;
	float Z = -ZAngle*TAU/360;
	float cx = cosf(X);
	float sx = sinf(X);
	float cy = cosf(Y);
	float sy = sinf(Y);
	float cz = cosf(Z);
	float sz = sinf(Z);

	transform.m[0][0] = cy*cz;
	transform.m[1][0] = (sx*sy*cz)-(cx*sz);
	transform.m[2][0] = (cx*sy*cz)+(sx*sz);

	transform.m[0][1] = -(cy*sz);
	transform.m[1][1] = -((sx*sy*sz)+(cx*cz));
	transform.m[2][1] = -((cx*sy*sz)-(sx*cz));

	transform.m[0][2] = -sy;
	transform.m[1][2] = sx*cy;
	transform.m[2][2] = cx*cy;

	transform.m[0][3] = 0+Trans1;
	transform.m[1][3] = 0+Trans2;
	transform.m[2][3] = 0+Trans3;
	return transform;
}


int main()
{
	/* OSC STUFF */
	
	/* 9000 is the input port, 9001 is the output port. */
	miniosc * oscin  = minioscInit( OSC_INPUTPORT_MAIN, 0, OSC_LOCALHOST, 0 );
	miniosc * oscout = minioscInit( 0, OSC_OUTPUTPORT_MAIN, OSC_LOCALHOST, 0 );
	miniosc * oscbut = minioscInit( 0, OSC_OUTPUTPORT_BUTT, OSC_LOCALHOST, 0 );
	
	/*
	 * Create the window, needed for making an OpenGL context, but also
	 * gives us a framebuffer we can draw into.  Minus signs in front of 
	 * width/height hint to rawdrawthat we want a hidden window.
	 */
	CNFGSetup( "OpenVR Battery Monitor", -WIDTH, -HEIGHT );

	/*
	 * We put this in a codeblock because it's logically together.
	 * no reason to keep the token around.
	 */
	{
		EVRInitError ierr;
		uint32_t token = VR_InitInternal( &ierr, EVRApplicationType_VRApplication_Overlay );
		if( !token )
		{
			printf( "Error!!!! Could not initialize OpenVR\n" );
			return -5;
		}

		/*
		 * Get the system and overlay interfaces.  We pass in the version of these
		 * interfaces that we wish to use, in case the runtime is newer, we can still
		 * get the interfaces we expect.
		 */
		oSystem = CNOVRGetOpenVRFunctionTable( IVRSystem_Version );
		oOverlay = CNOVRGetOpenVRFunctionTable( IVROverlay_Version );
		oApplications = CNOVRGetOpenVRFunctionTable( IVRApplications_Version );
		//oScreenshots = CNOVRGetOpenVRFunctionTable( IVRScreenshots_Version );
		oInput = CNOVRGetOpenVRFunctionTable( IVRInput_Version );
	}

	/*
	if (!oApplications->IsApplicationInstalled("iigo.iigoOverlay"))
    {
		EVRApplicationError app_error;
		app_error = oApplications->AddApplicationManifest("C:\\Users\\maru\\Documents\\C Programs\\obm\\manifest.vrmanifest", false);

		if (app_error == EVRApplicationError_VRApplicationError_None)
		{
			oApplications->SetApplicationAutoLaunch("iigo.iigoOverlay", true);
		}
    }
	*/

	oInput->SetActionManifestPath("C:\\Users\\maru\\Documents\\C Programs\\obm\\Bindings\\actions.json");

	VRActionHandle_t vibrationHandle = 0;

	oInput->GetActionHandle("/actions/obm/out/haptic", &vibrationHandle);

	VRActionHandle_t showDateHandle = 0;

	oInput->GetActionHandle("/actions/obm/in/ShowDate", &showDateHandle);

	VRInputValueHandle_t valueHandle = 0;

	oInput->GetInputSourceHandle("/devices/valve/index_controllerLHR-D46363B0", &valueHandle);

	VRActionSetHandle_t handleLegacy = 0;
	oInput->GetActionSetHandle("/actions/obm", &handleLegacy);

	{
		/* Generate the overlay. */
		oOverlay->CreateOverlay( "batterymonitoroverlay-overlay", "Battery Monitor Overlay", &overlayID );
		oOverlay->SetOverlayWidthInMeters( overlayID, INGAMEWIDTH );
		oOverlay->SetOverlayColor( overlayID, 1., 1., 1. );

		/* Control texture bounds to control the way the texture is mapped to the overlay. */
		VRTextureBounds_t bounds;
		bounds.uMin = 1;
		bounds.uMax = 0;
		bounds.vMin = 0;
		bounds.vMax = 1;
		oOverlay->SetOverlayTextureBounds( overlayID, &bounds );
	}

	/* Actually show the overlay. */
	oOverlay->ShowOverlay( overlayID );

	GLuint overlaytexture;
	{
		/* Initialize the texture with junk data. */
		uint8_t * myjunkdata = malloc( 128 * 128 * 4 );
		int x, y;
		for( y = 0; y < 128; y++ )
		for( x = 0; x < 128; x++ )
		{
			myjunkdata[ ( x + y * 128 ) * 4 + 0 ] = x * 2;
			myjunkdata[ ( x + y * 128 ) * 4 + 1 ] = y * 2;
			myjunkdata[ ( x + y * 128 ) * 4 + 2 ] = 0;
			myjunkdata[ ( x + y * 128 ) * 4 + 3 ] = 255;
		}
		
		
		/* Generate the texture. */
		glGenTextures( 1, &overlaytexture );
		glBindTexture( GL_TEXTURE_2D, overlaytexture );

		/* It is required to setup the min and mag filter of the texture. */
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		
		/*
		 * Load the texture with our dummy data.  Optionally we could pass 0 in where we are 
		 * passing in myjunkdata. That would allocate the RAM on the GPU but not do anything with it.
		 */
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, myjunkdata );
	}

	/* 
	* Transform for the right hand pos that puts the text somewhere reasonable,
	* the euler angles, and transpose values are set using 
	* the config.h include file.
	*/
	HmdMatrix34_t rightHandTransform = EulerToHmdMatrix34_t(XANGLE, YANGLE, ZANGLE, TRANS1, TRANS2, TRANS3);

	/* 
	* Transform for the HMD pos that puts the text somewhere reasonable,
	* the euler angles, and transpose values are set using
	* the config.h include file
	*/
	HmdMatrix34_t HMDTransform = EulerToHmdMatrix34_t(XANGLE_HMD, YANGLE_HMD, ZANGLE_HMD, TRANS1_HMD, TRANS2_HMD, TRANS3_HMD);

	int framenumber = 0;
	int ssframe = 0;
	int lastlook = 0;
	float oldheadpat = 0;
	float oldheat = 0;

	while( true )
	{
		VRActiveActionSet_t actionSet[1];
		actionSet[0].ulActionSet = handleLegacy;
		actionSet[0].ulRestrictedToDevice = 0;

		oInput->UpdateActionState( actionSet, sizeof(VRActiveActionSet_t), 1);


		CNFGBGColor = 0x00000022; /* Black Transparent Background */
		CNFGClearFrame();
		
		/* Process any window events and call callbacks. */
		CNFGHandleInput();

		/* Setup draw color. */
		CNFGColor( 0xffffffff ); /* white color */
		
		/* Setup where "CNFGDrawText" will draw. */
		CNFGPenX = 1;
		CNFGPenY = 1;

		/* Scratch buffer for us to write text into. */
		char str[256];
        time_t rawtime;
        struct tm * timeinfo;
        char timebuffer[80];

        time ( &rawtime );
        timeinfo = localtime ( &rawtime );

        strftime (timebuffer,80,"%H:%M:%S",timeinfo);

		/* Iterate over the list of all devices. */
		int i;
		int devices = 0;
        float lowestbattery = 1;
		for( i = 0; i < k_unMaxTrackedDeviceCount; i++ )
		{
			/* See if this device has a battery charge. */
			ETrackedDeviceProperty prop;
			ETrackedPropertyError err;
            int deviceclass = oSystem->GetTrackedDeviceClass(i);
            if( deviceclass == ETrackedDeviceClass_TrackedDeviceClass_Controller) 
            {    
			    float battery = oSystem->GetFloatTrackedDeviceProperty( i, ETrackedDeviceProperty_Prop_DeviceBatteryPercentage_Float, &err );
			
			    /* No error? Proceed. */
			    if( err == 0 )
			    {  
					devices++;

                    if (battery < lowestbattery)
                    {
                        lowestbattery = battery;
                    }
			    }
            }			
		}

		
		/* If the overlay is unassociated, associate it with the right controller. */
		if( !overlayAssociated && MuteSelf < 1)
		{
			TrackedDeviceIndex_t index;
			index = oSystem->GetTrackedDeviceIndexForControllerRole( ETrackedControllerRole_TrackedControllerRole_RightHand );
			if( index == k_unTrackedDeviceIndexInvalid || index == k_unTrackedDeviceIndex_Hmd )
			{
				printf( "Couldn't find your controller to attach our overlay to (%d)\n", index );
			}
			else
			{
				/* We have a ETrackedControllerRole_TrackedControllerRole_RightHand. Associate it. */
				EVROverlayError err;

				/* Apply the transform and attach the overlay to that tracked device object. */
				err = oOverlay->SetOverlayTransformTrackedDeviceRelative( overlayID, index, &rightHandTransform );

				/* Notify the terminal that this was associated. */
				printf( "Successfully associated your battery status window to the tracked device (%d %d %08x).\n",
					 err, index, overlayID );

				overlayAssociated = true;
			}
		}

		if( !overlayAssociated && MuteSelf == 1)
		{
			{
				/* We have a ETrackedControllerRole_TrackedDeviceClass_HMD. Associate it. */
				EVROverlayError err;

				/* Apply the transform and attach the overlay to that tracked device object. */
				err = oOverlay->SetOverlayTransformTrackedDeviceRelative( overlayID, k_unTrackedDeviceIndex_Hmd, &HMDTransform );

				/* Notify the terminal that this was associated. */
				printf( "Successfully associated your battery status window to the tracked device (%d %d %08x).\n",
					 err, k_unTrackedDeviceIndex_Hmd, overlayID );

				overlayAssociated = true;
			}
		}

		

		TrackedDevicePose_t hmdpos;

		VROverlayIntersectionParams_t intersectioninput;

		VROverlayIntersectionResults_t intersectionoutput;

		bool viewrayIntersecting;

		oSystem->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding, k_unTrackedDeviceIndex_Hmd, &hmdpos, 1);

		intersectioninput.vSource.v[0] = hmdpos.mDeviceToAbsoluteTracking.m[0][3];
		intersectioninput.vSource.v[1] = hmdpos.mDeviceToAbsoluteTracking.m[1][3];
		intersectioninput.vSource.v[2] = hmdpos.mDeviceToAbsoluteTracking.m[2][3];

		intersectioninput.vDirection.v[0] = -hmdpos.mDeviceToAbsoluteTracking.m[0][2];
		intersectioninput.vDirection.v[1] = -hmdpos.mDeviceToAbsoluteTracking.m[1][2];
		intersectioninput.vDirection.v[2] = -hmdpos.mDeviceToAbsoluteTracking.m[2][2];

		intersectioninput.eOrigin = ETrackingUniverseOrigin_TrackingUniverseStanding;


		viewrayIntersecting = oOverlay->ComputeOverlayIntersection(overlayID, &intersectioninput , &intersectionoutput);

		if (viewrayIntersecting == true)
		{
			lastlook = 60;
		}


		/* sets the overlay to be completly transparent by default. */
		float overlayalpha = 0;

		if ( lastlook >= 1)
		{
			overlayalpha = SmoothStep(.40,.35,intersectionoutput.fDistance);
		}

		if (MuteSelf == 1)
		{
			overlayalpha = 1.00;
		}

		InputDigitalActionData_t showDateActionData;

		EVRInputError showDateERR;

		showDateERR = oInput->GetDigitalActionData(showDateHandle, &showDateActionData, sizeof(InputDigitalActionData_t), k_ulInvalidInputValueHandle);

		if(showDateERR != EVRInputError_VRInputError_None)
		{
			printf( "ShowDate (%d).\n", showDateERR );
		}

		if(showDateActionData.bState)
		{
			strftime(timebuffer,80,"%y-%m-%d %a",timeinfo);
			if(lastlook >= 0 && intersectionoutput.fDistance <= .35 && showDateActionData.bChanged)
			{
				oInput->TriggerHapticVibrationAction(vibrationHandle, 0.0f, 0.13044f, 293.66f, 1.0f, valueHandle);
			}
		}

		sprintf( str, "%s%4.0f%%", timebuffer , lowestbattery * 100.);


		if (lowestbattery < .20) 
            {
            CNFGColor( 0xff2222ff ); /* Make Text red when below 20% */
            }
        CNFGDrawText( str, 2);


		oOverlay->SetOverlayAlpha( overlayID, overlayalpha);

		/* Finish rendering any pending draw operations. */
		CNFGFlushRender();

		/* Bind the texture we will be sending to OpenVR. */
		glBindTexture( GL_TEXTURE_2D, overlaytexture );
		
		/* Copy the current framebuffer into that texture. */
		glCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, WIDTH, HEIGHT, 0 );

		/* Setup a Texture_t object to send in the texture. */
		struct Texture_t tex;
		tex.eColorSpace = EColorSpace_ColorSpace_Auto;
		tex.eType = ETextureType_TextureType_OpenGL;
		tex.handle = (void*)(intptr_t)overlaytexture;

		/* Send texture into OpenVR as the overlay. */
		oOverlay->SetOverlayTexture( overlayID, &tex );

		/* We have to process through texture events. */
		struct VREvent_t nEvent;
		if( overlayAssociated )
		{
			oOverlay->PollNextOverlayEvent( overlayID, &nEvent, 0xffffff );
		}

		/* Display the image and wait for time to display next frame. */
		CNFGSwapBuffers();
		

// =============================================================================

		/*
		 * OSC STUFF
		 * Poll, waiting for up to 15 ms for a message.
		 */
		int r = minioscPoll( oscin, 15, OSCInputManager );

		UnixTime = time(NULL);
		if (UnixTime > (OldTimeDown + 10))
		{
			if (heat >= 0.09)
			{
				heat = heat - 0.1;
			}
			OldTimeDown = UnixTime;
		}

		Clocks = clock();
		if (Clocks > (OldClocksText + (1.5 * CLOCKS_PER_SEC)))
		{
			if (heat >= 0.09)
			{
				char chatboxOutputString[100] = "[#---------] 10\%";
				if (heat >= 0.19)
				{
					strcpy(chatboxOutputString, "[##--------] 20\%");
				}
				if (heat >= 0.29)
				{
					strcpy(chatboxOutputString, "[###-------] 30\%");
				}
				if (heat >= 0.39)
				{
					strcpy(chatboxOutputString, "[####------] 40\%");
				}
				if (heat >= 0.49)
				{
					strcpy(chatboxOutputString, "[#####-----] 50\%");
				}
				if (heat >= 0.59)
				{
					strcpy(chatboxOutputString, "[######----] 60\%");
				}
				if (heat >= 0.69)
				{
					strcpy(chatboxOutputString, "[#######---] 70\%");
				}
				if (heat >= 0.79)
				{
					strcpy(chatboxOutputString, "[########--] 80\%");
				}
				if (heat >= 0.89)
				{
					strcpy(chatboxOutputString, "[#########-] 90\%");
				}
				if (heat >= 0.99)
				{
					strcpy(chatboxOutputString, "[##########] 100\%");
				}
				//minioscSend( oscout, "/chatbox/input", ",sT", chatboxOutputString);
			}

			OldClocksText = Clocks;
		}

		if (oldheat != heat)
		{
			minioscSend( oscout, "/avatar/parameters/ThatFace", ",f", heat);
			oldheat = heat;
		}

		if (headpat > .1)
		{
			headpat = .1;
		}

		if (oldheadpat != headpat)
		{
			minioscSend( oscbut, "/devices/all/vibrate/speed", ",f", headpat);
			oldheadpat = headpat;
		}

// =============================================================================


		framenumber++;
		lastlook--;
	}

	return 0;
}