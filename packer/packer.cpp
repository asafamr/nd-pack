// compiler.cpp : Defines the entry point for the console application.
//


#include "packer.h"
#include "windows.h"
#include <tchar.h>
#include <sstream>
#include <algorithm>	
#include "../peml/peml_utils/peml_utils.h"

using namespace std;






bool isArgGiven(vector<wstring> &args, wstring token)
{
	return find(args.begin(), args.end(), token)!=args.end();
}

bool pack(Config &config)
{
	wstring zipPathBlock1=getTempFilePath();

	if (!CompressDir(config.block1DirPath, zipPathBlock1))
	{
		LOG_ERROR(L"CreateDuckZipFromDir failed");
		return false;
	}

	
	wstring  zipPathBlock2 ;

	if (!config.block2DirPath.empty())
	{
		zipPathBlock2 = getTempFilePath();
		CompressDir(config.block2DirPath, zipPathBlock2);
	}


	if (!CompileExtractor(config.outFilePath, zipPathBlock1, zipPathBlock2, config.ndDescPath, config.resources))
	{
		LOG_DEBUG(L"Deleting out file...");
		DeleteFile(config.outFilePath.c_str());
		DeleteFile(zipPathBlock1.c_str());//TODO:DRY
		if (!zipPathBlock2.empty())
		{
			DeleteFile(zipPathBlock2.c_str());
		}
		return true;
	}
	DeleteFile(zipPathBlock1.c_str());
	if (!zipPathBlock2.empty())
	{
		DeleteFile(zipPathBlock2.c_str());
	}
	return true;
}
int _tmain(int argc, _TCHAR* argv[])
{
	logVerbose = true;
	vector<wstring> arguments(argv + 1, argv + argc);
	Config config;
	//setup log filter (--verbose command line arg)
	logVerbose =isArgGiven(arguments,L"--verbose");
	LOG_DEBUG(L"ND compiler started...");

	LOG_DEBUG(L"Working dir: %s", getWorkingDirectory().c_str());
	peutils::Utils utils;
	if (!utils.parseCommandLineArgs(argc, argv, config.resources,
		vector<peutils::AdditionalArgument>{
		peutils::AdditionalArgument("out", "Output file", &config.outFilePath, peutils::AdditionalArgument::FILE, true),
			peutils::AdditionalArgument("block1", "Main SFX content", &config.block1DirPath, peutils::AdditionalArgument::EXISTING_DIR, true),
			peutils::AdditionalArgument("block2", "Additional compressed content", &config.block2DirPath, peutils::AdditionalArgument::EXISTING_DIR, false),
			peutils::AdditionalArgument("no-admin", "No admin privliges needed", &config.noAdmin, peutils::AdditionalArgument::BOOL, false),
			peutils::AdditionalArgument("verbose", "Log debug messages", &logVerbose, peutils::AdditionalArgument::BOOL, false),
			peutils::AdditionalArgument("nd-desc", "ND descriptor", &config.ndDescPath, peutils::AdditionalArgument::EXISITNG_FILE, false)
	}))
	{
		LOG_ERROR(utils.getParseError().c_str());
		return -1;
	}
		if (config.resources.manifest == L"" && !config.noAdmin)
		{
			config.resources.manifest =
				L"<assembly xmlns = \"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
				L"  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
				L"    <security>\r\n"
				L"      <requestedPrivileges>\r\n"
				L"      <requestedExecutionLevel level=\"requireAdministrator\" uiAccess=\"false\"></requestedExecutionLevel>\r\n"
				L"      </requestedPrivileges>\r\n"
				L"    </security>\r\n"
				L"  </trustInfo>\r\n"
				L"</assembly>";
		}
	if (!pack(config))
	{
		return -1;
	}

	
	LOG_DEBUG(L"Done.");
	return 0;
}

