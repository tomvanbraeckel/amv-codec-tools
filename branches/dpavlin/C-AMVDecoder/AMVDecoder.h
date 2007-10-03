// AMVDecoder.h : main header file for the AMVDECODER application
//

#if !defined(AFX_AMVDECODER_H__9EFE1BCC_D3FC_4EAF_93E7_767871D722B1__INCLUDED_)
#define AFX_AMVDECODER_H__9EFE1BCC_D3FC_4EAF_93E7_767871D722B1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CAMVDecoderApp:
// See AMVDecoder.cpp for the implementation of this class
//

class CAMVDecoderApp : public CWinApp
{
public:
	CAMVDecoderApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAMVDecoderApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CAMVDecoderApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_AMVDECODER_H__9EFE1BCC_D3FC_4EAF_93E7_767871D722B1__INCLUDED_)
