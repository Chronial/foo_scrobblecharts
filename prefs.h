#pragma once


// prefs-Dialogfeld

class prefs : public CDialog
{
	DECLARE_DYNAMIC(prefs)

public:
	prefs(CWnd* pParent = NULL);   // Standardkonstruktor
	virtual ~prefs();

// Dialogfelddaten
	enum { IDD = IDD_CONFIG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV-Unterstützung

	DECLARE_MESSAGE_MAP()
public:
	BOOL artistInContextmenu;
public:
	BOOL noCache;
};
