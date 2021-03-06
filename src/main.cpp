// chrlauncher
// Copyright (c) 2016 Henry++

#include <windows.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"
#include "unzip.h"

#include "resource.h"

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

#define CHROMIUM_UPDATE_URL L"http://chromium.woolyss.com/api/v3/?os=windows&bit=%d&type=%s&out=string"

rstring browser_name;
rstring browser_name_full;
rstring browser_directory;
rstring browser_type;
rstring browser_exe;
rstring browser_args;
rstring browser_version;

INT browser_architecture = 0;

VOID _app_setpercent (DWORD v, DWORD t)
{
	UINT percent = 0;

	if (t)
	{
		percent = static_cast<UINT>((double (v) / double (t)) * 100.0);
	}

	SendDlgItemMessage (app.GetHWND (), IDC_PROGRESS, PBM_SETPOS, percent, 0);

	_r_status_settext (app.GetHWND (), IDC_STATUSBAR, 1, _r_fmt (L"%s/%s", _r_fmt_size64 (v), _r_fmt_size64 (t)));
}

VOID _app_setstatus (LPCWSTR text)
{
	_r_status_settext (app.GetHWND (), IDC_STATUSBAR, 0, text);
}

BOOL _app_installzip (LPCWSTR path)
{
	BOOL result = FALSE;
	ZIPENTRY ze = {0};
	const size_t title_length = wcslen (L"chrome-win32");

	HZIP hz = OpenZip (path, nullptr);

	if (IsZipHandleU (hz))
	{
		// count total files
		GetZipItem (hz, -1, &ze);
		INT total_files = ze.index;

		// check archive is right package
		GetZipItem (hz, 0, &ze);

		if (wcsncmp (ze.name, L"chrome-win32", title_length) == 0)
		{
			DWORD total_size = 0;
			DWORD total_read = 0; // this is our progress so far

			// count size of unpacked files
			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				total_size += ze.unc_size;
			}

			rstring fpath;
			rstring fname;

			CHAR buffer[_R_BUFFER_LENGTH] = {0};

			DWORD written = 0;

			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				fname = ze.name + title_length + 1;
				fpath.Format (L"%s\\%s", browser_directory, fname);

				_app_setpercent (total_read, total_size);

				if ((ze.attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					_r_fs_mkdir (fpath);
				}
				else
				{
					HANDLE h = CreateFile (fpath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

					if (h != INVALID_HANDLE_VALUE)
					{
						DWORD total_read_file = 0;

						for (ZRESULT zr = ZR_MORE; zr == ZR_MORE;)
						{
							DWORD bufsize = _R_BUFFER_LENGTH;

							zr = UnzipItem (hz, static_cast<int>(i), buffer, bufsize);

							if (zr == ZR_OK)
							{
								bufsize = ze.unc_size - total_read_file;
							}

							buffer[bufsize] = 0;

							WriteFile (h, buffer, bufsize, &written, nullptr);

							total_read_file += bufsize;
							total_read += bufsize;
						}

						CloseHandle (h);
					}
				}
			}

			result = TRUE;
		}

		CloseZip (hz);
	}

	return result;
}

VOID _app_cleanup (LPCWSTR version)
{
	WIN32_FIND_DATA wfd = {0};
	HANDLE h = FindFirstFile (_r_fmt (L"%s\\*.manifest", browser_directory), &wfd);

	if (h != INVALID_HANDLE_VALUE)
	{
		size_t len = wcslen (version);

		do
		{
			if (_wcsnicmp (version, wfd.cFileName, len) != 0)
				DeleteFile (_r_fmt (L"%s\\%s", browser_directory, wfd.cFileName));
		}
		while (FindNextFile (h, &wfd));

		FindClose (h);
	}

	_r_fs_rmdir (_r_fmt (L"%s\\VisualElements", browser_directory));
}

BOOL _app_installupdate (LPCWSTR path)
{
	BOOL result = FALSE;
	BOOL is_ready = TRUE;

	// installing update
	_app_setstatus (I18N (&app, IDS_STATUS_INSTALL, 0));
	_app_setpercent (0, 0);

	// check install folder for running processes
	while (1)
	{
		is_ready = !_r_process_is_exists (browser_directory, browser_directory.GetLength ());

		if (is_ready || _r_msg (app.GetHWND (), MB_RETRYCANCEL | MB_ICONEXCLAMATION, APP_NAME, I18N (&app, IDS_STATUS_CLOSEBROWSER, 0)) != IDRETRY)
		{
			break;
		}
	}

	// create directory
	if (is_ready)
	{
		if (!_r_fs_exists (browser_directory))
			_r_fs_mkdir (browser_directory);

		result = _app_installzip (path);

		DeleteFile (path);
	}

	return result;
}

BOOL _app_downloadupdate (HINTERNET internet, LPCWSTR url, LPCWSTR path)
{
	BOOL result = FALSE;
	HINTERNET connect = nullptr;

	// download file
	_app_setstatus (I18N (&app, IDS_STATUS_DOWNLOAD, 0));
	_app_setpercent (0, 0);

	if (internet && url)
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

				HANDLE f = CreateFile (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

				if (f != INVALID_HANDLE_VALUE)
				{
					CHAR buffera[_R_BUFFER_LENGTH] = {0};

					DWORD out = 0, written = 0, total_written = 0;

					while (1)
					{
						if (!InternetReadFile (connect, buffera, _R_BUFFER_LENGTH - 1, &out) || !out)
							break;

						buffera[out] = 0;
						WriteFile (f, buffera, out, &written, nullptr);

						total_written += out;

						_app_setpercent (total_written, total_size);
					}

					result = (total_size == total_written);

					CloseHandle (f);
				}
			}

			InternetCloseHandle (connect);
		}
	}

	return result;
}

UINT WINAPI _app_checkupdate (LPVOID)
{
	HINTERNET internet = nullptr;
	HINTERNET connect = nullptr;

	rstring::map_one result;

	UINT days = app.ConfigGet (L"ChromiumCheckPeriod", 1).AsUint ();
	BOOL is_exists = _r_fs_exists (browser_exe);
	BOOL is_success = FALSE;

	if ((days || !is_exists) && !_r_process_is_exists (browser_directory, browser_directory.GetLength ()))
	{
		if (!is_exists || (_r_unixtime_now () - app.ConfigGet (L"ChromiumCheckPeriodLast", 0).AsLonglong ()) >= (86400 * days))
		{
			internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

			if (internet)
			{
				connect = InternetOpenUrl (internet, _r_fmt (CHROMIUM_UPDATE_URL, browser_architecture, browser_type), nullptr, 0, INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES, 0);

				if (connect)
				{
					DWORD status = 0, size = sizeof (status);
					HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

					if (status == HTTP_STATUS_OK)
					{
						DWORD out = 0;

						CHAR buffera[_R_BUFFER_LENGTH] = {0};
						rstring bufferw;

						while (1)
						{
							if (!InternetReadFile (connect, buffera, _R_BUFFER_LENGTH - 1, &out) || !out)
								break;

							buffera[out] = 0;
							bufferw.Append (buffera);
						}

						if (!bufferw.IsEmpty ())
						{
							rstring::rvector vc = bufferw.AsVector (L";");

							for (size_t i = 0; i < vc.size (); i++)
							{
								size_t pos = vc.at (i).Find (L'=');

								if (pos != rstring::npos)
								{
									result[vc.at (i).Midded (0, pos)] = vc.at (i).Midded (pos + 1);
								}
							}

							result[L"timestamp"] = _r_fmt_date (result[L"timestamp"].AsLonglong (), FDTF_SHORTDATE | FDTF_SHORTTIME);
						}
					}

					InternetCloseHandle (connect);
				}
			}

			if (result.size ())
			{
				if (!is_exists || result[L"version"].CompareNoCase (browser_version) != 0)
				{
					// get path
					WCHAR path[MAX_PATH] = {0};

					GetTempPath (MAX_PATH, path);
					GetTempFileName (path, nullptr, 0, path);

					// show info
					SetDlgItemText (app.GetHWND (), IDC_BROWSER, _r_fmt (I18N (&app, IDS_BROWSER, 0), browser_name_full));
					SetDlgItemText (app.GetHWND (), IDC_CURRENTVERSION, _r_fmt (I18N (&app, IDS_CURRENTVERSION, 0), browser_version.IsEmpty () ? L"<not found>" : browser_version));
					SetDlgItemText (app.GetHWND (), IDC_VERSION, _r_fmt (I18N (&app, IDS_VERSION, 0), result[L"version"]));
					SetDlgItemText (app.GetHWND (), IDC_DATE, _r_fmt (I18N (&app, IDS_DATE, 0), result[L"timestamp"]));

					_r_wnd_toggle (app.GetHWND (), TRUE);

					is_success = _app_downloadupdate (internet, result[L"download"], path);

					if (is_success)
					{
						is_success = _app_installupdate (path);
						_app_cleanup (result[L"version"]);
					}
					else
					{
						_r_msg (app.GetHWND (), MB_OK | MB_ICONWARNING, APP_NAME, I18N (&app, IDS_STATUS_NOTFOUND, 0), browser_name_full);
					}
				}

				if (is_success || is_exists)
					app.ConfigSet (L"ChromiumCheckPeriodLast", _r_unixtime_now ());
			}
			else
			{
				_r_msg (app.GetHWND (), MB_OK | MB_ICONWARNING, APP_NAME, I18N (&app, IDS_STATUS_NOTFOUND, 0), browser_name_full);
			}

			InternetCloseHandle (internet);
		}
	}

	PostMessage (app.GetHWND (), WM_DESTROY, 0, 0);

	return ERROR_SUCCESS;
}

rstring _app_getversion (LPCWSTR path)
{
	rstring result;

	DWORD verHandle;
	DWORD verSize = GetFileVersionInfoSize (path, &verHandle);

	if (verSize)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo (path, verHandle, verSize, verData))
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

BOOL _app_run ()
{
	ExpandEnvironmentStrings (browser_args.GetString (), browser_args.GetBuffer (_R_BUFFER_LENGTH), _R_BUFFER_LENGTH);

	return _r_run (_r_fmt (L"\"%s\" %s", browser_exe, browser_args), browser_directory);
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			app.SetHWND (hwnd);

			SetCurrentDirectory (app.GetDirectory ());

			INT parts[] = {app.GetDPI (230), -1};
			SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

			// get browser architecture...
			browser_architecture = app.ConfigGet (L"ChromiumArchitecture", 0).AsInt ();

			if (!browser_architecture || (browser_architecture != 64 && browser_architecture != 32))
			{
				browser_architecture = 0;

				// on XP only 32-bit supported
				if (_r_sys_validversion (5, 1, VER_EQUAL))
				{
					browser_architecture = 32;
				}

				if (!browser_architecture)
				{
					// ...by executable
					DWORD exe_type = 0;

					if (GetBinaryType (browser_exe, &exe_type))
					{
						if (exe_type == SCS_32BIT_BINARY)
						{
							browser_architecture = 32;
						}
						else if (exe_type == SCS_64BIT_BINARY)
						{
							browser_architecture = 64;
						}
					}

					// ...by processor browser_architecture
					SYSTEM_INFO si = {0};
					GetNativeSystemInfo (&si);

					if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
					{
						browser_architecture = 64;
					}
					else
					{
						browser_architecture = 32;
					}
				}
			}

			// configure paths
			browser_type = app.ConfigGet (L"ChromiumType", L"dev-codecs-sync");
			browser_name_full.Format (L"Chromium %d-bit (%s)", browser_architecture, browser_type);
			browser_directory = _r_normalize_path (app.ConfigGet (L"ChromiumDirectory", L".\\bin"));
			browser_exe.Format (L"%s\\chrome.exe", browser_directory);
			browser_args = app.ConfigGet (L"ChromiumCommandLine", L"--user-data-dir=..\\profile --no-default-browser-check --allow-outdated-plugins --disable-component-update");
			browser_version = _app_getversion (browser_exe);

			rstring ppapi_path = _r_normalize_path (app.ConfigGet (L"FlashPlayerPath", L".\\plugins\\pepflashplayer.dll"));

			if (!ppapi_path.IsEmpty () && _r_fs_exists (ppapi_path))
			{
				rstring ppapi_version = _app_getversion (ppapi_path);

				browser_args.Append (L" --ppapi-flash-path=\"");
				browser_args.Append (ppapi_path);
				browser_args.Append (L"\" --ppapi-flash-version=\"");
				browser_args.Append (ppapi_version);
				browser_args.Append (L"\"");
			}

			// parse command line
			INT numargs = 0;
			LPWSTR* arga = CommandLineToArgvW (GetCommandLine (), &numargs);

			rstring url;

			for (INT i = 1; i < numargs; i++)
			{
				if (_wcsicmp (arga[i], L"/url") == 0)
				{
					continue;
				}
				else if (arga[i][0] == L'-' && arga[i][1] == L'-')
				{
					browser_args.Append (L" ");
					browser_args.Append (arga[i]);
				}
				else
				{
					url = arga[i];
				}
			}

			if (!url.IsEmpty ())
			{
				browser_args.Append (L" -- \"");
				browser_args.Append (url);
				browser_args.Append (L"\"");
			}

			LocalFree (arga);

			// start update checking
			_beginthreadex (nullptr, 0, &_app_checkupdate, nullptr, 0, nullptr);

			break;
		}

		case WM_CLOSE:
		{
			if (_r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, I18N (&app, IDS_QUESTION_BUSY, 0)) != IDYES)
			{
				return TRUE;
			}

			DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			if (!_app_run ())
			{
				_r_msg (hwnd, MB_OK | MB_ICONSTOP, APP_NAME, I18N (&app, IDS_STATUS_ERROR, 0), GetLastError ());
			}

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
			SendMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
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
					SendMessage (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDM_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_ABOUT:
				{
					app.CreateAboutWindow ();
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
	if (app.CreateMainWindow (&DlgProc, nullptr))
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
