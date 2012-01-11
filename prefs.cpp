// prefs.cpp: Implementierungsdatei
//

#include "resource.h"
#include "stdafx.h"
#include "prefs.h"
#include "../_sdk/foobar2000/SDK/foobar2000.h"

// {25244A12-02DB-45a0-AE99-E1CE480EC548}
static const GUID guid_config = 
{ 0x25244a12, 0x2db, 0x45a0, { 0xae, 0x99, 0xe1, 0xce, 0x48, 0xe, 0xc5, 0x48 } };


class config_input : public preferences_page
{
public:
	HWND create(HWND parent){
		CWnd * pWnd = new CWnd;
		//pWnd->Attach(parent);
		//pWnd->Detach();
		prefs * prefWindow = new prefs(pWnd);
		prefWindow->Create(prefs::IDD,pWnd);
		return prefWindow->m_hWnd;
		//return uCreateDialog(IDD_CONFIG,parent,WndPr Proc);
	}

	GUID get_guid(){
		return guid_config;
	}
	bool preferences_page::reset_query(void){
		return false;
	}
	void preferences_page::reset(void) {}

	const char * get_name() {return "foo_scrobblecharts";}
	GUID get_parent_guid() {return preferences_page::guid_tools; }
};

//static service_factory_t<config,config_input> foo;
static preferences_page_factory_t <config_input> foo;


// prefs-Dialogfeld

IMPLEMENT_DYNAMIC(prefs, CDialog)

prefs::prefs(CWnd* pParent /*=NULL*/)
	: CDialog(prefs::IDD, pParent)
	, artistInContextmenu(FALSE)
	, noCache(FALSE)
{

}

prefs::~prefs()
{
}

void prefs::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_CONTEXT_ARTIST, artistInContextmenu);
	DDX_Check(pDX, IDC_NO_CACHE, noCache);
}


BEGIN_MESSAGE_MAP(prefs, CDialog)
END_MESSAGE_MAP()


// prefs-Meldungshandler

