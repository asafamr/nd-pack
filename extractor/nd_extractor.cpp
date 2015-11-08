//#include "libzip/zip.h"
#include "windows.h"
#include "../common/duckpack.h"

#include <fstream>
#include <string>
#include <string.h>
#include <iostream>
#include "Shlobj.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#define LIBARCHIVE_STATIC
extern "C" 
{
#include "../common/lib_archive/include/archive.h"
#include "../common/lib_archive/include/archive_entry.h"
}
#include <locale>
#include <codecvt>
#include <algorithm>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>
#include <sddl.h>
#include <tchar.h>

using namespace std;

#define BUFFER_SIZE 4096

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <streambuf>
#include <istream>

struct membuf : std::streambuf {
	membuf(char const* base, size_t size) {
		char* p(const_cast<char*>(base));
		this->setg(p, p, p + size);
	}
};
struct imemstream : virtual membuf, std::istream {
	imemstream(char const* base, size_t size)
		: membuf(base, size)
		, std::istream(static_cast<std::streambuf*>(this)) {
	}
};

#pragma comment(lib,"../common/lib_archive/lib/libarchive_" LIB_SUFFIX)
#pragma comment(lib,"../common/zlib/lib/zlib_" LIB_SUFFIX)


//#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup")


#define LOG_ERROR(...) Logger::logger.log( false , __VA_ARGS__ )
#define LOG_DEBUG(...) Logger::logger.log( true , __VA_ARGS__ )


inline std::wstring convertToWideString( const std::string& as )
{
	// deal with trivial case of empty string
	if( as.empty() )    return std::wstring();

	// determine required length of new string
	size_t reqLength = MultiByteToWideChar( CP_UTF8, 0, as.c_str(), (int)as.length(), 0, 0 );

	// construct new string of required length
	std::wstring ret( reqLength, L'\0' );

	// convert old string to new string
	MultiByteToWideChar( CP_UTF8, 0, as.c_str(), (int)as.length(), &ret[0], (int)ret.length() );
	return ret;
}


class Logger
{
public:
	void setVerbose(bool isVerbose){verbose=isVerbose;}
	void setOn(bool isOn){loggerOn=isOn;}
	void log(bool onlyVerbose,const wchar_t* format, ...);
	void logCstr(bool error,const wstring & prefix,const char * str);
	//void log(bool onlyVerbose,const char* format, ...);
	bool openStream(wstring & path);
	static Logger logger;
	~Logger();
private:
	wofstream *outLog;
	bool verbose;
	bool loggerOn;
	Logger():verbose(false),loggerOn(false),outLog(NULL){};
	
};


Logger Logger::logger;

Logger::~Logger()
{
	if(outLog)
	{
		outLog->flush();
		delete outLog;
	}
};
bool Logger::openStream(wstring & path)
{
	if(outLog)
	{
		outLog->flush();
		delete outLog;
	}
	outLog = new wofstream(path);
	return outLog!=NULL;
}

void Logger::log(bool onlyVerbose,const wchar_t* format, ...)
{
	if(!loggerOn||!outLog)return;
	if(onlyVerbose &!verbose)return;
	static wchar_t buffer[2048];
	va_list argptr;
	va_start(argptr, format);
	vswprintf(buffer, 2048, format, argptr);
	va_end(argptr);

	(*outLog) << buffer << L"\n";
	outLog->flush();
};

void Logger::logCstr(bool error,const wstring & prefix,const char * str)
{
	string s(str);
	wstring wideS(convertToWideString(s));
	log(!error,L"%s : %s",prefix.c_str(),wideS.c_str());
}


wstring GetLastErrorStdStrW()
{
	DWORD error = GetLastError();
	wchar_t errNumBuff[20]={0};
	_itow_s(error,errNumBuff,20,10);
	if (error)
	{
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | 
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR) &lpMsgBuf,
			0, NULL );
		if (bufLen)
		{
			LPCWSTR lpMsgStr = (LPCWSTR)lpMsgBuf;
			wstring result(lpMsgStr, lpMsgStr+bufLen);

			LocalFree(lpMsgBuf);
			result=wstring(errNumBuff) + L": " +result;
			return result;
		}
	}
	return wstring();
}

/**
*copy_data : copy all data blocks from ar to aw by iterating the data blocks
**/
int copy_data(struct archive *ar, struct archive *aw)
{
	int r;
	const void *buff;
	size_t size;
	long long offset;

	while (true) {
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return true;
		if (r < ARCHIVE_OK)
		{
			Logger::logger.logCstr(true,L"Could not read data block",archive_error_string(ar));
			return false;
		}
		r = archive_write_data_block(aw, buff, size, offset);
		if (r < ARCHIVE_OK) {
			Logger::logger.logCstr(true,L"Could not write data block",archive_error_string(aw));
			return false;
		}
	}
}


/************************************************************************/
/*createPriviligedDir : currently only win:creates a dir and make it admin only                                         */
/************************************************************************/
bool createPriviligedDir(wstring & path)
{
	SECURITY_ATTRIBUTES  sa;

	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = FALSE;  

	TCHAR * szSD = TEXT("D:")       // Discretionary ACL
		TEXT("(D;OICI;GA;;;BG)")     // Deny access to 
		// built-in guests
		TEXT("(D;OICI;GA;;;AN)")     // Deny access to 
		// anonymous logon
		//TEXT("(A;OICI;GRGWGX;;;AU)") --disabled // Allow 
		// read/write/execute 
		// to authenticated 
		// users
		TEXT("(A;OICI;GA;;;BA)");    // Allow full control 
	// to administrators


	if(!ConvertStringSecurityDescriptorToSecurityDescriptor(
		szSD,
		SDDL_REVISION_1,
		&(sa.lpSecurityDescriptor),
		NULL))
	{
		LOG_ERROR(L"Could not secuirty descriptor");
		return false;
	}
	//LPSECURITY_ATTRIBUTES attr=
	if (0 == CreateDirectory(path.c_str(),/* &sa*/NULL))//TODO enabled admin rights for dir after adding no-admin support
	{
		LOG_ERROR(L"Could not create directory %s",path.c_str());
		return false;
	}

	// Free the memory allocated for the SECURITY_DESCRIPTOR.
	if (NULL != LocalFree(sa.lpSecurityDescriptor))
	{
		LOG_ERROR(L"Could not free security descriptor");
	}
	return true;

}

bool getTempDirPath(wstring & out)
{
	LOG_DEBUG(L"Getting temp dir...");

	//should also create and take ownership
	wchar_t tempFileDirPath[MAX_PATH];
	GetTempPathW(MAX_PATH,tempFileDirPath);

	
	srand ( time(NULL) );
	for(int tryDir=0;tryDir<5;tryDir++)
	{
		wstring strPathTempDir=tempFileDirPath;
		strPathTempDir+=L"DUCK_";
		for(int i=0;i<8;i++)
		{
			strPathTempDir += (L'A' + rand()%24);
		}

		WIN32_FIND_DATA FindFileData;
		HANDLE handle = FindFirstFile(strPathTempDir.c_str(), &FindFileData) ;
		if(handle != INVALID_HANDLE_VALUE)
		{
			//dir already exists
			FindClose(handle);
			continue;
		}
			
		if(createPriviligedDir(strPathTempDir))
		{
			LOG_DEBUG(L"Created directory %s",strPathTempDir.c_str());
			out=strPathTempDir+L"\\";
			return true;
		}
		
	}
	
	LOG_ERROR(L"Could not temp dir");
	return false;
}
/*
void extractProgress(void * data)
{
	int a=5;
	a+=10;
	if(data)
	{

		a+=20;
	}
}
*/
wstring NormalizeDirPath(const wstring& path)
{
	wstring normailizedToDir=path;
	replace( normailizedToDir.begin(), normailizedToDir.end(), '\\', '/');
	if(normailizedToDir[normailizedToDir.length()-1]!='/')
	{
		normailizedToDir+=L"/";
	}
	return normailizedToDir;
}
bool strIsPrefixed(const wstring& str,const wstring& prefix)
{
	if(prefix.empty())
	{
		return true;
	}
	if(prefix.length()>str.length())
	{
		return false;
	}
	return str.compare(0,prefix.length(),prefix)==0;
}
__int64 extract(int fd,__int64 startOffset,__int64 endOffset ,const wstring &toDir,const wstring& glob,bool printProgress)
{
	
	LOG_DEBUG(L"Extracting block... %s",glob.c_str());
	
	
	
	wstring normailizedToDir= NormalizeDirPath(toDir);
	struct archive *inArch;
	struct archive *outArch;
	struct archive_entry *entry;
	int flags;
	int r;

	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	inArch = archive_read_new();
	archive_read_support_format_all(inArch);
	archive_read_support_compression_all(inArch);
	uint64_t totalSize=0;
	uint64_t totalWritten=0;
	if (r = duck_read_open_fd(inArch, fd, 10240,startOffset,endOffset))
	{
		LOG_ERROR(L"Could not open fd");
		return false;
	}
	while (true) 
	{
		r = archive_read_next_header(inArch, &entry);
		if (r == ARCHIVE_EOF)
		{
			break;
		}
		if (r < ARCHIVE_WARN)
		{
			Logger::logger.logCstr(true,L"archive_read_next_header-computing size returned error ",archive_error_string(inArch));
			return false;	
		}
		wstring path(archive_entry_pathname_w(entry));
		if(!strIsPrefixed(path,glob))
		{
			continue;
		}
		totalSize+=archive_entry_size(entry);
	}
	LOG_DEBUG(L"Computed size: %I64u" ,totalSize);
	if(printProgress)
	{
		wprintf(L"Total size: %I64u bytes\n",totalSize);
	}
	
	outArch = archive_write_disk_new();
	archive_write_disk_set_options(outArch, flags);

	archive_read_close(inArch);
	archive_read_free(inArch);
	inArch = archive_read_new();
	archive_read_support_format_all(inArch);
	archive_read_support_compression_all(inArch);
	if (r = duck_read_open_fd(inArch, fd, 10240,startOffset,endOffset))
	{
		
		LOG_ERROR(L"Could not open fd");
		Logger::logger.logCstr(true,L"archive_read_next_header returned error ",archive_error_string(inArch));
		return false;
	}

	//archive_read_extract_set_progress_callback(inArch,extractProgress,NULL);
	while (true) 
	{
		r = archive_read_next_header(inArch, &entry);
		if (r == ARCHIVE_EOF)
		{
			LOG_DEBUG(L"EOF reached");
			break;
		}
		if (r < ARCHIVE_WARN)
		{
			Logger::logger.logCstr(true,L"archive_read_next_header returned error ",archive_error_string(inArch));
			return false;	
		}
		if (r < ARCHIVE_OK)
		{
			Logger::logger.logCstr(true,L"archive_read_next_header returned ",archive_error_string(inArch));
			//return false;	
		}

		wstring path(archive_entry_pathname_w(entry));
		if(!strIsPrefixed(path,glob))
		{
			continue;
		}

		wstring s=normailizedToDir;
		s+=path;
		LOG_DEBUG(L"Setting old name %s to %s",path.c_str(),s.c_str());
		archive_entry_copy_pathname_w ( entry, s.c_str() );
		bool isDir=archive_entry_filetype(entry)&AE_IFDIR==0;
		
		r = archive_write_header(outArch, entry);
		if (r < ARCHIVE_OK)
		{
			Logger::logger.logCstr(true,L"archive_write_header returned ",archive_error_string(outArch));
			//return false;	
		}
		else if (!isDir) 
		{
			r = copy_data(inArch, outArch);
			if (r ==false)
			{
				return false;
			}
			
		}
		r = archive_write_finish_entry(outArch);
		if(printProgress)
		{
			wprintf(L"Written %s %s\n",isDir?L"dir":L"file",path.c_str());
			totalWritten+=archive_entry_size(entry);
			wprintf(L"Progress %I64u/%I64u\n",totalWritten,totalSize);
		}
		if (r < ARCHIVE_WARN)
		{
			Logger::logger.logCstr(true,L"archive_write_finish_entry returned error ",archive_error_string(outArch));
			return false;	
		}
		if (r < ARCHIVE_OK)
		{
			Logger::logger.logCstr(true,L"archive_write_finish_entry returned ",archive_error_string(outArch));
			//return false;	
		}
		
		
	}
	archive_read_close(inArch);
	archive_read_free(inArch);

	archive_write_close(outArch);
	archive_write_free(outArch);
	
	return true;
}
/*
bool getExecutableName(wstring &dir,wstring &out)
{
	wstring line;
	ifstream myfile (dir +L".duck");
	if (myfile.is_open())
	{
		DWORD ver;
		DWORD len;
		myfile.read((char*)&ver,sizeof(ver));
		myfile.read((char*)&len,sizeof(len));	
		if(len==0)
		{
			out=L"";
		}
		else
		{
			ScopedArrVar<wchar_t> execLine(new wchar_t[len]);
			myfile.read((char*)(execLine.get()),len*sizeof(wchar_t));
			out=wstring(execLine.get(),len);
		}
		myfile.close();
	}
	return true;

}
*/
bool spawnInstallerProcess(wstring &exePath )
{

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

	WCHAR* path=const_cast<WCHAR*>(exePath.c_str());

	// Start the child process. 
	if( !CreateProcess( NULL,   // No module name (use command line)
		path,        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi )           // Pointer to PROCESS_INFORMATION structure
		) 
	{
		LOG_ERROR(L"CreateProcess failed : %s",GetLastErrorStdStrW().c_str());
		return false;
	}

	// Wait until child process exits.
	WaitForSingleObject( pi.hProcess, INFINITE );

	// Close process and thread handles. 
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );
	return true;
}
bool deleteDir(wstring &path)
{
	LOG_DEBUG(L"Deleting dir %s",path.c_str());
	// recursively delete path
	size_t nFolderPathLen = path.length();
	ScopedArrVar<TCHAR> pszFrom =  ScopedArrVar<TCHAR>( new TCHAR[nFolderPathLen + 2]);
	wcscpy_s(pszFrom.get(), nFolderPathLen + 2, path.c_str());
	pszFrom.get()[nFolderPathLen] = 0;//from the interwebz: double null terminated
	pszFrom.get()[++nFolderPathLen] = 0;

	SHFILEOPSTRUCT stSHFileOpStruct = {0};
	stSHFileOpStruct.wFunc = FO_DELETE;
	stSHFileOpStruct.pFrom = pszFrom.get(); 
	stSHFileOpStruct.fFlags = FOF_NOCONFIRMATION|FOF_NO_UI;
	stSHFileOpStruct.fAnyOperationsAborted = FALSE; 
	int nFileDeleteOprnRet = SHFileOperation( &stSHFileOpStruct );
	if(nFileDeleteOprnRet)
	{
		LOG_ERROR(L"Could not delete dir %s",path.c_str());
		return false;
	}
	LOG_DEBUG(L"Deleted");
	return true;
}

bool extractAndRunBlock1(const int selfFileHandle,__int64 startOffset,__int64 endOffset, const string& exeCmd)
{
	wstring tempDir;
	wstring exeWide(exeCmd.begin(), exeCmd.end());
	//_lseek(selfFileHandle,startOffset,SEEK_SET);

	if(!getTempDirPath(tempDir))
	{
		LOG_ERROR(L"Could not create directory");
		return -1;
	}
	if(extract(selfFileHandle,startOffset,endOffset,tempDir,L"",false)<0)
	{
		LOG_ERROR(L"Could not extract main package");
		return -1;
	}

	//exeFile=L"someh.exe";
	/*if(!getExecutableName(tempDir,exeFile))
	{
		LOG_ERROR(L"Could not get main execution command");
		return -1;
	}*/
	if(!spawnInstallerProcess(tempDir+ exeWide))
	{
		LOG_ERROR(L"Could not spawn main process");
		return -1;
	}
	if(!deleteDir(tempDir))
	{
		LOG_DEBUG(L"Could not delete tempDir %s", tempDir.c_str());
		//dont fail
	}
	return 0;
}

bool extractFromBlock2(const int selfFileHandle,__int64 startOffset,__int64 endOffset,const wstring& outPath,const wstring &glob)
{
	wstring tempDir;
	wstring exeFile;
	//_lseek(selfFileHandle,offset2,SEEK_SET);

	if(extract(selfFileHandle,startOffset,endOffset,outPath,glob,true)<0)
	{
		LOG_ERROR(L"Could not extract second block");
		return -1;
	}
	return 0;
}
#define moduleDbg L"C:\\ws\\packingws\\out.exe"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow){
	LOG_DEBUG(L"Extractor started");//not logged currently because logger isnt set yet
#ifdef moduleDbg 
	wchar_t path[MAX_PATH]= moduleDbg;
#else
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(NULL, path, MAX_PATH) ;
#endif
	
	//let windows parse arguments find the arg after the log path token and use it
	LPWSTR *arglist;
	int nArgs;
	arglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

	LOG_DEBUG(L"Module path: %s",path);
	
	
	
	wstring pathStr(path);

	const wstring verbose(L"--verbose");
	const wstring shouldLog(L"--log");
	const wstring block2(L"--block2");
	const wstring block2Path(L"--block2Path");
	const wstring block2Target(L"--block2Target");
	const wstring logpath(L"--logpath");

	wstring cmd(pCmdLine);
	Logger::logger.setVerbose(cmd.find(verbose)!=cmd.npos);
	bool LoggerOn=cmd.find(shouldLog)!=cmd.npos;
	Logger::logger.setOn(LoggerOn);
	
	if(LoggerOn)
	{
		if(cmd.find(logpath)!=cmd.npos)
		{
			
			for(int i=0;i<nArgs-1;i++)
			{
				//check if the argument i equals logpath ("--logpath") which means the next argument is the log path
				if(logpath.compare(arglist[i])==0)
				{
					if(!Logger::logger.openStream(wstring(arglist[i+1])))
					{
						//msg box?
						//LOG_ERROR(L"Could not open log file stream : %s",arglist[i+1]);
					}
				}
			}

		}
		else
		{
			
			wstring logPath(pathStr.substr(0,pathStr.find_last_of(L"/\\")) +L"/duckinstaller.log");
			if(!Logger::logger.openStream(logPath))
			{
				//msg box?
				//LOG_ERROR(L"Could not open log file stream : %s",logPath.c_str);
			}
		}
	}
	
	int  selfFileHandle;
	errno_t err = _wsopen_s( &selfFileHandle,  path, _O_RDONLY, _SH_DENYWR, _S_IREAD );

	if (err != 0)
	{
		LOG_DEBUG(L"Could not open self (err=%d): %s", (int)err,path);
		return -1;
	}
	DuckFileDescriptor desc={0};

	_lseek(selfFileHandle,-(long)(sizeof(desc)),SEEK_END);
	size_t endOfDesc=_tell(selfFileHandle);
	_read (selfFileHandle, (char*)&desc, sizeof(desc) );

	LOG_DEBUG(L"Descriptor read %d %d %d %d %d %d",desc.crc32, desc.magicNum,desc.block1Offset, desc.block2Offset,  desc.varDescOffset, desc.version);

	if(desc.magicNum== ND_MAGIC_NUM&&desc.version==ND_VER)
	{
		if(cmd.find(block2)!=cmd.npos)
		{
			wstring extractFromPath;
			wstring targetPath(pathStr.substr(0,pathStr.find_last_of(L"/\\")));
			for(int i=0;i<nArgs-1;i++)
			{
				if(block2Path.compare(arglist[i])==0)
				{
					extractFromPath=arglist[i+1];
				}
				else if( block2Target.compare(arglist[i])==0)
				{
					targetPath=arglist[i+1];
				}

			}
			if(extractFromBlock2(selfFileHandle,desc.block2Offset,desc.varDescOffset,targetPath,extractFromPath))
			{
				//LOG_ERROR(L"Could not extract block2");
			}
		}
		else
		{
			if(SetEnvironmentVariable(L"DUCK_EXE",path)==0)
			{
				LOG_ERROR(L"Could not set exe var %s",GetLastErrorStdStrW().c_str());
				return -1;
			}
			string exeCmd;

			char buf[2048] = { 0 };
			_lseek(selfFileHandle, desc.varDescOffset, SEEK_SET);
			_read(selfFileHandle, (char*)&buf, endOfDesc - desc.varDescOffset);
			//string s(buf);
			//exeCmd = wstring(s.begin(), s.end());
			boost::property_tree::ptree pt;
			boost::property_tree::read_json(imemstream(buf, 2048), pt);
			exeCmd = pt.get<std::string>("run");
			
			if(extractAndRunBlock1(selfFileHandle,desc.block1Offset,desc.block2Offset, exeCmd))
			{
				LOG_ERROR(L"Could not extract block1");
			}
		}
	}
	else
	{
		LOG_ERROR(L"Installer file corrupted");
		return -1;
	}
}