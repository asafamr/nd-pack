#ifndef DUCKPACK_H
#define DUCKPACK_H

#define XSTR(s) STR(s)
#define STR(s) #s


#ifndef _WIN64
 #define BITCOUNT "32"
#else
 #define BITCOUNT "64"
#endif


struct DuckFileDescriptor{
	DWORD magicNum;
	DWORD version;
	__int64 block1Offset;
	__int64 block2Offset;
	__int64 varDescOffset;
	DWORD crc32;//TODO:implement
};

template <typename T>
class ScopedVar 
{
private:
	T* var;
public:
	ScopedVar(T* newVar){var= newVar;}
	~ScopedVar(){delete var;}
	T* get(){return var;}
};

template <typename T>
class ScopedArrVar 
{
private:
	T* var;
public:
	ScopedArrVar(void* newVar){var= (T*)newVar;}
	~ScopedArrVar(){delete [] var;}
	T* get(){return var;}
};

#define ND_MAGIC_NUM 0x0723A5EF
#define ND_VER 1

#define LIB_SUFFIX XSTR(_MSC_VER)XSTR("_")XSTR(BITCOUNT)XSTR(".lib")



//#define EXTRACTOR_NAME "extractor_" XSTR(_MSC_VER) XSTR("_") XSTR(BITCOUNT) XSTR(".exe")


//required vs2010 settings: set out dir of both projects to $(SolutionDir)..\bin\$(Platform)\$(Configuration)\  
//set the packer resource include to $(SolutionDir)..\bin\$(Platform)\$(Configuration)\ 
//set the packer dependent on extractor



#endif //DUCKPACK_H


int duck_read_open_fd(struct archive *a, int fd, size_t block_size,size_t startOffset,size_t endOffset);