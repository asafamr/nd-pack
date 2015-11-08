#include "packer.h"
#include "resource.h"
#include "../common/duckpack.h"

#include <windows.h>

#include "Shlwapi.h"

#define LIBARCHIVE_STATIC
extern "C" 
{
#include "../common/lib_archive/include/archive.h"
#include "../common/lib_archive/include/archive_entry.h"
}

#pragma comment(lib,"../common/lib_archive/lib/libarchive_" LIB_SUFFIX)
#pragma comment(lib,"../common/zlib/lib/zlib_" LIB_SUFFIX)
 

//#pragma comment(lib,"shlwapi.lib")

using namespace std;
#define BUFSIZE 4096
wstring getNormalizedAbsoluteFilePath(const wstring& rel)
{
	WCHAR  buffer[BUFSIZE] = TEXT("");
	WCHAR  buf[BUFSIZE] = TEXT("");
	//WCHAR** lppPart = { NULL };
	int ret = GetFullPathNameW(rel.c_str(),
		BUFSIZE,
		buffer,
		NULL);
	wstring absolutePath(buffer);
	//absolutePath += L"\\";
	if (!ret)
	{
		LOG_ERROR(L"Could not get absolute path of %s", rel.c_str());
	}
	return absolutePath;
}

//base dir should be absolute
bool getRelativePathFromPath(const wstring& dest,const wstring& baseDir,wstring& out)
{//TODO:move to win specific and crossplatofmize
	
	WCHAR szOut[BUFSIZE] = L"";
	wstring baseDirWithDash = baseDir + L'\\';
	// Retrieve the full path name for a file. 
	// The file does not need to exist.

	
	struct _stat statStruct;
	int ret = _wstat(dest.c_str(), &statStruct);
	bool isDir = false;
	if (ret)
	{
		LOG_ERROR(L"Could not stat %s", dest.c_str());
	}
	else
	{
		isDir = statStruct.st_mode & S_IFDIR != 0;
	}

	ret=PathRelativePathTo(szOut, baseDirWithDash.c_str(), isDir?FILE_ATTRIBUTE_DIRECTORY:NULL,dest.c_str(),FILE_ATTRIBUTE_NORMAL);
	if(ret)
	{
		//PathRelativePathTo adds .\ prefix - remove it
		if(szOut[0]==L'.' && szOut[1]=='\\')
		{
			out=szOut+2;
		}
		else
		{
			out=szOut;
		}
		
		return true;
	}
	return false;
}
class ScopedArchiveEntry
{
private:
	struct archive_entry *entry;
public:
	ScopedArchiveEntry(const ScopedArchiveEntry* e)
	{
		if(e==NULL)
		{
			entry=NULL;
		}
		else
		{
			entry=archive_entry_clone(e->entry);
		}
		
	}
	ScopedArchiveEntry()
	{
		entry=archive_entry_new();
	}
	~ScopedArchiveEntry()
	{
		if(entry)
		{
			archive_entry_free(entry);
		}
		entry=NULL;
	}
	archive_entry* get()
	{
		return entry;
	}
};

class ScopedArchive
{
protected:
	struct archive *arch;
	void logError(const wstring & fromFunc)
	{
		string archiveError(archive_error_string(arch));
		wstring wideError(convertToWideString(archiveError));
		LOG_ERROR(L"%s : %s",fromFunc.c_str(),wideError.c_str());
	}
	//virtual void closeAndFreeArch()=0;
public:
	ScopedArchive():arch(NULL){}
	/*virtual ~ScopedArchive()
	{
		if(arch)
		{
			closeAndFreeArch();
		}
	}*/
	archive * get()
	{
		return arch;
	}
	
};
class ZipWriteArchive:ScopedArchive
{
protected:
	void closeAndFreeArch()
	{
		if(arch)
		{
			archive_write_close(arch);
			archive_write_free(arch);
		}
	}
public:
	~ZipWriteArchive(){closeAndFreeArch();}
	bool init(const wstring&zipDestPath)
	{
		arch = archive_write_new();
		if(arch==NULL)
		{
			LOG_ERROR(L"archive_write_new failed");
			return false;
		}
		if(archive_write_add_filter_gzip(arch)!=ARCHIVE_OK)
		{
			logError(L"init-archive_write_add_filter_gzip");
			return false;
		}
		if(archive_write_set_format_pax_restricted(arch)!=ARCHIVE_OK)
		{
			logError(L"init-archive_write_set_format_pax_restricted");
			return false;
		}
		
		if(archive_write_open_filename_w(arch, zipDestPath.c_str())!=ARCHIVE_OK)
		{
			logError(L"init-archive_write_open_filename_w");
			return false;
		}
		return true;
	}
	bool writeHeader(archive_entry * entry)
	{
		int r = archive_write_header(arch, entry);
		if (r == ARCHIVE_FATAL)
		{
			logError(L"writeHeader ARCHIVE_FATAL");
			return false;
		}
		else if (r <= ARCHIVE_FAILED) {
			logError(L"writeHeader ARCHIVE_FAILED ");
			return false;
		}
		else if (r < ARCHIVE_OK) 
		{
			logError(L"writeHeader < ARCHIVE_OK");
			return false;
		}
		return true;
	}
	bool writeFile(const wstring& path)
	{
		char buff[8096];
		wchar_t errbuff[2048];
		FILE* fd ;
		errno_t err=_wfopen_s(&fd,path.c_str(), L"rb");
		if(err)
		{
			LOG_ERROR(L"ZipWriteArchive.writeFile _wfopen_s failed (%d - %s)",err,_wcserror_s(errbuff,err));
			return false;
		}
		size_t len = fread( buff,1,sizeof(buff),fd);
		while (len>0) 
		{
			archive_write_data(arch, buff, len);
			len = fread(buff,1,sizeof(buff),fd);
		}
		if(err = fclose(fd))
		{
			LOG_ERROR(L"ZipWriteArchive.writeFile Could not close file (%d - %s)",err, _wcserror_s(errbuff, err));
		}
		return true;
	}
	bool writeData(char* buff,int len)
	{
		return len==archive_write_data(arch, buff, len);
	}
};

class DiskReadArchive:ScopedArchive
{
	protected:
		void closeAndFreeArch()
		{
			if(arch)
			{
				archive_read_close(arch);
				archive_read_free(arch);
			}
		}
	//the archive_read_disk locks the current file so we always serve the previous
	auto_ptr<ScopedArchiveEntry> lastArchiveEntry;
	auto_ptr<ScopedArchiveEntry> curArchiveEntry;
	bool reachedEnd;
	
	bool advanceEntry()
	{
		//TODO This is ugly - make it better
		//move the pointer from curArchiveEntry to lastArchiveEntry (dont delete)
		lastArchiveEntry.reset(curArchiveEntry.get());
		curArchiveEntry.release();
		if(reachedEnd)return true;
		
		curArchiveEntry.reset(new ScopedArchiveEntry) ;
		int r =archive_read_next_header2(arch,curArchiveEntry->get());
		if (r == ARCHIVE_EOF)
		{
			reachedEnd=true;
			curArchiveEntry.reset(NULL);
		}
		else if (r != ARCHIVE_OK)
		{
			logError(L"updateNextEntry");
			return false;
		}
		//makes sure to recursively find all dirs
		archive_read_disk_descend(arch);
		return true;
	}

public:
	~DiskReadArchive(){closeAndFreeArch();}
	const ScopedArchiveEntry * getNextEntryAndAdvnce()
	{
		if(advanceEntry()==false)
		{
			logError(L"getNextEntryAndAdvnce failed");
			return NULL;
		}
		return lastArchiveEntry.get();
	}
	
	bool init(const wstring&srcDir)
	{
		reachedEnd=false;
		arch=archive_read_disk_new();
		if(arch==NULL)
		{
			LOG_ERROR(L"archive_read_disk_new failed");
			return false;
		}
		if(archive_read_disk_open_w(arch,srcDir.c_str())!=ARCHIVE_OK)
		{
			logError(L"DiskReadArchive.init");
			return false;
		}
		//due to what said above advanceEntry 
		return advanceEntry();
	}
	bool hasNextEntry()
	{
		return !reachedEnd;
	}
	

};
bool CompressDir( const wstring& srcPath,const wstring & destZipPath )
{
	if(srcPath.empty())
	{
		LOG_ERROR(L"createDuckZipFromDir srcPath is empty");
		return false;
	}

	
	wstring normailzedSrcPath = getNormalizedAbsoluteFilePath(srcPath);
	
	DiskReadArchive readArch;
	if(!readArch.init(normailzedSrcPath))return false;
	ZipWriteArchive writeArch;
	if(!writeArch.init(destZipPath))return false;
	
	while(readArch.hasNextEntry())
	{
		ScopedArchiveEntry entry(readArch.getNextEntryAndAdvnce());
		if(entry.get()==NULL)
		{
			LOG_ERROR(L"Got NULL ScopedArchiveEntry");
			return false;
		}
		wstring oldName(archive_entry_sourcepath_w(entry.get() ));

		//libarchive adds a //?/ prefix that should support long paths but we'll remove it
		if(oldName.length()>4 && oldName.substr(0,4)==L"\\\\?\\" )
		{
			oldName=oldName.substr(4);
		}

		if(oldName==normailzedSrcPath)
		{
			continue;
		}

		wstring relativePath;
		getRelativePathFromPath(oldName,normailzedSrcPath, relativePath);

		archive_entry_copy_pathname_w(entry.get(),relativePath.c_str());
		writeArch.writeHeader(entry.get());
		if(archive_entry_size(entry.get())>0)
		{
			if(!writeArch.writeFile(oldName))
			{
				return false;
			}
		}
		
	}

	//write .duck file - command line to be executed when unpacked
	//TODO if ported to other compilers: some of these depend on the wstring implementation and os
	//wstring duckFileContent=L"duck_file_ver=1.0.0\nexe_file="+exeCmd;
	//WORD utf16TextFileHeader=0xFEFF;
	//DWORD eofDword=0x000A000D;

	/*
	ScopedArchiveEntry sae;
	DWORD lengthOfStr=exeCmd.length();
	DWORD duckVer=DUCK_VER;

	archive_entry_set_pathname(sae.get(),".duck");
	archive_entry_copy_pathname_w(sae.get(),L".duck");
	archive_entry_set_size(sae.get(), exeCmd.length()*sizeof(wchar_t)+sizeof(lengthOfStr)+sizeof(duckVer));
	archive_entry_set_filetype(sae.get(), AE_IFREG);
	//archive_entry_set_perm(sae.get(), 0644);

	writeArch.writeHeader(sae.get());
	
	//writeArch.writeData((char*)&utf16TextFileHeader,sizeof(utf16TextFileHeader));
	writeArch.writeData((char*)&duckVer,sizeof(duckVer));
	writeArch.writeData((char*)&lengthOfStr,sizeof(lengthOfStr));
	writeArch.writeData((char*)exeCmd.c_str(),exeCmd.length()*sizeof(wchar_t));
	//writeArch.writeData((char*)&eofDword,sizeof(eofDword));
	*/
	return true;
}