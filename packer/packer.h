
#ifndef COMPILER_H
#define COMPILER_H

#include <string>
#include <Windows.h>
#include <vector>
#include "../peml/peml/include/peml.h"
using namespace std;

class Config{
public:

	wstring outFilePath;
	wstring block1DirPath;
	wstring block2DirPath;

	pemetalib::ExeResources resources;
	
	wstring argIconPath;
	bool noAdmin;


	wstring ndDescPath;

	Config() : noAdmin(false) {}
private:
	
};


wstring GetLastErrorStdStrW();

class LengthedStructFiekd;




static bool logVerbose;

#define LOG_ERROR(...) NDLog( false , __VA_ARGS__ )
#define LOG_DEBUG(...) NDLog( true , __VA_ARGS__ )

static void NDLog(bool onlyVerbose,const wchar_t* format, ...)
{
	if(onlyVerbose && !logVerbose)return;
	va_list argptr;
	va_start(argptr, format);
	vfwprintf(stdout, format, argptr);
	va_end(argptr);
	fwprintf(stdout,L"\n");
};

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

	// return new string ( compiler should optimize this away )
	return ret;
}


bool CompileExtractor(const wstring& outPath,const wstring& zipPath,const wstring& zipBlock2Path,const wstring& varDescPath, pemetalib::ExeResources res);
bool CompressDir(const wstring& srcPath,const wstring & destZipPath);
wstring getTempFilePath();
wstring getWorkingDirectory();
#endif /*COMPILER_H*/