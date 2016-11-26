#pragma once

#include "API\defs.h"
#include <list>
typedef std::basic_string<OIVCHAR> tstring;
typedef std::list<tstring> ListFiles;
typedef ListFiles::iterator ListFilesIterator;

class Utility
{
public:
    static tstring GetExePath();

    static tstring GetExeFolder();

    static tstring GetAppDataFolder();

    static void EnsurePath(const tstring& path);

    static bool DirectoryExists(const tstring& path);

    static void find_files(tstring wrkdir, ListFiles &lstFileData, bool recursive = false);
    
    static size_t GetFileSize(const wchar_t* fileName);
    
};