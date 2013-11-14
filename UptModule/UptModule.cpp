#include "StdAfx.h"

CUpdater*			gUpdaterHandle;
static HANDLE		hCRCEvent, hMainEvent, hTerminatingEvent;

// Teh factory
// UpdaterLoader API calls it now
extern "C" IUpdaterClient* CreateUpdaterInstance(const char* pName)
{
	// DO make sure these names match macroes inside Updater.h!
	if ( !strncmp(pName, "UPTMODULE_INTERFACE_CLIENT001", 30) )
	{
		CUpdaterClient001*		pClient = new CUpdaterClient001;
		return (IUpdaterClient*)pClient;
	}

	// Something has gone wrong..
	return nullptr;
}

IUpdaterClient001* GetUpdaterInterface()
{
	// Now wraps the factory call for compatibility purposes
	return (CUpdaterClient001*)CreateUpdaterInstance(UPDATER_INTERFACE_CLIENT001);
}

// Some great wrappers and replicates of MS functions for std::string
void StrPathAppend(std::wstring& pszPath, const wchar_t* pszMore)
{
	if ( !pszPath.empty() )
	{
		if ( pszPath.back() != '\\' )
			pszPath.push_back('\\');
		pszPath += pszMore;
	}
	else
		pszPath = pszMore;
}

DWORD WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	UNREFERENCED_PARAMETER(hinstDLL);
	UNREFERENCED_PARAMETER(lpvReserved);

	switch ( fdwReason )
	{
	case DLL_PROCESS_ATTACH:
		curl_global_init(CURL_GLOBAL_WIN32);
		gUpdaterHandle = new CUpdater;
		break;

	case DLL_PROCESS_DETACH:
		gUpdaterHandle->DisconnectFromFTP();
		gUpdaterHandle->WriteSettingsFile();
		curl_global_cleanup();
		delete gUpdaterHandle;
		break;
	}
	return TRUE;
}

static inline const wchar_t* GetSizeText(size_t nSize)
{
	static wchar_t			wcSizeText[16];
	if ( nSize < 1024 )
		_snwprintf(wcSizeText, 16, L"%dB", nSize);
	else
	{
		if ( nSize < 1024 * 1024 )
			_snwprintf(wcSizeText, 16, L"%.2fKB", static_cast<float>(nSize) / 1024);
		else
		{
			if ( nSize < 1024 * 1024 * 1024 )
				_snwprintf(wcSizeText, 16, L"%.2fMB", static_cast<float>(nSize) / (1024*1024));
			else
				_snwprintf(wcSizeText, 16, L"%.2fGB", static_cast<float>(nSize) / (1024*1024*1024));
		}
	}
	return wcSizeText;
}

static void CleanupDirectory(const wchar_t* pDirName)
{
	WIN32_FIND_DATA		findData;
	HANDLE				hFile;

	if ( SetCurrentDirectory(pDirName) )
	{
		hFile = FindFirstFile(L"*", &findData);
		if ( hFile != INVALID_HANDLE_VALUE )
		{
			do
			{
				if ( _wcsicmp(findData.cFileName, L".") && _wcsicmp(findData.cFileName, L"..") )
				{
					if ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
						CleanupDirectory(findData.cFileName);
					else
						DeleteFile(findData.cFileName);
				}
			}
			while ( FindNextFile(hFile, &findData) );

			FindClose(hFile);
		}

		SetCurrentDirectory(L"..\\");
		RemoveDirectory(pDirName);
	}
}

static void MakeSureDirExists(const wchar_t* pDirName)
{
	HANDLE hDir = CreateFile(pDirName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if ( hDir == INVALID_HANDLE_VALUE )
		CreateDirectory(pDirName, NULL);
	else
		CloseHandle(hDir);
}

static size_t WriteData(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	size_t blocks_written = fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
	gUpdaterHandle->AddProgress(blocks_written * size);

	return blocks_written;
}

static bool IsWindowsVistaOrHigher()
{
	OSVERSIONINFO osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osvi);
	return osvi.dwMajorVersion >= 6;
}

DWORD WINAPI HashingThread(LPVOID lpParam)
{
	CCRC32				HashingClass;
	HashPair*			pStruct = reinterpret_cast<HashPair*>(lpParam);
	HANDLE				handles[2] = { hCRCEvent, hTerminatingEvent };

	for (;;)
	{
		DWORD	dwReturnCode = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		if ( !(dwReturnCode - WAIT_OBJECT_0) )
		{
			HashingClass.FileCRC(pStruct->first.c_str(), &pStruct->second, 8388608);
			ResetEvent(hCRCEvent);
			SetEvent(hMainEvent);
		}
		else
		{
			if ( (dwReturnCode - WAIT_OBJECT_0) == 1 )
				break;
		}
	}

	CloseHandle(hCRCEvent);
	CloseHandle(hMainEvent);
	CloseHandle(hTerminatingEvent);
	return TRUE;
}

void CUpdater::EchoMessage(const char* pMessage)
{
	if ( MessageFunction )
	{
		wchar_t		wcMessage[512];

		mbstowcs(wcMessage, pMessage, 512);
		MessageFunction(wcMessage);
	}
}

void CUpdater::EchoMessage(const wchar_t* pMessage)
{
	if ( MessageFunction )
		MessageFunction(pMessage);
}

void CUpdater::ConnectToFTP()
{
	if ( !pCurlHandle )
	{
		pCurlHandle = curl_easy_init();
		curl_easy_setopt(pCurlHandle, CURLOPT_WRITEFUNCTION, WriteData);
	}
	if ( !pCurlMulti )
		pCurlMulti = curl_multi_init();
}

void CUpdater::DisconnectFromFTP()
{
	if ( pCurlHandle )
	{
		if ( pCurlMulti )
			curl_multi_remove_handle(pCurlMulti, pCurlHandle);
		curl_easy_cleanup(pCurlHandle);
		pCurlHandle = nullptr;

		if ( pCurlMulti )
		{
			curl_multi_cleanup(pCurlMulti);
			pCurlMulti = nullptr;
		}
	}
	if ( bDownloadingStatus != 3 && hDownloadedFile )
	{
		fclose(hDownloadedFile);
		hDownloadedFile = nullptr;
	}
}

void CUpdater::SetFileToDownload(const wchar_t* pFileName, bool bReadWriteAccess)
{
	// Make sure upt_cache exists in User Files + make sure the directories exist
	//char	cUptCacheDir[MAX_PATH];	// libcurl wants this as char
	//wchar_t	cTempPath[MAX_PATH];
	//wchar_t	cParentDirPath[MAX_PATH];
	std::wstring		strPathToFile(szFullCachePath);

	//wcsncpy(cTempPath, szFullCachePath, MAX_PATH);
	//wcstombs(cUptCacheDir, cTempPath, MAX_PATH);
	szCurrentFile = pFileName;
	MakeSureDirExists(szFullCachePath.c_str());

	StrPathAppend(strPathToFile, L"upt_cache");
	MakeSureDirExists(strPathToFile.c_str());

	//StrPathAppend(strPathToFile, pFileName);

	std::wstring	strFileNameTokerized(pFileName);

	// Fill path per directory
	size_t		found = strFileNameTokerized.find_first_of('\\');
	if ( found != std::string::npos )
	{
		size_t		lastFound = 0;
		do
		{
			//std::wstring		strTempString(strFileNameTokerized, lastFound, found - lastFound);
			StrPathAppend(strPathToFile, std::wstring(strFileNameTokerized.cbegin()+lastFound, strFileNameTokerized.cbegin()+found/*strTempString.c_str()*/).c_str());
			MakeSureDirExists(strPathToFile.c_str());

			lastFound = found+1;
			found = strFileNameTokerized.find_first_of('\\', found+1);
		}
		while ( found != std::string::npos );

		StrPathAppend(strPathToFile, strFileNameTokerized.substr(lastFound).c_str());
	}
	else
	{
		// No subdirs
		StrPathAppend(strPathToFile, pFileName);
	}

	/*wchar_t*	pSlashPos;
	int			nUserFilesPathEnd = strlen(cUptCacheDir) + 1;

	PathAppendA(cUptCacheDir, pFileName);
	wcsncpy(cTempPath, cUptCacheDir, MAX_PATH);
	pSlashPos = wcschr(&cTempPath[nUserFilesPathEnd], '\\');

	if ( pSlashPos )
	{
		for (;;)
		{
			*pSlashPos = '\0';

			MakeSureDirExists(cTempPath);

			*pSlashPos = '\\';
			if ( pSlashPos == wcsrchr(&cTempPath[nUserFilesPathEnd], '\\') )
				break;
			pSlashPos = wcschr(pSlashPos+1, '\\');
		}

	}*/

	if ( FILE* hTheFile = _wfopen(strPathToFile.c_str(), bReadWriteAccess ? L"wb+" : L"wb") )
	{
		std::string			strCurlPath = LOGIN_DATA"/";

		strCurlPath += std::string(szCurrentFile.cbegin(), szCurrentFile.cend());

		for ( auto it = strCurlPath.begin(); it != strCurlPath.end(); it++ )
		{
			if ( *it == '\\' )
				*it = '/';
		}

		curl_easy_setopt(pCurlHandle, CURLOPT_WRITEDATA, hTheFile);
		curl_easy_setopt(pCurlHandle, CURLOPT_URL, strCurlPath.c_str());
		curl_multi_add_handle(pCurlMulti, pCurlHandle);

		hDownloadedFile = hTheFile;
		bDownloadingStatus = 1;
	}
	else
		hDownloadedFile = NULL;
}

void CUpdater::CleanupCache()
{
	wchar_t				cBackupCurrentDir[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, cBackupCurrentDir);

	if ( SetCurrentDirectory(szFullCachePath.c_str()) )
	{
		CleanupDirectory(L"upt_cache");

		SetCurrentDirectory(cBackupCurrentDir);
	}
}


void CUpdater::AddFileToDownloadList(const char* pFileToAdd)
{
	std::string		strNewFileNarrow(pFileToAdd);
	filesList.push_back(std::wstring(strNewFileNarrow.cbegin(), strNewFileNarrow.cend()));
}

void CUpdater::DownloadFile()
{
	int		nActiveTransfers;
	curl_multi_perform(pCurlMulti, &nActiveTransfers);
	bDownloadingStatus = 2;
}

long CUpdater::Process()
{
	long	nStatus;
	if ( bCheckingForUpdates )
		nStatus = UPTMODULESTATE_CHECKING;
	else
	{
		if ( bFilesReadyToInstall )
			nStatus = UPTMODULESTATE_NEW_UPDATES;
		else
		{
			if ( bDownloadingInProgress )
				nStatus = UPTMODULESTATE_DOWNLOADING;
			else
			{
				if ( bAllReady )
					nStatus = UPTMODULESTATE_ALL_READY;
				else
				{
					if ( bMoveFilesNow )
						nStatus = UPTMODULESTATE_INSTALLING;
					else
						nStatus = UPTMODULESTATE_IDLE;
				}
			}
		}
	}
	if ( bDownloadingStatus > 1 )
	{
		if ( bDownloadingStatus != 4 )
		{
			int		nActiveTransfers;
			int		nPerformMessage = curl_multi_perform(pCurlMulti, &nActiveTransfers);
			if ( nPerformMessage == CURLM_OK )
			{
				// Process messages
				int			nMessagesPending;
				CURLMsg*	pCurlMultiMessage = curl_multi_info_read(pCurlMulti, &nMessagesPending);

				if ( pCurlMultiMessage )
				{
					if ( pCurlMultiMessage->msg == CURLMSG_DONE )
					{
						CURLcode	errorCode = pCurlMultiMessage->data.result;
						/*if ( pCurlMultiMessage->msg != CURLMSG_DONE )
						{*/

						curl_multi_remove_handle(pCurlMulti, pCurlHandle);

						if ( errorCode == CURLE_OK )
						{
							if ( bDownloadingStatus == 3 )
							{
								// Check for updates
								DisconnectFromFTP();
								wNumberOfFiles = 0;
								bDownloadingStatus = 4;
							}
							else
								DownloadNextFile(nStatus, false);
						}
						else
						{
							if ( bDownloadingStatus == 3 )
							{
								EchoMessage(L"Update service is unavailable right now");
								DisconnectFromFTP();
								bCheckingForUpdates = false;
								bDownloadingInProgress = false;
								bUpdateServiceOff = true;
								bDownloadingStatus = 0;
								nStatus = UPTMODULESTATE_IDLE;
							}
							else
							{
								wchar_t		wcError[256];

								_snwprintf(wcError, 256, L"An error occured when downloading file %s, skipping...", szCurrentFile.c_str());
								EchoMessage(wcError);
								DownloadNextFile(nStatus, true);
							}
						}
					}
				}
			}
			else
			{
				wchar_t		wcError[128];
				_snwprintf(wcError, 128, L"An error occured while downloading the updates: error code %d", nPerformMessage);
				EchoMessage(wcError);
				DisconnectFromFTP();
				bCheckingForUpdates = false;
				bDownloadingInProgress = false;
				bDownloadingStatus = 0;
				nStatus = UPTMODULESTATE_IDLE;
			}
		}
		else
		{
			// Parse hashmap one entry by tick
			bool	bIHaveFinished = false;
			nUpdateResult = ParseHashmap(bIHaveFinished);

			if ( bIHaveFinished )
			{
				fclose(hDownloadedFile);
				hDownloadedFile = NULL;
				if ( nUpdateResult )
				{
					bFilesReadyToInstall = true;
					nStatus = UPTMODULESTATE_NEW_UPDATES;
					if ( nUpdateResult == 2 )
					{
						EchoMessage(L"New updater version available!");
						AddFileToDownloadList("updater\\updater.exe");
						AddFileToDownloadList("updater\\UptModule.dll");
						bDownloadingStatus = 0;
						bCheckingForUpdates = false;
					}
					else
					{
						wchar_t		wcSizeText[MAX_PATH];
						_snwprintf(wcSizeText, MAX_PATH, L"%d files to download, %s will be downloaded", filesList.size(), GetSizeText(dwTotalDownloadSize));
						EchoMessage(L"An update for the game is available to download");
						EchoMessage(wcSizeText);
					}
				}
				else
					EchoMessage(L"Your game is up to date");
			}
		}
	}
	else
	{
		if ( bMoveFilesNow )
		{
			wchar_t			cCurrentDirectoryBackup[MAX_PATH];
			std::wstring	strDirName(szFullCachePath);

			StrPathAppend(strDirName, L"upt_cache");

			GetCurrentDirectory(MAX_PATH, cCurrentDirectoryBackup);
			if ( SetCurrentDirectory(strDirName.c_str()) )
			{
				strDirName.clear();
				MoveFiles(strDirName);
				SetCurrentDirectory(cCurrentDirectoryBackup);
				EchoMessage(L"The update has been installed successfully!");
			}
			bMoveFilesNow = false;
			nStatus = UPTMODULESTATE_IDLE;
		}
	}
	return nStatus;
}

void CUpdater::Initialize()
{
	if ( !bInitialized )
	{
		wchar_t			wcTempPath[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, wcTempPath);
		//GetModuleFileName(nullptr, wcTempPath, MAX_PATH);

		// Cut last 2 \'s from the string
		/*wchar_t*		pSlashPos = wcsrchr(wcTempPath, '\\');
		if ( pSlashPos )
		{
			if ( !bFromMainDir )
			{
				pSlashPos = '\0';
				pSlashPos = wcsrchr(wcTempPath, '\\');
			}*/
		szFullGamePath = wcTempPath;

		HKEY	hRegKey;

		if ( RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders", 0, KEY_READ, &hRegKey) == ERROR_SUCCESS )
		{
			DWORD	dwType;
			DWORD	dwSize = MAX_PATH;

			memset(wcTempPath, 0, sizeof(wcTempPath));

			RegQueryValueEx(hRegKey, L"Personal", 0, &dwType, reinterpret_cast<unsigned char*>(wcTempPath), &dwSize);
			RegCloseKey(hRegKey);

			PathAppend(wcTempPath, L"GTA Vice City Stories User Files");
			szFullCachePath = wcTempPath;

			// Read updater.set
			ReadSettingsFile();
		}

		bInitialized = true;
	}
}

void CUpdater::RegisterMessageCallback(UptMessageCallback function)
{
	MessageFunction = function;
}

bool CUpdater::PerformUpdateCheck()
{
	EchoMessage(L"Checking for updates...");
	bCheckingForUpdates = true;
	bFilesReadyToInstall = false;
	bDownloadingInProgress = false;
	bUpdateServiceOff = false;
	bAllReady = false;
	dwDownloadedData = 0;
	CleanupCache();
	CleanupFilesList();
	ConnectToFTP();
	SetFileToDownload(HASHMAP_NAME, true);
	DownloadFile();
	bDownloadingStatus = 3;

	return true;
}

void CUpdater::PerformFilesDownload()
{
	EchoMessage(L"Downloading updates...");
	listIt = filesList.cbegin();
	bFilesReadyToInstall = false;
	bDownloadingInProgress = true;
	ConnectToFTP();
	SetFileToDownload(listIt->c_str());
	DownloadFile();
}

void CUpdater::LaunchInstaller()
{
	if ( bAllReady )
	{
		std::wstring	strUpdaterPath(bUpdaterMustUpdate ? szFullCachePath : szFullGamePath);
		std::wstring	strCommandLinePath(L"-install ");
		strCommandLinePath += szFullGamePath;

		if ( bUpdaterMustUpdate )
			StrPathAppend(strUpdaterPath, L"upt_cache");

		StrPathAppend(strUpdaterPath, L"updater\\updater.exe");

		ShellExecute(NULL, IsWindowsVistaOrHigher() ? L"runas" : nullptr, strUpdaterPath.c_str(), strCommandLinePath.c_str(), nullptr, SW_SHOWNORMAL);

		bAllReady = false;
	}
}

void CUpdater::InstallUpdates()
{
	bMoveFilesNow = true;
}

long CUpdater::ParseHashmap(bool& bFinished)
{
	DWORD					buffer;
	static HANDLE			hHashingThread;
	static BYTE				bParsingState = 0;
	static BYTE				bUpdateStatus;
	static HashPair			sCommunicationModule;

	switch ( bParsingState )
	{
	case 0:
		{
			rewind(hDownloadedFile);
			fread(&buffer, 4, 1, hDownloadedFile);

			if ( buffer > UPDATER_VERSION )
			{
				bFinished = true;
				bUpdaterMustUpdate = true;
				return 2;	// Old updater
			}
			hCRCEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			hMainEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			hTerminatingEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			hHashingThread = CreateThread(NULL, 0, HashingThread, &sCommunicationModule, 0, 0);
			bUpdaterMustUpdate = false;
			wCurrentReadFile = 0;
			dwTotalDownloadSize = 0;
			bUpdateStatus = 0;
			++bParsingState;
			return -1;
		}
	case 1:
		{
			WORD		wNumberOfDLCs;
			fread(&wNumberOfFiles, 2, 1, hDownloadedFile);
			fread(&wNumberOfDLCs, 2, 1, hDownloadedFile);

			// Parse DLC stuff
			if ( m_DLCsByIndex )
			{
				delete[] m_DLCsByIndex;
				m_DLCsByIndex = nullptr;
			}

			if ( wNumberOfDLCs )
			{
				m_DLCsByIndex = new DLCEntry[wNumberOfDLCs];
				for ( WORD i = 0; i < wNumberOfDLCs; ++i )
				{
					char			cFileName[32];
					time_t			times[2];
					BYTE			bStringLength;
					memset(cFileName, 0, 32);
					fread(&bStringLength, 1, 1, hDownloadedFile);
					fread(cFileName, bStringLength, 1, hDownloadedFile);
					fread(times, sizeof(time_t), 2, hDownloadedFile);
					m_DLCsByIndex[i].m_strName = cFileName;
					m_DLCsByIndex[i].m_launchDate = times[0];
					m_DLCsByIndex[i].m_expirationDate = times[1];
				}
			}
			++bParsingState;
			return -1;
		}
	case 2:
		{
			//static WORD		wCurrentReadFile = 0;
			static bool			bHashingNow = false;
			static char			cFileName[MAX_PATH];
			static HashEntry	fileData;

			if ( !bHashingNow )
			{
				if ( wCurrentReadFile < wNumberOfFiles )
				{
	//				static CCRC32	HashingClass;
					WORD			wStringLength;
					wchar_t			wcWideFileName[MAX_PATH];

					memset(cFileName, 0, MAX_PATH);
					fread(&wStringLength, 2, 1, hDownloadedFile);
					fread(cFileName, wStringLength, 1, hDownloadedFile);
					fread(&fileData, sizeof(HashEntry), 1, hDownloadedFile);
					if ( fileData.nDLCNum == -1 || CheckThisDLC(fileData.nDLCNum) )
					{
						mbstowcs(wcWideFileName, cFileName, MAX_PATH);

						//wcsncpy(sCommunicationModule.first.data(), szFullGamePath, MAX_PATH);
						sCommunicationModule.first = szFullGamePath;
						StrPathAppend(sCommunicationModule.first, wcWideFileName);
						//PathAppend(sCommunicationModule.first.data(), cFileName);
						SetEvent(hCRCEvent);
						bHashingNow = true;
		//				PathAppend(cFullFilePath, cFileName);

						// CRC32 the local file
		//				DWORD	dwLocalHash;
		//				char	cFullFilePath[MAX_PATH];

		//				strncpy(cFullFilePath, szFullGamePath, MAX_PATH);
		//				PathAppend(cFullFilePath, cFileName);
		//				HashingClass.FileCRC(cFullFilePath, &dwLocalHash);
					}
					else
						++wCurrentReadFile;
				}
				else
				{
					wCurrentReadFile = 0;
					++bParsingState;
				}
			}
			else
			{
				if ( WaitForSingleObject(hMainEvent, 0) == WAIT_OBJECT_0 )
				{
					// Check
					if ( sCommunicationModule.second != 0xFFFFFFFF || !fileData.bIgnoreIfAbsent )
					{
						if ( sCommunicationModule.second != fileData.nHash )
						{
							AddFileToDownloadList(cFileName);
							dwTotalDownloadSize += fileData.nSize;
							bUpdateStatus = 1;
						}
					}
					ResetEvent(hMainEvent);
					bHashingNow = false;

					++wCurrentReadFile;
				}
			}
			return -1;

		}
	case 3:
		{
			SetEvent(hTerminatingEvent);
			bFinished = true;
			bParsingState = 0;
			bDownloadingStatus = 0;
			bCheckingForUpdates = false;
			return bUpdateStatus;
		}
	}
	return 0;	// No updates
}

void CUpdater::DownloadNextFile(long& nStatus, bool bPopCurrentFile)
{
	if ( hDownloadedFile )
	{
		fclose(hDownloadedFile);
		hDownloadedFile = nullptr;
		if ( bPopCurrentFile )
		{
			std::wstring	strFullFilePath(szFullCachePath);

			StrPathAppend(strFullFilePath, L"upt_cache");
			StrPathAppend(strFullFilePath, szCurrentFile.c_str());
			//wchar_t		szFullPathToFile[MAX_PATH];

			//wcsncpy(szFullPathToFile, szFullCachePath, MAX_PATH);
			//PathAppend(szFullPathToFile, L"upt_cache");
			//PathAppend(szFullPathToFile, szCurrentFile);
			DeleteFile(strFullFilePath.c_str());
		}
	}
	listIt++;
	if ( listIt == filesList.cend() )
	{
		DisconnectFromFTP();
//		CleanupCache();
		bDownloadingInProgress = false;
		bAllReady = true;
		bDownloadingStatus = 0;
		nStatus = UPTMODULESTATE_ALL_READY;
	}
	else
	{
		SetFileToDownload(listIt->c_str());
		DownloadFile();
	}
}

void CUpdater::MoveFiles(std::wstring& dirName)
{
	WIN32_FIND_DATA		findData;
	HANDLE				hFile;

	hFile = FindFirstFile(L"*", &findData);

	if ( hFile != INVALID_HANDLE_VALUE )
	{
		do
		{
			if ( _wcsicmp(findData.cFileName, L".") && _wcsicmp(findData.cFileName, L"..") && _wcsicmp(findData.cFileName, HASHMAP_NAME) )
			{
				std::wstring		strDirToCheck(szFullGamePath);

				StrPathAppend(strDirToCheck, dirName.c_str());
				StrPathAppend(strDirToCheck, findData.cFileName);
				/*wchar_t	cSADirToCheck[MAX_PATH];

				wcsncpy(cSADirToCheck, szFullGamePath, MAX_PATH);
				PathAppend(cSADirToCheck, pDirName);
				PathAppend(cSADirToCheck, findData.cFileName);*/

				if ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				{
					//long	nPlaceInString = strlen(pDirName);
					MakeSureDirExists(strDirToCheck.c_str());
					if ( SetCurrentDirectory(findData.cFileName) )
					{
						std::wstring		strTempString(dirName);
						StrPathAppend(strTempString, findData.cFileName);
						//PathAppend(pDirName, findData.cFileName);
						MoveFiles(strTempString);
						//pDirName[nPlaceInString] = '\0';
					}
				}
				else
				{
					SetFileAttributes(strDirToCheck.c_str(), FILE_ATTRIBUTE_NORMAL);
					if ( !_wcsicmp(findData.cFileName, L"updater.exe") || !_wcsicmp(findData.cFileName, L"UptModule.dll") )
					{
						unsigned long	nAttempts = 0;
						while ( !CopyFile(findData.cFileName, strDirToCheck.c_str(), FALSE) )
						{
							Sleep(250);
							if ( ++nAttempts >= 15 )
							{
								wchar_t		wcError[128];
								//wchar_t		wcFileName[96];

								//mbstowcs(wcFileName, findData.cFileName, 96);
								_snwprintf(wcError, 128, L"Failed to install file %s", findData.cFileName);
								EchoMessage(wcError);
								break;
							}
						}
					}
					else
					{
						unsigned long nAttempts = 0;
						DeleteFile(strDirToCheck.c_str());

						while ( !MoveFile(findData.cFileName, strDirToCheck.c_str()) )
						{
							Sleep(250);
							if ( ++nAttempts >= 15 )
							{
								wchar_t		wcError[128];

								_snwprintf(wcError, 128, L"Failed to install file %s", findData.cFileName);
								EchoMessage(wcError);
								break;
							}
						}
					}
				}
			}
		}
		while ( FindNextFile(hFile, &findData) );

		FindClose(hFile);
	}

	SetCurrentDirectory(L"..\\");
}

void CUpdater::ReadSettingsFile()
{
	std::wstring		strDLCFilePath = szFullCachePath;
	StrPathAppend(strDLCFilePath, L"dlc.set");

	if ( FILE* hSetFile = _wfopen(strDLCFilePath.c_str(), L"rb") )
	{
		unsigned int		nFileVersion;
		signed short		nIndex = 0;

		fread(&nFileVersion, 4, 1, hSetFile);
		if ( nFileVersion == DLC_SETTINGS_FILE_VERSION )
		{
			for ( ;; )
			{
				char		cDLCName[32];
				bool		bDLCState;
				BYTE		nStringLength;

				if ( fread(&nStringLength, 1, 1, hSetFile) != 1 )
					break;

				memset(cDLCName, 0, 32);
				fread(cDLCName, nStringLength, 1, hSetFile);
				fread(&bDLCState, 1, 1, hSetFile);
				AddThisDLCToList(cDLCName, bDLCState);
				++nIndex;
			}
		}
		else
		{
			// TODO: Handle different dlc.set versions
		}
		fclose(hSetFile);
	}
}

void CUpdater::WriteSettingsFile()
{
	std::wstring		strDLCFilePath = szFullCachePath;
	StrPathAppend(strDLCFilePath, L"dlc.set");

	if ( FILE* hSetFile = _wfopen(strDLCFilePath.c_str(), L"wb") )
	{
		unsigned int		nFileVersion = DLC_SETTINGS_FILE_VERSION;
		//signed short		nIndex = 0;

		fwrite(&nFileVersion, 4, 1, hSetFile);
		for ( auto it = m_DLCsMap.cbegin(); it != m_DLCsMap.cend(); it++ )
		{
			/*bool	bInstalled;
			if ( nIndex == it->first )
			{
				// This DLC is marked as active
				bInstalled = true;
				it++;
			}
			else
				bInstalled = false;*/

			/*fwrite(it->second.c_str(), 32, 1, hSetFile);
			fwrite(&bInstalled, 1, 1, hSetFile);*/
			BYTE		nStringSize = static_cast<BYTE>(it->first.size());
			fwrite(&nStringSize, 1, 1, hSetFile);
			fwrite(it->first.c_str(), nStringSize, 1, hSetFile);
			fwrite(&it->second, 1, 1, hSetFile);
		}
		fclose(hSetFile);
	}
}