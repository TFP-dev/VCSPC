#ifndef __WIDESCREENSUPPORT
#define __WIDESCREENSUPPORT

class WidescreenSupport
{
public:
	static long&	nCTRubberSlider;
	static long*&	nCTRubberSliderMinPos;
	static long*&	nCTRubberSliderMaxPos;
	static float*&	fHorizontalAspectRatio;
	static float*&	fVerticalAspectRatio;
	static float	fScreenWidthMultiplier;
	static float	fScreenWidthDivider;
	static float	fScreenHeightMultiplier;
	static float	f4;
	static float	f40;
	static float	f45;
	static float	f50;
	static float	f55;
	static float	f60;
	static float	f70;
	static float	f95;
	static float	f100;
	static float	f160;
	static float	f350;
	static float	f555;
	static float	f580;
	static float	f0pt3;
	static float	f1pt3;
	static float	f0pt49;
	static float	f0pt42;
	static float	f0pt35;
	static float	f0pt7;
	static float	f0pt8;
	static float	f0pt56;
	static float	fMenuSliderPosX;
	static float	fMenuSliderWidth;
	static float	fMenuMessageWidth;
	static float	fCTSliderRight;
	static float	fCTSliderLeft;
	static float	fScreenCoorsFix;
//	static float	fSpawningFix;
	static float	fAimpointFix;
	static float	fMapZonePosX2;
//	static float	fSkyMultFix;

	static float	f0pt7_h;
	static float	f0pt95_h;
	static float	f1pt2_h;
	static float	f2pt1_h;
	static float	f1_h;
	static float	f28_h;
	static float	f97_centh;
	static float	fMenuSliderPosY2;
	static float	fMenuSliderPosY3;
	static float	fMenuSliderPosY4;
	static float	fMenuSliderHeight2;

	static float	fProperWidthMultiplier;
	static float	fProperHeightMultiplier;

//	static float	fTextDrawsWidthMultiplier;

	static const float	fFOVMultiplier;

	static void				Recalculate(long nWidth, long nHeight, bool bAlways);
	static float			SetAspectRatio();
	static float			GetTextPosition();
//	static float			GetSkyWidth();
	static unsigned char	GetTextBoxPos();
//	static void		SetFieldOfView(float FOV);

	inline static float GetScreenWidthMultiplier()
	{
		return fScreenWidthMultiplier;
	}

	inline static float GetScreenWidthDivider()
	{
		return fScreenWidthDivider;
	}

	inline static float GetScreenHeightMultiplier()
	{
		return fScreenHeightMultiplier;
	}
};

#endif