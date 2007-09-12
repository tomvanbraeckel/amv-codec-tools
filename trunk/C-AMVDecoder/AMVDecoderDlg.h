// AMVDecoderDlg.h : header file
//

#if !defined(AFX_AMVDECODERDLG_H__DF4A56B3_49C1_429D_BA63_5BDB587D67FA__INCLUDED_)
#define AFX_AMVDECODERDLG_H__DF4A56B3_49C1_429D_BA63_5BDB587D67FA__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "amvlib/AmvDec.h"

#define PLAY_STOP			0
#define PLAY_START			1
#define PLAY_ING			2
#define PLAY_PAUSE			3
/////////////////////////////////////////////////////////////////////////////
// CAMVDecoderDlg dialog
#define	OutBlocks			2			// 存储输出音频数据的单元数
struct CAudioData
{
	PBYTE lpdata;
	DWORD dwLength;
};

class CAMVDecoderDlg : public CDialog
{
// Construction
public:
	BOOL m_bShutOff;
	void FillBuffer(char* );
	int DrawPicture(CDC *pDC, CRect *rect, VIDEOBUFF *vbuff);
	CAMVDecoderDlg(CWnd* pParent = NULL);	// standard constructor

	int isPlay;
	int bufflock;
	int audioopened;

	CString amvfile;
	AMVDecoder *amvdec;
	AMVInfo *amvinfo;
	FRAMEBUFF *framebuf;
	VIDEOBUFF *videobuf;
	AUDIOBUFF *audiobuf;

	CDC *pDC; //屏幕绘图设备
	BITMAPINFO m_BmpInfo;
	HBITMAP hBmp;
	CDC MemDC;
	
	LPDIRECTSOUND lpds;
	DSCAPS dscaps;
	LPDIRECTSOUNDBUFFER *lplpDsb;
	DSBUFFERDESC dsbdesc;
	PCMWAVEFORMAT pcmwf;
	HWAVEOUT m_hWaveOut;
	PWAVEHDR m_pWaveHdr1;
	PWAVEHDR m_pWaveHdr2;
	char*    m_pBuffer1;
	char*    m_pBuffer2;	
	WAVEFORMATEX m_waveformout;
//	CAudioData m_AudioData1;
//	CAudioData m_AudioData2;
//	CAudioData m_AudioDataOut2[OutBlocks];
	int nAudioOut, nReceive;//接收、播放指针
// Dialog Data
	//{{AFX_DATA(CAMVDecoderDlg)
	enum { IDD = IDD_AMVDECODER_DIALOG };
	CButton	m_btnGetWav;
	CButton	m_btnStop;
	CButton	m_btnPause;
	CButton	m_btnOpen;
	CButton	m_btnGetPic;
	CButton	m_btnPlay;
	CButton	m_btnClose;
	CStatic	m_pic;
	CString	m_info;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAMVDecoderDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CAMVDecoderDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnButtonOpen();
	afx_msg void OnClose();
	afx_msg void OnButtonClose();
	afx_msg void OnButtonPlay();
	afx_msg void OnButtonPause();
	afx_msg void OnButtonStop();
	afx_msg void OnButtonGetpic();
	afx_msg void OnMM_WOM_OPEN(UINT wParam, LONG lParam);
	afx_msg void OnMM_WOM_DONE(UINT wParam, LONG lParam);
	afx_msg void OnMM_WOM_CLOSE(UINT wParam, LONG lParam);
	afx_msg void OnButtonGetwav();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_AMVDECODERDLG_H__DF4A56B3_49C1_429D_BA63_5BDB587D67FA__INCLUDED_)
