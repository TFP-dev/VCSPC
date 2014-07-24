#include "StdAfx.h"
#include "Vehicle.h"

#include "Antennas.h"
#include "Timer.h"
#include "PlayerInfo.h"
#include "Pad.h"
#include "RealTimeShadowMgr.h"

WRAPPER void CVehicle::SetWindowOpenFlag(unsigned char nWindow) { WRAPARG(nWindow); EAXJMP(0x6D3080); }
WRAPPER void CVehicle::ClearWindowOpenFlag(unsigned char nWindow) { WRAPARG(nWindow); EAXJMP(0x6D30B0); }

bool&	CVehicle::m_bEnableMouseSteering = *(bool*)0xC1CC02;
bool&	CVehicle::m_bEnableMouseFlying = *(bool*)0xC1CC03;

static RwObject* GetCurrentAtomicObjectCB(RwObject* pObject, void* data)
{
	if ( RpAtomicGetFlags(pObject) & rpATOMICRENDER )
		*static_cast<RwObject**>(data) = pObject;
	return pObject;
}

static RpMaterial* SetCompAlphaCB(RpMaterial* pMaterial, void* data)
{
	pMaterial->color.alpha = reinterpret_cast<RwUInt8>(data);
	return pMaterial;
}

void CVehicle::SetComponentAtomicAlpha(RpAtomic* pAtomic, int nAlpha)
{
	RpGeometry*	pGeometry = RpAtomicGetGeometry(pAtomic);
	pGeometry->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;

	RpGeometryForAllMaterials(pGeometry, SetCompAlphaCB, reinterpret_cast<void*>(nAlpha));
}

void CVehicle::RenderForShadow(RpClump* pClump)
{
	RpClumpForAllAtomics(pClump, ShadowCameraRenderCB, nullptr);

	// Is open top?
	if ( m_dwVehicleSubClass == VEHICLE_QUAD || m_dwVehicleSubClass == VEHICLE_BIKE || m_dwVehicleSubClass == VEHICLE_BMX )
	{
		// Render driver
		if ( m_pDriver )
			m_pDriver->RenderForShadow(reinterpret_cast<RpClump*>(m_pDriver->m_pRwObject));

		// Render passengers
		for ( int i = 0; i < 8; i++ )
		{
			if ( m_apPassengers[i] )
				m_apPassengers[i]->RenderForShadow(reinterpret_cast<RpClump*>(m_apPassengers[i]->m_pRwObject));
		}
	}
}

#include "Font.h"

static float		fCurrentY;

static RwFrame* DisplayFrameInfo(RwFrame* frame, void* pData)
{
	char			Temp[256];

	RwMatrix*		pMat = RwFrameGetMatrix(frame);
	int				nFlags = rwMatrixGetFlags(pMat);
	sprintf(Temp, "%g %g %g %X", pMat->pos.x, pMat->pos.y, pMat->pos.z, nFlags);
	//rwMatrixSetFlags(pMat, 0);

	CFont::SetProportional(true);
	CFont::SetEdge(1);
	CFont::SetFontStyle(FONT_Eurostile);
	CFont::SetScale(_width(0.3f), _height(0.5f));
	CFont::SetOrientation(ALIGN_Left);
	CFont::SetColor(CRGBA(255, 255, 255, 255));
	CFont::SetDropColor(CRGBA(0, 0, 0, 255));
	CFont::PrintString(_xleft(10.0f), _y(fCurrentY), Temp);

	fCurrentY += 10.0f;

	RwFrameForAllChildren(frame, DisplayFrameInfo, pData);
	return frame;
}

void CAutomobile::DebugWheelDisplay()
{
	fCurrentY = 10.0f;

	DisplayFrameInfo(m_pCarNode[2], nullptr);
	DisplayFrameInfo(m_pCarNode[5], nullptr);
	DisplayFrameInfo(m_pCarNode[4], nullptr);
	DisplayFrameInfo(m_pCarNode[7], nullptr);
}

void CAutomobile::ControlWindows()
{
	if ( CPad::GetPad(0)->RightShockJustDown() )
	{
		static bool		bWindowsOpen = false;

		if ( !bWindowsOpen )
		{
			SetWindowOpenFlag(8);
			SetWindowOpenFlag(10);
		}
		else
		{
			ClearWindowOpenFlag(8);
			ClearWindowOpenFlag(10);
		}

		bWindowsOpen = bWindowsOpen == false;
	}
}

void CHeli::Render()
{
	double		dRotorsSpeed, dMovingRotorSpeed;

	m_nTimeTillWeNeedThisCar = CTimer::m_snTimeInMilliseconds + 3000;

	if ( m_fRotorSpeed > 0.0 )
		dRotorsSpeed = min(1.7 * (1.0/0.22) * m_fRotorSpeed, 1.5);
	else
		dRotorsSpeed = 0.0;

	dMovingRotorSpeed = dRotorsSpeed - 0.4;
	if ( dMovingRotorSpeed < 0.0 )
		dMovingRotorSpeed = 0.0;

	int			nStaticRotorAlpha = min(static_cast<int>((1.5-dRotorsSpeed) * 255.0), 255);
	int			nMovingRotorAlpha = min(static_cast<int>(dMovingRotorSpeed * 150.0), 150);

	if ( m_pCarNode[11] )
	{
		RpAtomic*	pOutAtomic = nullptr;
		RwFrameForAllObjects(m_pCarNode[11], GetCurrentAtomicObjectCB, &pOutAtomic);
		if ( pOutAtomic )
			SetComponentAtomicAlpha(pOutAtomic, nStaticRotorAlpha);
	}

	if ( m_pCarNode[13] )
	{
		RpAtomic*	pOutAtomic = nullptr;
		RwFrameForAllObjects(m_pCarNode[13], GetCurrentAtomicObjectCB, &pOutAtomic);
		if ( pOutAtomic )
			SetComponentAtomicAlpha(pOutAtomic, nStaticRotorAlpha);
	}

	if ( m_pCarNode[12] )
	{
		RpAtomic*	pOutAtomic = nullptr;
		RwFrameForAllObjects(m_pCarNode[12], GetCurrentAtomicObjectCB, &pOutAtomic);
		if ( pOutAtomic )
			SetComponentAtomicAlpha(pOutAtomic, nMovingRotorAlpha);
	}

	if ( m_pCarNode[14] )
	{
		RpAtomic*	pOutAtomic = nullptr;
		RwFrameForAllObjects(m_pCarNode[14], GetCurrentAtomicObjectCB, &pOutAtomic);
		if ( pOutAtomic )
			SetComponentAtomicAlpha(pOutAtomic, nMovingRotorAlpha);
	}

	CEntity::Render();
}

CColModel* CAutomobile::RenderAntennas()
{
	if ( m_nModelIndex == VT_RCBANDIT )
	{
		CMatrix*	pCoords = GetMatrix();
		if ( pCoords )
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this), *pCoords->GetAt(), *pCoords * CVector(0.13f, -0.55f, 0.5f), 1.0f, 0.9f);
	}
	if ( m_nModelIndex == VT_WILLARD )
	{
		CMatrix*	pCoords = GetMatrix();
		if ( pCoords )
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this), *pCoords->GetAt(), *pCoords * CVector(-1.0f, 0.84f, 0.375f), 1.1f, 0.93f);
	}
	/*else if ( m_nModelIndex == VT_STINGER )
	{
		CMatrix*	pCoords = GetMatrix();
		if ( pCoords )
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this), *pCoords->GetAt(), *pCoords * CVector(-0.8f, -1.75f, 0.2f), 0.5f, 0.9f);
	}*/
	/*else if ( m_nModelIndex == VT_POLICEM )
	{
		CMatrix*	pCoords = GetMatrix();
		if ( pCoords )
		{
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this), *pCoords->GetAt(), *pCoords * CVector(0.9f, -2.25f, 0.22f), 1.25f, 0.95f);
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this) + 1, *pCoords->GetAt(), *pCoords * CVector(-0.9f, -2.25f, 0.22f), 1.25f, 0.95f);
			CAntennas::RegisterOne(reinterpret_cast<unsigned int>(this) + 2, (*pCoords->GetAt() - (*pCoords->GetUp() * 0.5f)).Normalize(), *pCoords * CVector(0.0f, -1.1f, 0.68f), 0.75f, 0.9f);
		}
	}*/

#ifdef CONTROLLABLE_WINDOWS_TEST
	if ( this == FindPlayerVehicle(-1, false) )
		ControlWindows();
#endif

	return ((CColModel*(__thiscall*)(CEntity*))0x535300)(this);
}