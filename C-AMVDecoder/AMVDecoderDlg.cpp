// AMVDecoderDlg.cpp : implementation file
//

#include "stdafx.h"
#include "AMVDecoder.h"
#include "AMVDecoderDlg.h"
#include <math.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//////////////////////////////////////////////////////////////////////////////
int  BUFFERSIZE=3675;
/////////////////////////////////////////////////////////////////////////////

// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();
	
	// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA
	
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL
	
	// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
//{{AFX_MSG_MAP(CAboutDlg)
// No message handlers
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAMVDecoderDlg dialog

CAMVDecoderDlg::CAMVDecoderDlg(CWnd* pParent /*=NULL*/)
: CDialog(CAMVDecoderDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAMVDecoderDlg)
	m_info = _T("");
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_bShutOff=FALSE;
}

void CAMVDecoderDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAMVDecoderDlg)
	DDX_Control(pDX, IDC_BUTTON_GETWAV, m_btnGetWav);
	DDX_Control(pDX, IDC_BUTTON_STOP, m_btnStop);
	DDX_Control(pDX, IDC_BUTTON_PAUSE, m_btnPause);
	DDX_Control(pDX, IDC_BUTTON_OPEN, m_btnOpen);
	DDX_Control(pDX, IDC_BUTTON_GETPIC, m_btnGetPic);
	DDX_Control(pDX, IDC_BUTTON_PLAY, m_btnPlay);
	DDX_Control(pDX, IDC_BUTTON_CLOSE, m_btnClose);
	DDX_Control(pDX, IDC_STATIC_PIC, m_pic);
	DDX_Text(pDX, IDC_STATIC_INFO, m_info);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAMVDecoderDlg, CDialog)
//{{AFX_MSG_MAP(CAMVDecoderDlg)
ON_WM_SYSCOMMAND()
ON_WM_PAINT()
ON_WM_QUERYDRAGICON()
ON_WM_TIMER()
ON_BN_CLICKED(IDC_BUTTON_OPEN, OnButtonOpen)
ON_WM_CLOSE()
ON_BN_CLICKED(IDC_BUTTON_CLOSE, OnButtonClose)
ON_BN_CLICKED(IDC_BUTTON_PLAY, OnButtonPlay)
ON_BN_CLICKED(IDC_BUTTON_PAUSE, OnButtonPause)
ON_BN_CLICKED(IDC_BUTTON_STOP, OnButtonStop)
ON_BN_CLICKED(IDC_BUTTON_GETPIC, OnButtonGetpic)
ON_MESSAGE(MM_WOM_OPEN, OnMM_WOM_OPEN)
ON_MESSAGE(MM_WOM_DONE, OnMM_WOM_DONE)
ON_MESSAGE(MM_WOM_CLOSE, OnMM_WOM_CLOSE)
ON_BN_CLICKED(IDC_BUTTON_GETWAV, OnButtonGetwav)
//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAMVDecoderDlg message handlers

BOOL CAMVDecoderDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	
	// Add "About..." menu item to system menu.
	
	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);
	
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}
	
	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here
	isPlay = PLAY_STOP;
	bufflock = 0;
	
	audioopened = 0;
	
	/////////////////////////////////////////////////////////////////////////////////////////
	
	////////////////////////////////////////////////////////////////////////////////////////////
	amvdec = NULL;
	amvinfo = NULL;
	framebuf = NULL;
	videobuf = NULL;
	audiobuf = NULL;
	
	m_btnPlay.EnableWindow(FALSE);
	m_btnPause.EnableWindow(FALSE);
	m_btnStop.EnableWindow(FALSE);
	m_btnGetPic.EnableWindow(FALSE);
	m_btnGetWav.EnableWindow(FALSE);
	
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CAMVDecoderDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CAMVDecoderDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting
		
		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);
		
		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;
		
		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CAMVDecoderDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}


void CAMVDecoderDlg::OnMM_WOM_OPEN(UINT wParam, LONG lParam)
{   
	m_pWaveHdr1 = new WAVEHDR;
	m_pWaveHdr2 = new WAVEHDR;
	m_pBuffer1  = new char [BUFFERSIZE];
	m_pBuffer2  = new char [BUFFERSIZE];
	if(!m_hWaveOut)
		MessageBox("m_hWaveOut is a INVALID HANDLE!");
	if(!m_pWaveHdr1 ||  !m_pWaveHdr1 || !m_pBuffer1 || !m_pBuffer2)
		MessageBox("malloc memory failed!");
	
	m_pWaveHdr1->lpData  =(LPTSTR) m_pBuffer1 ;
	m_pWaveHdr1->dwBufferLength  = BUFFERSIZE ;
	m_pWaveHdr1->dwBytesRecorded = 0 ;
	m_pWaveHdr1->dwUser          = 0 ;
	m_pWaveHdr1->dwFlags         = 0 ;
	m_pWaveHdr1->dwLoops         = 0 ;
	m_pWaveHdr1->lpNext          = NULL ;
	m_pWaveHdr1->reserved        = 0 ;
	
	if(!(MMSYSERR_NOERROR==waveOutPrepareHeader (m_hWaveOut, m_pWaveHdr1,sizeof (WAVEHDR))))
		MessageBox("waveOutPrepareHeader m_pWaveHdr1 failed");
	
	m_pWaveHdr2->lpData          = (LPTSTR)m_pBuffer2 ;
	m_pWaveHdr2->dwBufferLength  = BUFFERSIZE ;
	m_pWaveHdr2->dwBytesRecorded = 0 ;
	m_pWaveHdr2->dwUser          = 0 ;
	m_pWaveHdr2->dwFlags         = 0 ;
	m_pWaveHdr2->dwLoops         = 1 ;
	m_pWaveHdr2->lpNext          = NULL ;
	m_pWaveHdr2->reserved        = 0 ;
	
	if(!(MMSYSERR_NOERROR==waveOutPrepareHeader (m_hWaveOut, m_pWaveHdr2,sizeof (WAVEHDR))))
		MessageBox("waveOutPrepareHeader m_pWaveHdr2 failed");
	//	MessageBox("in MM_WOM_OPEN message handler");
	FillBuffer (m_pBuffer1) ;
	waveOutWrite (m_hWaveOut, m_pWaveHdr1, sizeof (WAVEHDR)) ;
	
	FillBuffer (m_pBuffer2) ;
	waveOutWrite (m_hWaveOut, m_pWaveHdr2, sizeof (WAVEHDR)) ;
	
}

void CAMVDecoderDlg::OnMM_WOM_DONE(UINT wParam, LONG lParam)
{
	//	MessageBox("in MM_WOM_DONE message handler");
	if(m_bShutOff && m_hWaveOut)
	{
		//		MessageBox("Close the waveDevice next,IN MM_WOM_DONE");
		waveOutClose(m_hWaveOut);
		return;
	}
	if(!m_bShutOff)
	{
		FillBuffer (((PWAVEHDR) lParam)->lpData) ;
		waveOutWrite (m_hWaveOut, (PWAVEHDR) lParam, sizeof (WAVEHDR)) ;
	}
}

void CAMVDecoderDlg::OnMM_WOM_CLOSE(UINT wParam,LONG lParam)
{
	//	MessageBox("in MM_WOM_CLOSE message handler");
	m_hWaveOut=NULL;
	waveOutUnprepareHeader (m_hWaveOut, m_pWaveHdr1, sizeof (WAVEHDR)) ;
	waveOutUnprepareHeader (m_hWaveOut, m_pWaveHdr2, sizeof (WAVEHDR)) ;
	
	free (m_pWaveHdr1) ;
	free (m_pWaveHdr2) ;
	free (m_pBuffer1) ;
	free (m_pBuffer2) ;	
}


int CAMVDecoderDlg::DrawPicture(CDC *pDC, CRect *rect, VIDEOBUFF *vbuff)
{
	hBmp = CreateDIBitmap(
		pDC->m_hDC,					// handle to device context
		(BITMAPINFOHEADER*)(&m_BmpInfo),   //	pointer to bitmap size and format data
		CBM_INIT,	// initialization flag
		vbuff->fbmpdat,	// pointer to initialization data
		&m_BmpInfo,	// pointer to bitmap color-format data
		DIB_RGB_COLORS	// color-data usage
		);
	
	if(hBmp == NULL)
	{
		pDC->DeleteDC();
		return -1;
	}
	
	MemDC.CreateCompatibleDC(pDC);
	SelectObject(MemDC.m_hDC, hBmp);
	DeleteObject(hBmp);
	pDC->BitBlt(0, 0, rect->Width(), rect->Height(), &MemDC, 0, 0, SRCCOPY);
	
	MemDC.DeleteDC();
	
	return 0;
}

void CAMVDecoderDlg::OnTimer(UINT nIDEvent) 
{
	// TODO: Add your message handler code here and/or call default
	
	
	if(nIDEvent == 0)
	{

		
	}
	
	
	CDialog::OnTimer(nIDEvent);
}

void CAMVDecoderDlg::OnButtonOpen() 
{
	// TODO: Add your control notification handler code here
	static char BASED_CODE szFilter[] = "AMV Media Files (*.amv)|*.amv|All Files (*.*)|*.*||";
	
	CFileDialog dlg(TRUE, NULL, NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, szFilter, NULL);
	if(dlg.DoModal() == IDOK)
	{
		amvfile = dlg.GetPathName();
		
		if(isPlay == PLAY_ING || isPlay == PLAY_PAUSE)
			KillTimer(0);
		
		if(amvdec)
			AmvClose(amvdec);
		amvdec = AmvOpen(amvfile);
		if(amvdec == NULL)
			return;
		amvinfo = &(amvdec->amvinfo);
		
		m_info.Format("视频尺寸: %d X %d, 帧速率: %d fps, 播放时间: %dh-%dm-%ds",
			amvinfo->dwWidth, amvinfo->dwHeight, amvinfo->dwSpeed,
			amvinfo->dwTimeHour, amvinfo->dwTimeMin, amvinfo->dwTimeSec);
		UpdateData(FALSE);
		//////////////////////////////////////////////////////////////////////////////////		
		////////////////////////////////////              //////////////////////////////////////
		////////////////////////////////////   VIDEO      /////////////////////////////////
		CRect rect, rect1;
		GetClientRect(&rect);
		m_btnClose.GetWindowRect(rect1);
		m_pic.MoveWindow(rect.left + (rect.Width()-amvinfo->dwWidth - rect1.Width())/2, 
			rect.top + 20, 
			amvinfo->dwWidth, amvinfo->dwHeight, TRUE);
		
		// 画初始视频图像
		bufflock = 1;
		AmvReadNextFrame(amvdec);
		bufflock = 0;
		framebuf = &(amvdec->framebuf);
		///////////////////////start to decode video ///////////////////////////////////////
		AmvVideoDecode(amvdec);
		videobuf = &(amvdec->videobuf);
		
		// 获取绘制坐标的文本框
		CWnd* pWnd = GetDlgItem(IDC_STATIC_PIC);                			
		//获得对话框上的picture的窗口句柄
		pWnd->GetClientRect(&rect);
		// 指针
		pDC = pWnd->GetDC();
		
		m_BmpInfo.bmiHeader.biSize			= sizeof(BITMAPINFOHEADER);
		m_BmpInfo.bmiHeader.biWidth			= amvinfo->dwWidth;
		m_BmpInfo.bmiHeader.biHeight		= -amvinfo->dwHeight;
		m_BmpInfo.bmiHeader.biCompression	= BI_RGB;
		m_BmpInfo.bmiHeader.biPlanes		= 1;
		m_BmpInfo.bmiHeader.biBitCount		= 24;
		m_BmpInfo.bmiHeader.biSizeImage		= amvinfo->dwHeight*amvinfo->dwWidth*3;    
		
		DrawPicture(pDC, &rect, videobuf);
		
		/////////////////////////////////////////////////////////////////////////////////////////		
		isPlay = PLAY_START;
		m_btnPlay.EnableWindow(TRUE);
		m_btnPause.EnableWindow(TRUE);
		m_btnStop.EnableWindow(TRUE);
		m_btnGetPic.EnableWindow(TRUE);
		m_btnGetWav.EnableWindow(TRUE);
		///////////////////////////////////////////////////////////////////////////////////////
		//////////////////////     audio               /////////////////////////////////////////
		/////////////////////                          ////////////////////////////////////////
		if(m_hWaveOut)
		{
			m_bShutOff=TRUE;
			waveOutReset(m_hWaveOut);
		}
		////////////////////                          ////////////////////////////////////////
		////////////////////      audio               //////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////
//================================================audio===============================//
/*	AmvAudioDecode(amvdec);
	audiobuf = &(amvdec->audiobuf);

		m_waveformout.wFormatTag		= WAVE_FORMAT_PCM;
		m_waveformout.nChannels		    = amvinfo->nChannels;
		m_waveformout.nSamplesPerSec	= amvinfo->nSamplesPerSec;
		m_waveformout.nAvgBytesPerSec   = amvinfo->nAvgBytesPerSec;
//		BUFFERSIZE=audiobuf->len;
		BUFFERSIZE=amvinfo->nAvgBytesPerSec / 12;
		m_waveformout.nBlockAlign	    = amvinfo->nBlockAlign;
		m_waveformout.wBitsPerSample	= amvinfo->wBitsPerSample;
		m_waveformout.cbSize			= amvinfo->cbSize;
		
		if(waveOutOpen(&m_hWaveOut, WAVE_MAPPER, &m_waveformout, 
			(DWORD)this->m_hWnd, NULL, CALLBACK_WINDOW))
		{
			audioopened = 0;
			MessageBeep(MB_ICONEXCLAMATION);
			AfxMessageBox("Audio output erro");
		}

		m_bShutOff=FALSE;*/

//====================================================================================//
	}

}

void CAMVDecoderDlg::OnClose() 
{
	// TODO: Add your message handler code here and/or call default
	if(amvdec)
	{
		AmvClose(amvdec);
	}
	
	if(audioopened)
	{
		//		waveOutUnprepareHeader(hWaveOut, pWaveHdrOut, sizeof(WAVEHDR));
		//		waveOutReset(hWaveOut);
		//		waveOutClose(hWaveOut);
	}
	if(m_hWaveOut)
	{
		m_bShutOff=TRUE;
		waveOutReset(m_hWaveOut);
	}
	
	CDialog::OnClose();
}

void CAMVDecoderDlg::OnButtonClose() 
{
	// TODO: Add your control notification handler code here
	//	MessageBox("Close waveDevice Next,IN OnButtonClose");
	if(m_hWaveOut)
	{
		
		m_bShutOff=TRUE;
		waveOutReset(m_hWaveOut);
	}
	SendMessage(WM_CLOSE);
}

void CAMVDecoderDlg::OnButtonPlay() 
{
	// TODO: Add your control notification handler code here
	if(isPlay == PLAY_START)
	{
		//SetTimer(0, 1000/amvinfo->dwSpeed, NULL);
		m_btnPlay.EnableWindow(FALSE);
		isPlay = PLAY_ING;
	}
	else if(isPlay == PLAY_PAUSE)
	{
		//SetTimer(0, 1000/amvinfo->dwSpeed, NULL);
		
		isPlay = PLAY_ING;
	}
	m_btnPause.SetWindowText("暂停");
	m_btnPause.EnableWindow(TRUE);
	/////////////////////////////////////////////audio/////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////                           ///////////////////////////////////
	//////////////////////////       AUDIO               //////////////////////////////////
	AmvAudioDecode(amvdec);
	audiobuf = &(amvdec->audiobuf);
	if(audioopened == 0)
	{
		m_waveformout.wFormatTag		= WAVE_FORMAT_PCM;
		m_waveformout.nChannels		    = amvinfo->nChannels;
		m_waveformout.nSamplesPerSec	= amvinfo->nSamplesPerSec;
		m_waveformout.nAvgBytesPerSec   = amvinfo->nAvgBytesPerSec;
//		BUFFERSIZE=audiobuf->len;
		BUFFERSIZE=amvinfo->nAvgBytesPerSec / 12;
		m_waveformout.nBlockAlign	    = amvinfo->nBlockAlign;
		m_waveformout.wBitsPerSample	= amvinfo->wBitsPerSample;
		m_waveformout.cbSize			= amvinfo->cbSize;
		
		if(waveOutOpen(&m_hWaveOut, WAVE_MAPPER, &m_waveformout, 
			(DWORD)this->m_hWnd, NULL, CALLBACK_WINDOW))
		{
			audioopened = 0;
			MessageBeep(MB_ICONEXCLAMATION);
			AfxMessageBox("Audio output erro");
		}
		//	else
		//		audioopened = 1;
		m_bShutOff=FALSE;
	}
	
	/////////////////////////        audio        //////////////////////////////////////
	////////////////////////////               //////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
}

void CAMVDecoderDlg::OnButtonPause() 
{
	// TODO: Add your control notification handler code here
	if(isPlay == PLAY_ING)
	{
		KillTimer(0);
		m_btnPause.SetWindowText("继续");
		m_btnPlay.EnableWindow(TRUE);
		isPlay = PLAY_PAUSE;
	}
	else if(isPlay == PLAY_PAUSE)
	{
		SetTimer(0, 1000/amvinfo->dwSpeed, NULL);
		m_btnPause.SetWindowText("暂停");
		m_btnPlay.EnableWindow(FALSE);
		isPlay = PLAY_ING;
	}
	
	if(m_hWaveOut)
	{
		m_bShutOff=TRUE;
		waveOutReset(m_hWaveOut);
	}
	else
	{
		AmvAudioDecode(amvdec);
		audiobuf = &(amvdec->audiobuf);
		if(audioopened == 0)
		{
			m_waveformout.wFormatTag		= WAVE_FORMAT_PCM;
			m_waveformout.nChannels		    = amvinfo->nChannels;
			m_waveformout.nSamplesPerSec	= amvinfo->nSamplesPerSec;
			m_waveformout.nAvgBytesPerSec   = amvinfo->nAvgBytesPerSec;
//		BUFFERSIZE=audiobuf->len;
		BUFFERSIZE=amvinfo->nAvgBytesPerSec / 12;
			m_waveformout.nBlockAlign	    = amvinfo->nBlockAlign;
			m_waveformout.wBitsPerSample	= amvinfo->wBitsPerSample;
			m_waveformout.cbSize			= amvinfo->cbSize;
			
			if(waveOutOpen(&m_hWaveOut, WAVE_MAPPER, &m_waveformout, 
				(DWORD)this->m_hWnd, NULL, CALLBACK_WINDOW))
			{
				audioopened = 0;
				MessageBeep(MB_ICONEXCLAMATION);
				AfxMessageBox("Audio output erro");
			}
			//	else
			//		audioopened = 1;
			m_bShutOff=FALSE;
		}
	}
}

void CAMVDecoderDlg::OnButtonStop() 
{
	// TODO: Add your control notification handler code here
	if(isPlay == PLAY_ING || isPlay == PLAY_PAUSE)
	{
		KillTimer(0);
	//	if(isPlay == PLAY_PAUSE)
		m_btnPause.SetWindowText("暂停");
		m_btnPause.EnableWindow(FALSE);
		m_btnPlay.EnableWindow(TRUE);
		isPlay = PLAY_START;
		
		AmvRewindFrameStart(amvdec);
		// 画初始视频图像
		bufflock = 1;
		AmvReadNextFrame(amvdec);
		bufflock = 0;
		framebuf = &(amvdec->framebuf);
		AmvVideoDecode(amvdec);
		videobuf = &(amvdec->videobuf);
		
		// 获取绘制坐标的文本框
		CRect rect;
		CWnd* pWnd = GetDlgItem(IDC_STATIC_PIC);                			
		//获得对话框上的picture的窗口句柄
		pWnd->GetClientRect(&rect);
		// 指针
		pDC = pWnd->GetDC();
		
		m_BmpInfo.bmiHeader.biSize			= sizeof(BITMAPINFOHEADER);
		m_BmpInfo.bmiHeader.biWidth			= amvinfo->dwWidth;
		m_BmpInfo.bmiHeader.biHeight		= -amvinfo->dwHeight;
		m_BmpInfo.bmiHeader.biCompression	= BI_RGB;
		m_BmpInfo.bmiHeader.biPlanes		= 1;
		m_BmpInfo.bmiHeader.biBitCount		= 24;
		m_BmpInfo.bmiHeader.biSizeImage		= amvinfo->dwHeight*amvinfo->dwWidth*3;    
		
		DrawPicture(pDC, &rect, videobuf);
		
		if(m_hWaveOut)
		{
			m_bShutOff=TRUE;
			waveOutReset(m_hWaveOut);
		}
	}
	if(m_hWaveOut)
		{
			m_bShutOff=TRUE;
			waveOutReset(m_hWaveOut);
		}
}

void CAMVDecoderDlg::OnButtonGetpic() 
{
	// TODO: Add your control notification handler code here
	FRAMEBUFF fbuff;
	BYTE *buff;
	CString filepath;
	
	if(bufflock == 1)
		return;
	else
	{
		buff = new BYTE [amvdec->framebuf.videobufflen];
		memcpy(buff, amvdec->framebuf.videobuff, amvdec->framebuf.videobufflen);
		fbuff.videobuff = buff;
		fbuff.videobufflen = amvdec->framebuf.videobufflen;
	}
	
	static char BASED_CODE szFilter[] = "Jpeg Files (*.jpg)|*.jpg|All Files (*.*)|*.*||";
	
	CFileDialog dlg(FALSE, NULL, NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, szFilter, NULL);
	if(dlg.DoModal() == IDOK)
	{
		filepath = dlg.GetPathName();
		AmvCreateJpegFileFromBuffer(amvinfo, &fbuff, filepath);
	}
	
	delete [] buff;
}

void CAMVDecoderDlg::OnButtonGetwav() 
{
	// TODO: Add your control notification handler code here
	CString filepath;
	
	if(isPlay == PLAY_ING)
	{
		KillTimer(0);
	}
	
	m_btnGetWav.EnableWindow(FALSE);
	
	static char  BASED_CODE szFilter[] = "Wave Files (*.wav)|*.wav|All Files (*.*)|*.*||";
	
	CFileDialog dlg(FALSE, NULL, NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, szFilter, NULL);
	if(dlg.DoModal() == IDOK)
	{
		filepath = dlg.GetPathName();
		AmvCreateWavFileFromAmvFile(amvdec, AUDIO_FILE_TYPE_PCM, filepath);
	}
	
	m_btnGetWav.EnableWindow(TRUE);
	
	if(isPlay == PLAY_ING)
	{
		SetTimer(0, 1000/amvinfo->dwSpeed, NULL);
	}
}



void CAMVDecoderDlg::FillBuffer(char* pBuffer)
{
	AmvReadNextFrame(amvdec);
//====================================video===========================================//
		CRect rect;
	// 获取绘制坐标的文本框
		CWnd* pWnd = GetDlgItem(IDC_STATIC_PIC);                			
		//获得对话框上的picture的窗口句柄
		pWnd->GetClientRect(&rect);
		// 指针
		pDC = pWnd->GetDC();
		
		bufflock = 1;
		//AmvReadNextFrame(amvdec);
		bufflock = 0;
		framebuf = &(amvdec->framebuf);
		if(framebuf->framenum == -1)
		{
			KillTimer(0);
			m_btnPlay.SetWindowText("播放");
			m_btnPlay.EnableWindow(TRUE);
			isPlay = PLAY_START;
			AmvRewindFrameStart(amvdec);
			bufflock = 1;
			AmvReadNextFrame(amvdec);
			bufflock = 0;
			if(m_hWaveOut)
			{
				m_bShutOff=TRUE;
				waveOutReset(m_hWaveOut);
			}	
		}
		
		AmvVideoDecode(amvdec);
		videobuf = &(amvdec->videobuf);
		DrawPicture(pDC, &rect, videobuf);
		
//====================================video*******************************************//
	AmvAudioDecode(amvdec);
	audiobuf = &(amvdec->audiobuf);
	//	char ch[200];
	//	sprintf(ch,"audiobuf->len is :%d",audiobuf->len);
	//	MessageBox(ch);
	//	memcpy(pBuffer, audiobuf->audiodata, audiobuf->len);
	memcpy(pBuffer, audiobuf->audiodata, BUFFERSIZE);
	//	if(audiobuf->len!=(unsigned int)BUFFERSIZE)
	//	{
	//		char ch[200];
	//		sprintf(ch,"audiobuf->len is :%d,BUFFERSIZE is :%d",audiobuf->len,BUFFERSIZE);
	//		MessageBox(ch);
	//	}
	
}



/*void CAMVDecoderDlg::FillBuffer(CAudioData m_AudioData)
{

  AmvAudioDecode(amvdec);
  audiobuf = &(amvdec->audiobuf);
  
	m_AudioData.lpdata = (PBYTE)realloc(0, audiobuf->len);
	if(m_AudioData.lpdata != NULL)
	{
	memcpy(m_AudioData.lpdata, audiobuf->audiodata, audiobuf->len);
	m_AudioData.dwLength = audiobuf->len;
	}
	
}*/
