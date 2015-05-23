/** 
 * @file  SourceControl.cpp
 *
 * @brief Implementation file for some source control-related functions.
 */

#include "StdAfx.h"
#define POCO_NO_UNWINDOWS 1
#include <Poco/Process.h>
#include <Poco/Format.h>
#include <direct.h>
#include "UnicodeString.h"
#include "unicoder.h"
#include "Merge.h"
#include "OptionsDef.h"
#include "OptionsMgr.h"
#include "RegKey.h"
#include "paths.h"
#include "VssPromptDlg.h"
#include "ssapi.h"      // BSP - Includes for Visual Source Safe COM interface
#include "CCPromptDlg.h"
#include "VSSHelper.h"

using Poco::format;
using Poco::Process;
using Poco::ProcessHandle;

void
CMergeApp::InitializeSourceControlMembers()
{
	m_pVssHelper->SetProjectBase((const TCHAR *)theApp.GetProfileString(_T("Settings"), _T("VssProject"), _T("")));
	m_strVssUser = theApp.GetProfileString(_T("Settings"), _T("VssUser"), _T(""));
//	m_strVssPassword = theApp.GetProfileString(_T("Settings"), _T("VssPassword"), _T(""));
	theApp.WriteProfileString(_T("Settings"), _T("VssPassword"), _T(""));
	m_strVssDatabase = theApp.GetProfileString(_T("Settings"), _T("VssDatabase"),_T(""));
	m_strCCComment = _T("");
	m_bCheckinVCS = FALSE;

	String vssPath = GetOptionsMgr()->GetString(OPT_VSS_PATH);
	if (vssPath.empty())
	{
		CRegKeyEx reg;
		if (reg.QueryRegMachine(_T("SOFTWARE\\Microsoft\\SourceSafe")))
		{
			TCHAR temp[_MAX_PATH] = {0};
			reg.ReadChars(_T("SCCServerPath"), temp, _MAX_PATH, _T(""));
			vssPath = paths_ConcatPath(paths_GetPathOnly(temp), _T("Ss.exe"));
			GetOptionsMgr()->SaveOption(OPT_VSS_PATH, vssPath);
		}
	}
}

/**
* @brief Saves file to selected version control system
* @param strSavePath Path where to save including filename
* @return Tells if caller can continue (no errors happened)
* @sa CheckSavePath()
*/
BOOL CMergeApp::SaveToVersionControl(const String& strSavePath)
{
	CFileStatus status;
	INT_PTR userChoice = 0;
	int nVerSys = 0;

	nVerSys = GetOptionsMgr()->GetInt(OPT_VCS_SYSTEM);

	switch(nVerSys)
	{
	case VCS_NONE:	//no versioning system
		// Already handled in CheckSavePath()
		break;
	case VCS_VSS4:	// Visual Source Safe
	{
		// Prompt for user choice
		CVssPromptDlg dlg;
		dlg.m_strMessage = LangFormatString1(IDS_SAVE_FMT, strSavePath.c_str()).c_str();
		dlg.m_strProject = m_pVssHelper->GetProjectBase().c_str();
		dlg.m_strUser = m_strVssUser;          // BSP - Add VSS user name to dialog box
		dlg.m_strPassword = m_strVssPassword;

		// Dialog not suppressed - show it and allow user to select "checkout all"
		if (!m_CheckOutMulti)
		{
			dlg.m_bMultiCheckouts = FALSE;
			userChoice = dlg.DoModal();
			m_CheckOutMulti = dlg.m_bMultiCheckouts;
		}
		else // Dialog already shown and user selected to "checkout all"
			userChoice = IDOK;

		// process versioning system specific action
		if (userChoice == IDOK)
		{
			CWaitCursor waitstatus;
			m_pVssHelper->SetProjectBase((const TCHAR *)dlg.m_strProject);
			theApp.WriteProfileString(_T("Settings"), _T("VssProject"), m_pVssHelper->GetProjectBase().c_str());
			String path, name;
			paths_SplitFilename(strSavePath, &path, &name, NULL);
			String spath(path);
			String sname(path);
			if (!spath.empty())
			{
				_chdrive(_totupper(spath[0]) - 'A' + 1);
				_tchdir(spath.c_str());
			}
			try
			{
				std::string vssPath = ucr::toUTF8(GetOptionsMgr()->GetString(OPT_VSS_PATH));
				std::string sn;
				std::vector<std::string> args;
				args.push_back("checkout");
				format(sn, "\"%s/%s\"", ucr::toUTF8(m_pVssHelper->GetProjectBase()), ucr::toUTF8(sname));
				args.push_back(sn);
				ProcessHandle hVss(Process::launch(vssPath, args));
				int code = Process::wait(hVss);
				if (code != 0)
				{
					LangMessageBox(IDS_VSSERROR, MB_ICONSTOP);
					return FALSE;
				}
			}
			catch (...)
			{
				LangMessageBox(IDS_VSS_RUN_ERROR, MB_ICONSTOP);
				return FALSE;
			}
		}
		else
			return FALSE; // User selected cancel
	}
	break;
	case VCS_VSS5: // CVisual SourceSafe 5.0+ (COM)
	{
		// prompt for user choice
		CVssPromptDlg dlg;
		CRegKeyEx reg;
		CString spath, sname;

		dlg.m_strMessage = LangFormatString1(IDS_SAVE_FMT, strSavePath.c_str()).c_str();
		dlg.m_strProject = m_pVssHelper->GetProjectBase().c_str();
		dlg.m_strUser = m_strVssUser;          // BSP - Add VSS user name to dialog box
		dlg.m_strPassword = m_strVssPassword;
		dlg.m_strSelectedDatabase = m_strVssDatabase;
		dlg.m_bVCProjSync = TRUE;

		// Dialog not suppressed - show it and allow user to select "checkout all"
		if (!m_CheckOutMulti)
		{
			dlg.m_bMultiCheckouts = FALSE;
			userChoice = dlg.DoModal();
			m_CheckOutMulti = dlg.m_bMultiCheckouts;
		}
		else // Dialog already shown and user selected to "checkout all"
			userChoice = IDOK;

		// process versioning system specific action
		if (userChoice == IDOK)
		{
			CWaitCursor waitstatus;
			BOOL bOpened = FALSE;
			m_pVssHelper->SetProjectBase((const TCHAR *)dlg.m_strProject);
			m_strVssUser = dlg.m_strUser;
			m_strVssPassword = dlg.m_strPassword;
			m_strVssDatabase = dlg.m_strSelectedDatabase;
			m_bVCProjSync = dlg.m_bVCProjSync;					

			theApp.WriteProfileString(_T("Settings"), _T("VssDatabase"), m_strVssDatabase);
			theApp.WriteProfileString(_T("Settings"), _T("VssProject"), m_pVssHelper->GetProjectBase().c_str());
			theApp.WriteProfileString(_T("Settings"), _T("VssUser"), m_strVssUser);
//			theApp.WriteProfileString(_T("Settings"), _T("VssPassword"), m_strVssPassword);

			IVSSDatabase vssdb;
			IVSSItems vssis;
			IVSSItem vssi;

			COleException *eOleException = new COleException;
				
			// BSP - Create the COM interface pointer to VSS
			if (!vssdb.CreateDispatch(_T("SourceSafe"), eOleException))
			{
				throw eOleException;	// catch block deletes.
			}
			else
			{
				eOleException->Delete();
			}

			//check if m_strVSSDatabase is specified:
			if (!m_strVssDatabase.IsEmpty())
			{
				CString iniPath = m_strVssDatabase + _T("\\srcsafe.ini");
				TRY
				{
					// BSP - Open the specific VSS data file  using info from VSS dialog box
					vssdb.Open(iniPath, m_strVssUser, m_strVssPassword);
				}
				CATCH_ALL(e)
				{
					ShowVSSError(e, _T(""));
				}
				END_CATCH_ALL

				bOpened = TRUE;
			}
			
			if (bOpened == FALSE)
			{
				CString iniPath = m_strVssDatabase + _T("\\srcsafe.ini");
				TRY
				{
					// BSP - Open the specific VSS data file  using info from VSS dialog box
					//let vss try to find one if not specified
					vssdb.Open(NULL, m_strVssUser, m_strVssPassword);
				}
				CATCH_ALL(e)
				{
					ShowVSSError(e, _T(""));
					return FALSE;
				}
				END_CATCH_ALL
			}

			String path, name;
			paths_SplitFilename(strSavePath, &path, &name, 0);
			spath = path.c_str();
			sname = name.c_str();

			// BSP - Combine the project entered on the dialog box with the file name...
			const UINT nBufferSize = 1024;
			static TCHAR buffer[nBufferSize];
			static TCHAR buffer1[nBufferSize];
			static TCHAR buffer2[nBufferSize];

			_tcscpy(buffer1, strSavePath.c_str());
			_tcscpy(buffer2, m_pVssHelper->GetProjectBase().c_str());
			_tcslwr(buffer1);
			_tcslwr(buffer2);

			//make sure they both have \\ instead of /
			for (int k = 0; k < nBufferSize; k++)
			{
				if (buffer1[k] == '/')
					buffer1[k] = '\\';
			}

			m_pVssHelper->SetProjectBase(buffer2);
			TCHAR * pbuf2 = &buffer2[2];//skip the $/
			TCHAR * pdest =  _tcsstr(buffer1, pbuf2);
			if (pdest)
			{
				int index  = (int)(pdest - buffer1 + 1);
			
				_tcscpy(buffer, buffer1);
				TCHAR * fp = &buffer[int(index + _tcslen(pbuf2))];
				sname = fp;

				if (sname[0] == ':')
				{
					_tcscpy(buffer2, sname);
					_tcscpy(buffer, (TCHAR*)&buffer2[2]);
					sname = buffer;
				}
			}
			String strItem = m_pVssHelper->GetProjectBase() + _T("\\") + static_cast<const TCHAR *>(sname);

			TRY
			{
				//  BSP - ...to get the specific source safe item to be checked out
				vssi = vssdb.GetVSSItem( strItem.c_str(), 0 );
			}
			CATCH_ALL(e)
			{
				ShowVSSError(e, strItem);
				return FALSE;
			}
			END_CATCH_ALL

			if (!m_bVssSuppressPathCheck)
			{
				// BSP - Get the working directory where VSS will put the file...
				String strLocalSpec = vssi.GetLocalSpec();

				// BSP - ...and compare it to the directory WinMerge is using.
				if (string_compare_nocase(strLocalSpec, strSavePath))
				{
					// BSP - if the directories are different, let the user confirm the CheckOut
					int iRes = LangMessageBox(IDS_VSSFOLDER_AND_FILE_NOMATCH, 
							MB_YESNO | MB_YES_TO_ALL | MB_ICONWARNING);

					if (iRes == IDNO)
					{
						m_bVssSuppressPathCheck = FALSE;
						m_CheckOutMulti = FALSE; // Reset, we don't want 100 of the same errors
						return FALSE;   // No means user has to start from begin
					}
					else if (iRes == IDYESTOALL)
						m_bVssSuppressPathCheck = TRUE; // Don't ask again with selected files
				}
			}

			TRY
			{
				// BSP - Finally! Check out the file!
				vssi.Checkout(_T(""), strSavePath.c_str(), 0);
			}
			CATCH_ALL(e)
			{
				ShowVSSError(e, strSavePath);
				return FALSE;
			}
			END_CATCH_ALL
		}
		else
			return FALSE; // User selected cancel
	}
	break;
	case VCS_CLEARCASE:
	{
		// prompt for user choice
		CCCPromptDlg dlg;
		if (!m_CheckOutMulti)
		{
			dlg.m_bMultiCheckouts = FALSE;
			dlg.m_comments = _T("");
			dlg.m_bCheckin = FALSE;
			userChoice = dlg.DoModal();
			m_CheckOutMulti = dlg.m_bMultiCheckouts;
			m_strCCComment = dlg.m_comments;
			m_bCheckinVCS = dlg.m_bCheckin;
		}
		else // Dialog already shown and user selected to "checkout all"
			userChoice = IDOK;

		// process versioning system specific action
		if (userChoice == IDOK)
		{
			CWaitCursor waitstatus;
			String path, name;
			paths_SplitFilename(strSavePath, &path, &name, 0);
			String spath(path);
			String sname(name);
			if (!spath.empty())
			{
				_chdrive(_totupper(spath[0])-'A'+1);
				_tchdir(spath.c_str());
			}

			// checkout operation
			std::string vssPath = ucr::toUTF8(GetOptionsMgr()->GetString(OPT_VSS_PATH));
			std::string sn, sn2;
			std::vector<std::string> args;
			args.push_back("checkout");
			args.push_back("-c");
			format(sn, "\"%s\"", ucr::toUTF8((LPCTSTR)m_strCCComment));
			args.push_back(sn);
			format(sn2, "\"%s\"", ucr::toUTF8(sname));
			args.push_back(sn2);
			try
			{
				ProcessHandle hVss(Process::launch(vssPath, args));
				int code = Process::wait(hVss);
				if (code != 0)
				{
					LangMessageBox(IDS_VSSERROR, MB_ICONSTOP);
					return FALSE;
				}
			}
			catch (...)
			{
				LangMessageBox(IDS_VSS_RUN_ERROR, MB_ICONSTOP);
				return FALSE;
			}
		}
	}
	break;
	}	//switch(m_nVerSys)

	return TRUE;
}

/**
 * @brief Checkin in file into ClearCase.
 */ 
void CMergeApp::CheckinToClearCase(const String &strDestinationPath)
{
	String spath, sname;
	paths_SplitFilename(strDestinationPath, &spath, &sname, 0);
	int code;
	std::vector<std::string> args;
	std::string sname_utf8;
	
	// checkin operation
	args.push_back("checkin");
	args.push_back("-nc");
	format(sname_utf8, "\"%s\"", ucr::toUTF8(sname));
	args.push_back(sname_utf8);
	std::string vssPath = ucr::toUTF8(GetOptionsMgr()->GetString(OPT_VSS_PATH));
	try
	{
		ProcessHandle hVss(Process::launch(vssPath, args));
		code = Process::wait(hVss);
		if (code != 0)
		{
			if (LangMessageBox(IDS_VSS_CHECKINERROR, MB_ICONWARNING | MB_YESNO) == IDYES)
			{
				// undo checkout operation
				args.push_back("uncheckout");
				args.push_back("-rm");
				format(sname_utf8, "\"%s\"", ucr::toUTF8(sname));
				args.push_back(sname_utf8);
				ProcessHandle hVss(Process::launch(vssPath, args));
				code = Process::wait(hVss);
				if (code != 0)
				{
					LangMessageBox(IDS_VSS_UNCOERROR, MB_ICONSTOP);
					return;
				}
			}
			return;
		}
	}
	catch (...)
	{
		LangMessageBox(IDS_VSS_RUN_ERROR, MB_ICONSTOP);
		return;
	}
}