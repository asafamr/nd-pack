#include "packer.h"
#include "resource.h"
#include "../common/duckpack.h"
#include "peml.h"

#include <fstream>
//#include <iostream>
//#include <string.h>
class LengthedStructFiekd;


bool WriteExtractorDataToOutfile(const wstring& outPath)
{
	LOG_DEBUG(L"Writing extractor to %s",outPath.c_str());
	//get a module handle to our compiler process and load the extractor which is embedded as a resource
	HMODULE moduleHandle = GetModuleHandle(NULL);
	HRSRC rc = FindResource(moduleHandle, MAKEINTRESOURCE(IDR_EXTRACTOR),MAKEINTRESOURCE(RAWFILE));
	HGLOBAL rcData = LoadResource(moduleHandle, rc);
	
	UINT extractorSize = SizeofResource(moduleHandle, rc);
	const char * data = static_cast<const char*>(LockResource(rcData));

	// write the extractor to the out path
	HANDLE hFile = CreateFileW (outPath.c_str(),      
		GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); 
	if(hFile==INVALID_HANDLE_VALUE)
	{
		LOG_ERROR(L"Could not create file %s - %s",outPath.c_str(),GetLastErrorStdStrW().c_str() );
		return false;
	}
	DWORD sizeWritten;
	BOOL writeSucceeded=WriteFile(hFile,data,extractorSize,&sizeWritten,NULL);
	if(!writeSucceeded||sizeWritten!= extractorSize)
	{
		LOG_ERROR(L"Could not write to %s - %s",outPath.c_str(),GetLastErrorStdStrW().c_str());
		CloseHandle(hFile);
		DeleteFileW(outPath.c_str());
		return false;
	}
	CloseHandle(hFile);
	return true;
}
void PemlLog(std::wstring& msg, int level, int code)
{
	if (level == 0)
	{
		LOG_DEBUG(msg.c_str());
	}
	else
	{
		LOG_ERROR(msg.c_str());
	}
	
}
bool CompileExtractor(const wstring& outPath,const wstring& zipPath,const wstring& zipBlock2Path,const wstring& varDescPath, pemetalib::ExeResources res)
{

	//write the extractor to out path
	if(!WriteExtractorDataToOutfile(outPath))return false;


	pemetalib::LogCallback logcb;
	logcb = &PemlLog;
	pemetalib::SetLogCallback(logcb);
	pemetalib::UpdateExeResources(res, outPath);

	//append zip file 
	static const int buffSize=1024*8;

	char bufferRd[buffSize];
	char bufferWr[buffSize];

	ofstream ofs;
	ifstream ifs;
	ofs.rdbuf()->pubsetbuf(bufferWr, buffSize);
	ifs.rdbuf()->pubsetbuf(bufferRd, buffSize);
	ifs.open (zipPath.c_str(), ifstream::in | ifstream::binary);
	//int a = errno;
	//cout << strerror_s( bufferRd, buffSize, errno);
	ofs.open (outPath.c_str(), ofstream::out | ofstream::app| ofstream::binary);
	ofs.seekp(0, ios_base::end);//tellp doesn't work right otherwise
	__int64 zipOffset=ofs.tellp();
	__int64 zipOffset2=0;
	__int64 varDescOffset=0;
	ofs<<ifs.rdbuf();

	if(zipBlock2Path!=L"")//TODO:DRY
	{
		ifs.close();
		zipOffset2=ofs.tellp();
		ifs.open (zipBlock2Path.c_str(), ofstream::in | ofstream::binary);
		if (!ifs.good() || !ifs.is_open())
		{
			LOG_ERROR(L"Could not write block2");
			return false;
		}
		ifs.seekg(0, ios_base::end);
		__int64 fileSize = ifs.tellg();
		if (fileSize == 0)
		{
			LOG_ERROR(L"Block2 file empty");
			return false;
		}
		ifs.seekg(0, ios_base::beg);
		ofs<<ifs.rdbuf();

	}
	if(varDescPath!=L"")
	{
		ifs.close();
		varDescOffset=ofs.tellp();
		ifs.open (varDescPath.c_str(), ofstream::in | ofstream::binary);
		if(!ifs.good() || !ifs.is_open())
		{
			LOG_ERROR(L"Could not write desc block");
			return false;
		}
		ifs.seekg(0, ios_base::end);
		__int64 fileSize = ifs.tellg();
		if (fileSize == 0)
		{
			LOG_ERROR(L"Desc file empty");
			return false;
		}
		ifs.seekg(0, ios_base::beg);
		ofs << ifs.rdbuf();

	}
	

	//append descriptor
	DuckFileDescriptor desc={0};
	desc.magicNum=ND_MAGIC_NUM;
	desc.version=ND_VER;
	desc.block1Offset=zipOffset;
	desc.block2Offset=zipOffset2;
	desc.varDescOffset=varDescOffset;
	//desc.crc32=TO IMPLEMENT
	int before=  ofs.tellp();
	ofs.write((char*)&desc,sizeof(desc));
	int after =  ofs.tellp();
	ofs.close();
	ifs.close();
	return true;
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

wstring getTempFilePath()
{
	wchar_t bufferTempDir[MAX_PATH];
	wchar_t bufferTempFile [MAX_PATH];

	if (GetTempPathW (MAX_PATH, bufferTempDir) == 0)
	{
		return L"";
	}
	UINT uRetVal = GetTempFileNameW(bufferTempDir,L"DEMO",0,bufferTempFile);
	return wstring(bufferTempFile);
}

wstring getWorkingDirectory()
{
	TCHAR NPath[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, NPath);
	return wstring(NPath);
}