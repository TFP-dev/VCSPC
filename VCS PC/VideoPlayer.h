#ifndef __VIDEOPLAYER
#define __VIDEOPLAYER

class CVideoPlayer
{
private:
	static BINK*				m_hBinkPlayer;
	static BINKBUFFER*			m_hBinkBuffer;
	static RwRaster*			m_pVideoRaster;
	static CRect				m_videoFrame;
	static unsigned char		m_bSurfaceMask;
	static unsigned char		m_bExtraThreadIndex;

private:
	static void					UpdateVideoFrame(const CRect* pVideoFrame);

public:
	static void					Create(const char* pFileName, const CRect* pVideoFrame = nullptr, bool bAudio = true);
	static void					Release();
	static bool					PlayNextFullscreenFrame();
	static void					PlayNextFrame();
	static void					WindowProc(WPARAM wParam);
};

#endif