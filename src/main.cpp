// chrlauncher
// Copyright (c) 2015 Henry++

#include <windows.h>

#include "main.h"
#include "application.h"
#include "routine.h"

#include "resource.h"

#include "unzip.h"

#define BUFFER 16384

NOTIFYICONDATA nid = {0};

CApplication app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_AUTHOR, APP_COPYRIGHT);

#define WOOLYSS_API_VERSION L"http://chromium.woolyss.com/api/?os=windows&bit=%d&out=string"

BOOL is_busy = FALSE;

DWORD architecture = 0;
WCHAR current_version[100] = {0};

CString bin_directory = app.GetDirectory () + L"\\bin";
CString update_directory = app.GetDirectory () + L"\\updates";

CString chrome_path = bin_directory + L"\\chrome.exe";

VOID _Application_SetPercent (INT percent)
{
	SendDlgItemMessage (app.GetHWND (), IDC_PROGRESS, PBM_SETPOS, percent, 0);

	_r_status_settext (app.GetHWND (), IDC_STATUSBAR, 1, _r_fmt (L"%i%%", percent));
}

VOID _Application_SetStatus (LPCWSTR text)
{
	_r_status_settext (app.GetHWND (), IDC_STATUSBAR, 0, text);
}

BOOL _Chromium_InstallUpdate (LPCWSTR path)
{
	BOOL result = FALSE;

	is_busy = TRUE;

	ZIPENTRY ze = {0};

	HZIP hz = OpenZip (path, nullptr);

	_Application_SetStatus (I18N (&app, IDS_STATUS_INSTALL, 0));
	_Application_SetPercent (0);

	if (IsZipHandleU (hz))
	{
		// remove directory
		SHFILEOPSTRUCT op = {0};

		op.wFunc = FO_DELETE;
		op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_NO_UI;
		op.pFrom = bin_directory;

		SHFileOperation (&op);

		// create new directory
		SHCreateDirectoryEx (nullptr, bin_directory, nullptr);

		DWORD total_size = 0;
		DWORD total_progress = 0;
		DWORD count_all = 0; // this is our progress so far

		// count total files
		GetZipItem (hz, -1, &ze);
		INT total_files = ze.index;

		// check archive is Chromium package
		GetZipItem (hz, 0, &ze);

		if (wcsncmp (ze.name, L"chrome-win32", 12) == 0)
		{
			// count size of unpacked files
			for (INT i = 0; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				total_size += ze.unc_size;
			}

			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				CString name = ze.name;
				name = name.Mid (name.Find (L"/"));

				CString file = _r_fmt (L"%s\\%s", bin_directory, name);

				if ((ze.attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					SHCreateDirectoryEx (nullptr, file, nullptr);
				}
				else
				{
					HANDLE f = CreateFile (file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

					if (f != INVALID_HANDLE_VALUE)
					{
						CHAR* buff = new char[BUFFER];
						DWORD count_file = 0;

						for (ZRESULT zr = ZR_MORE; zr == ZR_MORE;)
						{
							zr = UnzipItem (hz, i, buff, BUFFER);

							ULONG bufsize = BUFFER;

							if (zr == ZR_OK)
							{
								bufsize = ze.unc_size - count_file;
							}

							DWORD written = 0;
							WriteFile (f, buff, bufsize, &written, nullptr);

							count_file += bufsize;
							count_all += bufsize;

							_Application_SetPercent ((double (count_all) / double (total_size)) * 100.0);
						}

						delete[] buff;

						CloseHandle (f);
					}
				}
			}

			result = TRUE;
		}

		CloseZip (hz);

		if (result)
		{
			DeleteFile (path);
			RemoveDirectory (update_directory);
		}
	}

	is_busy = FALSE;

	return result;
}

BOOL _Chromium_DownloadUpdate (LPCWSTR url, LPCWSTR version)
{
	HINTERNET internet = nullptr;
	HINTERNET connect = nullptr;

	// if file already downloaded
	CString path;
	path.Format (L"%s\\update_%s.zip", update_directory, version);

	if (_r_file_is_exists (path))
	{
		if (_Chromium_InstallUpdate (path))
		{
			return TRUE;
		}
	}

	// download file
	_Application_SetStatus (I18N (&app, IDS_STATUS_DOWNLOAD, 0));
	_Application_SetPercent (0);

	is_busy = TRUE;

	internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);

	if (internet)
	{
		connect = InternetOpenUrl (internet, url, nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

		if (connect)
		{
			DWORD status = 0, size = sizeof (status);
			HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

			if (status == HTTP_STATUS_OK)
			{
				DWORD total_size = 0;

				size = sizeof (total_size);

				HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &total_size, &size, nullptr);

				DWORD out = 0;
				DWORD total_written = 0;

				CHAR* buff = new CHAR[BUFFER];

				SHCreateDirectoryEx (nullptr, update_directory, nullptr);
				HANDLE f = CreateFile (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (f != INVALID_HANDLE_VALUE)
				{
					while (1)
					{
						if (!InternetReadFile (connect, buff, BUFFER, &out) || !out)
						{
							break;
						}

						DWORD written = 0;
						WriteFile (f, buff, out, &written, nullptr);

						total_written += out;

						_Application_SetPercent ((double (total_written) / double (total_size)) * 100.0);
					}

					CloseHandle (f);
				}

				delete[] buff;

				is_busy = FALSE;

				if (_Chromium_InstallUpdate (path))
				{
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

UINT WINAPI _Chromium_CheckUpdate (LPVOID)
{
	HINTERNET internet = nullptr, connect = nullptr;

	CStringMap result;

	DWORD days = app.ConfigGet (L"ChromiumCheckDays", 1);
	BOOL is_exists = _r_file_is_exists (chrome_path);
	BOOL is_success = FALSE;

	if (days || !is_exists)
	{
		if (_r_unixtime_now () - app.ConfigGet (L"ChromiumLastChecking", 0) >= (86400 * days) || !is_exists)
		{
			internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);

			if (internet)
			{
				connect = InternetOpenUrl (internet, _r_fmt (WOOLYSS_API_VERSION, architecture), nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

				if (connect)
				{
					DWORD dwStatus = 0, dwStatusSize = sizeof (dwStatus);
					HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &dwStatus, &dwStatusSize, nullptr);

					if (dwStatus == HTTP_STATUS_OK)
					{
						DWORD out = 0;

						CStringA buffera;
						CStringW bufferw;

						InternetReadFile (connect, buffera.GetBuffer (1024), 1024, &out);
						buffera.ReleaseBuffer ();

						if (out)
						{
							bufferw = CA2W (buffera, CP_UTF8);
							bufferw = bufferw.Trim (L" \r\n"); // trim whitespaces

							INT pos = 0, div = 0;
							CString token = bufferw.Tokenize (L";", pos);
							CString key, val;

							while (!token.IsEmpty ())
							{
								token = token.Trim (L" \r\n");

								div = token.Find (L":");

								key = token.Mid (0, div);
								val = token.Mid (div + 1).Trim (L" \r\n");

								result[key] = val;

								token = bufferw.Tokenize (L";", pos);
							}

							is_success = TRUE;
						}
					}

					app.ConfigSet (L"ChromiumLastChecking", DWORD (_r_unixtime_now ()));
				}
			}

			InternetCloseHandle (connect);
			InternetCloseHandle (internet);
		}

		if (is_success)
		{
			SetDlgItemText (app.GetHWND (), IDC_VERSION, _r_fmt (I18N (&app, IDS_VERSION, 0), result[L"version"], result[L"architecture"]));
			SetDlgItemText (app.GetHWND (), IDC_REVISION, _r_fmt (I18N (&app, IDS_REVISION, 0), result[L"revision"]));
			SetDlgItemText (app.GetHWND (), IDC_BUILD, _r_fmt (I18N (&app, IDS_BUILD, 0), result[L"build"]));
			SetDlgItemText (app.GetHWND (), IDC_COMMIT, _r_fmt (I18N (&app, IDS_COMMIT, 0), result[L"commit"], result[L"commit"]));
			SetDlgItemText (app.GetHWND (), IDC_DATE, _r_fmt (I18N (&app, IDS_DATE, 0), _r_fmt_date (wcstoull (result[L"timestamp"], nullptr, 10), FDTF_SHORTDATE)));

			// check for new version
			if (wcsncmp (current_version, result[L"version"], result[L"version"].GetLength ()) != 0)
			{
				_r_windowtoggle (app.GetHWND (), TRUE);

				CString url = _r_fmt (L"https://storage.googleapis.com/chromium-browser-continuous/%s/%s/chrome-win32.zip", result[L"architecture"] == L"32-bit" ? L"Win" : L"Win_x64", result[L"revision"]);

				_Chromium_DownloadUpdate (url, result[L"version"]);
			}
		}
	}

	PostMessage (app.GetHWND (), WM_DESTROY, 0, 0);

	return ERROR_SUCCESS;
}

CString _Chromium_GetVersion ()
{
	CString result;

	DWORD verHandle = 0;
	DWORD verSize = GetFileVersionInfoSize (chrome_path, &verHandle);

	if (verSize)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo (chrome_path, verHandle, verSize, verData))
		{
			LPBYTE buffer = nullptr;
			UINT size = 0;

			if (VerQueryValue (verData, L"\\", (VOID FAR* FAR*)&buffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO *verInfo = (VS_FIXEDFILEINFO*)buffer;

					if (verInfo->dwSignature == 0xfeef04bd)
					{
						// Doesn't matter if you are on 32 bit or 64 bit,
						// DWORD is always 32 bits, so first two revision numbers
						// come from dwFileVersionMS, last two come from dwFileVersionLS

						result.Format (L"%d.%d.%d.%d", (verInfo->dwFileVersionMS >> 16) & 0xffff, (verInfo->dwFileVersionMS >> 0) & 0xffff, (verInfo->dwFileVersionLS >> 16) & 0xffff, (verInfo->dwFileVersionLS >> 0) & 0xffff);
					}
				}
			}
		}

		delete[] verData;
	}

	return result;
}

BOOL _Chromium_Run ()
{
	return _r_run (nullptr, _r_fmt (L"\"%s\" %s", chrome_path, app.ConfigGet (L"ChromiumCommandLine", L"--user-data-dir=..\\profile --no-default-browser-check")).GetBuffer ());
}

LRESULT CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			app.SetHWND (hwnd);

			INT parts[] = {_r_window_dpi (224), -1};
			SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

			// get architecture...
			architecture = app.ConfigGet (L"ChromiumArchitecture", 0);

			if (!architecture || (architecture != 64 && architecture != 32))
			{
				architecture = 0;

				// on XP only 32-bit Chromium supported
				if (_r_system_validversion (5, 1, VER_EQUAL))
				{
					architecture = 32;
				}

				// ...by executable
				DWORD exe_type = 0;

				if (GetBinaryType (chrome_path, &exe_type))
				{
					if (exe_type == SCS_32BIT_BINARY)
					{
						architecture = 32;
					}
					else if (exe_type == SCS_64BIT_BINARY)
					{
						architecture = 64;
					}
				}

				if (!architecture)
				{
					// ...by processor architecture
					SYSTEM_INFO si = {0};
					GetNativeSystemInfo (&si);

					if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
					{
						architecture = 32;
					}
					else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
					{
						architecture = 64;
					}
				}
			}

			// get current version
			StringCchCopy (current_version, _countof (current_version), _Chromium_GetVersion ());

			// start update checking
			_beginthreadex (nullptr, 0, &_Chromium_CheckUpdate, nullptr, 0, nullptr);

			break;
		}

		case WM_CLOSE:
		{
			if (is_busy && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, I18N (&app, IDS_QUESTION_BUSY, 0)) == IDNO)
			{
				return TRUE;
			}

			break;
		}

		case WM_DESTROY:
		{
			_Chromium_Run ();
			PostQuitMessage (0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_LBUTTONDOWN:
		{
			SendMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, NULL);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) != WS_EX_LAYERED)
			{
				SetWindowLongPtr (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
			}

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (nullptr, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			switch (LPNMHDR (lparam)->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					ShellExecute (nullptr, nullptr, PNMLINK (lparam)->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDM_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, APP_WEBSITE, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_ABOUT:
				{
					MSGBOXPARAMS mbp = {0};

					mbp.cbSize = sizeof (mbp);
					mbp.dwStyle = MB_OK | MB_USERICON;
					mbp.lpszIcon = MAKEINTRESOURCE (IDI_MAIN);
					mbp.hwndOwner = hwnd;
					mbp.hInstance = GetModuleHandle (nullptr);
					mbp.lpszCaption = I18N (&app, IDS_ABOUT, 0).GetString ();
					mbp.lpszText = _r_fmt (L"%s %s\r\n%s", APP_NAME, APP_VERSION, APP_COPYRIGHT).GetString ();

					MessageBoxIndirect (&mbp);

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow ((DLGPROC)DlgProc))
	{
		MSG msg = {0};

		while (GetMessage (&msg, nullptr, 0, 0))
		{
			if (!IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
	}

	return ERROR_SUCCESS;
}
