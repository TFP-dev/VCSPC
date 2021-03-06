#pragma warning(disable:4481)	// nonstandard extension used: override specifier 'override'
#pragma warning(disable:4401)	// member is bit field
#pragma warning(disable:4733)	// handler not registered as safe handler
#pragma warning(disable:4725)	// instruction may be inaccurate on some Pentiums

#pragma warning(disable: 4302)	// 'reinterpret_cast': truncation from 'void *' to 'RwUInt8'
#pragma warning(disable: 4624)	// destructor was implicitly defined as deleted
#pragma warning(disable: 4100)	// unreferenced formal parameter
#pragma warning(disable: 4244)	// conversion from '' to 'BYTE', possible loss of data
#pragma warning(disable: 4996)	// The POSIX name for this item is deprecated
#pragma warning(disable: 4458)	// declaration of 'pMatrix' hides class member
#pragma warning(disable: 4189)	// local variable is initialized but not referenced
#pragma warning(disable: 4505)	// unreferenced local function has been removed


#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define _USE_MATH_DEFINES

#define WINVER 0x0501
#define _WIN32_WINNT 0x0501

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <mmsystem.h>
#include <dsound.h>
#include <ctime>
#include <cmath>
#include <vector>
#include <string>
#include <map>
#include <cassert>
#include <xinput.h>
#include <Shlwapi.h>
#include <ShlObj.h>

#pragma warning(disable:4512)
#include <boost\bimap.hpp>
#pragma warning(default:4512)

#include "buildnumber.h"

typedef uint8_t uint8;
typedef int8_t int8;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;


//#define INCLUDE_AUDIO_DEBUG
#define INCLUDE_WINDOW_NAME
//#define INCLUDE_VCSIMG_LOADER
//#define INCLUDE_MULTIFONTFILES

#ifdef DEVBUILD
//#define EXTRA_ASH_LOGGING
//#define TRACE_D3D_MARKER
//#define EXTRA_WINDOWMODE_CHECKING
//#define ADD_UNFINISHED_WINDOWED_MODE
//#define TRACE_PROJECTILES
//#define NO_AUDIO
//#define TRACE_COLOUR_STUFF
//#define TRACE_STREAMING
//#define TRACE_STREAMING2
//#define TRACE_CTEXT
//#define TRACE_UPCASE
//#define TRACE_NODES_STREAMS
//#define INCLUDE_COOL_BINK_SCREEN
//#define INCLUDE_SQUARE_RADAR
//#define INCLUDE_TRIANGLE_RADAR
//#define TRACE_STREAMS
//#define TRACE_FUCKING_TEXTURES
//#define INCLUDE_STREAMING_TEXT
//#define LIST_MEMORY
//#define MAKE_CONSOLE
//#define DUMP_MENUS
//#define V_RADAR_TEST
//#define DEBUG_GETMODELINFO_WRAPPER
//#define FANCY_FRONTEND_CONTROLLERS_TEST
//#define SHOW_FOV_FANCY_RHYME
#define NO_REPLAY_TEXT
//#define SKIP_DEBUG_TEXTS
//#define RENDER_THREAD_TEST
//#define TRACE_TEXTURE_FILTERMODES
//#define RWERRORSET_HOOK
//#define CONTROLLABLE_WINDOWS_TEST
//#define MULTITHREADING_TEST
//#define NATIVE_MESH_CONVERTER

//#define CONTROLLABLE_FOV

//#define MAKE_ZZCOOL_MOVIE_DEMO

#endif

#define DONT_FIX_STREAMING

#define _x(a) (RsGlobal.MaximumWidth - WidescreenSupport::fProperWidthMultiplier * (a))
#define _xleft(a) (WidescreenSupport::fProperWidthMultiplier * (a))
#define _xmiddle(a) ((RsGlobal.MaximumWidth / 2) + WidescreenSupport::fProperWidthMultiplier * (a))
#define _y(a) (WidescreenSupport::fProperHeightMultiplier * (a))
#define _ydown(a) (RsGlobal.MaximumHeight - WidescreenSupport::fProperHeightMultiplier * (a))
#define _ymiddle(a) ((RsGlobal.MaximumHeight / 2) + WidescreenSupport::fProperHeightMultiplier * (a))
#define _width(a) _xleft(a)
#define _height(a) _y(a)
#define _coords(a) (GetCoord(a))

#define MOD_VERSION	"v3.5 BETA"
#define VERSION_NAME "Silver Palmetto"
#define VERSION_NAME_UPPERCASE "SILVER PALMETTO"
#define RELEASE_CANDIDATE "1"

#define	PlayerStatsInteger	((int*)0xB78E20)
#define	PlayerStatsFloat	((float*)0xB79380)

// Retrieving RGB from RwRGBA
#define RwRGBAGetRGB(a) (*(DWORD*)&(a) & 0xFFFFFF)

// Some handy funcs
void			LogToFile(const char* str, ...);
inline void		ToLower(char* str, BYTE len)
{
	do
	{
		if ( *str >= 'A' && *str <= 'Z' )
			*str += 0x20;
		++str;
		--len;
	}
	while ( len );
}

template<typename T>
inline T random(T a, T b)
{
	return a + static_cast<T>(rand() * (1.0f/RAND_MAX) * (b - a));
}

template<typename T>
inline T Min(const T& a, const T& b)
{
	return a > b ? b : a;
}

template<typename T>
inline T Max(const T& a, const T& b)
{
	return a > b ? a : b;
}

#ifdef MAKE_CONSOLE
#define echo(x, ...) printf(x, __VA_ARGS__)
#else
#define echo(x, ...)
#endif

#if defined DEVBUILD && !defined MAKE_ZZCOOL_MOVIE_DEMO
void			EnterAmazingScreenshotMode(bool bEnable);
#endif


#include <rwsdk\rwcore.h>
#include <rwsdk\rpworld.h>
#include <rwsdk\rtpng.h>
#include <rwsdk\rpanisot.h>
#include <rwsdk\rphanim.h>
#include <rwsdk\rpmatfx.h>

#include <d3d9.h>

#undef RXPIPELINEGLOBAL
#define RXPIPELINEGLOBAL(var) (RWPLUGINOFFSET(rwPipeGlobals, RwEngineInstance, *(RwInt32*)0xC9BC60)->var)

#include "CCRC32.h"
#include "MemoryMgr.h"
#include "StaticPatcher.h"
#include "Maths.h"
#include "Rs.h"
#include "WidescreenSupport.h"
#include "FileMgr.h"

extern char*				latestMissionName;
extern char*				PriorityText;
extern char*				StyledText_1;
extern char*				StyledText_2;
extern char*				StyledText_3;
extern char*				StyledText_4;
extern char*				StyledText_5;
extern char*				StyledText_6;
extern char*				StyledText_7;
extern char*				Garage_MessageIDString;
extern uint8_t*				scmBlock;
extern DWORD*				CPopulation__allRandomPedThisType;
extern DWORD*				PlayerEnexEntryStage;
extern DWORD*				activeInterior;
extern DWORD*				memoryAvailable;
extern DWORD*				memoryUsed;
extern float*				WidthAspectRatio;
extern float*				HeightAspectRatio;
extern float&				ScreenAspectRatio;
extern float*				fFOV;
extern BYTE*				bWants_To_Draw_Hud;
extern BYTE*				radarGrey;
extern bool*				bHideStyledTextWhileFading;
extern float*				currentFPS;
//extern CGridref*			gridref;
//extern CSprite2d*			texFonts;
extern void					(*TheScriptsInitFunc)();

extern bool					InAmazingScreenshotMode;

extern char					gString[552];
extern char					gUString[552];
extern CCRC32				HashHelper;

extern bool                 bNoOutro;
extern bool                 bNoAudio;
extern void                 InitExtraStuff();

extern void                 PatchStats();

extern void                 RotateVertices(CVector2D *rect, unsigned int numVerts, float x, float y, float angle);

#ifdef INCLUDE_MULTIFONTFILES
extern bool					bCanReloadFonts;
extern eFontFileIDs			bLastFontsID;
#endif

