// directory.cc
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"
#include "debug.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];

    // MP4 mod tag
    memset(table, 0, sizeof(DirectoryEntry) * size); // dummy operation to keep valgrind happy

    tableSize = size;
    for (int i = 0; i < tableSize; i++)
        table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{
    delete[] table;
}

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void Directory::FetchFrom(OpenFile *file) //讀取file(now directory)
{
    (void)file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
    // DEBUG('f', "Finish Directory::FetchFrom");
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void Directory::WriteBack(OpenFile *file)
{
    (void)file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++) //找是否有這個檔名的檔案 有就回傳位置 無就回傳-1
        if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
            return i;
    return -1; // name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::Find(char *name)
{
    char *findCur;
    char cpyName[256]; //最大可以到256
    strcpy(cpyName, name); //將現在的名字複製
    if (name[0] == '/') findCur = strtok(cpyName+1, "/"); //分段並取當前第一個為需要到達的位置
    else findCur = strtok(cpyName, "/");
    if (findCur == NULL) findCur = cpyName; // last in path
    int i = FindIndex(findCur); //找是否有這個檔案 若是沒有會回傳-1

    if (i != -1) { //找到了 需要繼續往下找
        char findNxt[256]; //存下一層
        if (strlen(name) - (strlen(findCur) + 1) == 0) return table[i].sector;
        // return if the current is the last //已經是最後一層了 可以return

        strcpy(findNxt, name + strlen(findCur) + 1); //將下一層的目錄複製到findNxt

        if (!table[i].isDir) return -1; //找到的不是目錄也不是檔案
        Directory* subDir = new Directory(NumDirEntries);
        OpenFile* dirFile = new OpenFile(table[i].sector);
        subDir->FetchFrom(dirFile);  //讀取該檔案資訊
        int findSec = subDir->Find(findNxt);
        delete dirFile;
        delete subDir;
        return findSec;
    }
    return -1; //沒找到
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool Directory::Add(char *name, int newSector, bool isDir)
{
    DEBUG('f', "Adding path " << name);
    char *findCur, cpyName[256];
    strcpy(cpyName, name); //先複製
    if (name[0] == '/') findCur = strtok(cpyName+1, "/"); //一個一個讀取
    else findCur = strtok(cpyName, "/");
    if (findCur == NULL) findCur = cpyName; // last in path

    char findNxt[256];
    int i; //已經存在了 不能再新增
    if ((i = FindIndex(findCur)) != -1 && strlen(name) - (strlen(findCur) + 1) == 0) {
        DEBUG('f', "Already exist and can't fit target name anymore: " << name);
        return FALSE;
    }
    // cout << findCur << " " << findNxt << endl;
    
    //還要繼續往下深入
    if (strlen(name) - (strlen(findCur) + 1) > 0) {
        // *(findNxt+strlen(findNxt)) = '/';
        // while (strtok(NULL, "/"));
        strcpy(findNxt, name + strlen(findCur) + 1); //往下遞迴
        Directory* subDir = new Directory(NumDirEntries);
        OpenFile* dirFile = new OpenFile(table[i].sector);
        subDir->FetchFrom(dirFile);
        bool success = subDir->Add(findNxt, newSector, isDir);
        subDir->WriteBack(dirFile);
        delete dirFile;
        delete subDir;
        return success;
    }

    //在目前的檔案夾中新增檔案
    for (int i = 0; i < tableSize; i++)
        if (!table[i].inUse) //在空的table放入檔案
        {
            table[i].inUse = TRUE;
            strncpy(table[i].name, findCur, FileNameMaxLen);
            table[i].sector = newSector;
            table[i].isDir = isDir;
            if (isDir) {
                DEBUG('f', "Create sub-dir " << table[i].name << " " << table[i].sector);
                // Directory* subDir = new Directory(NumDirEntries);
                // OpenFile* dirFile = new OpenFile(table[i].sector);
                // subDir->WriteBack(dirFile);
                // delete subDir;
                // delete dirFile;        
            }
            else DEBUG('f', "Create file " << table[i].name << " " << table[i].sector);
            // DEBUG('f', "Finish creating file " << table[i].name);
            
            return TRUE;
        }
    return FALSE; // no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory.
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool Directory::Remove(char *name)
{
    char *findCur, cpyName[256];
    strcpy(cpyName, name); //一樣先取名字
    if (name[0] == '/') findCur = strtok(cpyName+1, "/");
    else findCur = strtok(cpyName, "/");
    if (findCur == NULL) findCur = cpyName; // last in path
    int i = FindIndex(findCur);

    if (i == -1 && strlen(name) - (strlen(findCur) + 1) == 0)
        return FALSE; // name not in directory

    char findNxt[256];
    if (strlen(name) - (strlen(findCur) + 1) > 0) { //深入資料夾繼續找
        // DEBUG('f', "Deleting ")
        // *(findNxt+strlen(findNxt)) = '/';
        // while (strtok(NULL, "/"));
        // findNxt = name + strlen(findCur) + 1;
        strcpy(findNxt, name + strlen(findCur) + 1);
        Directory* subDir = new Directory(NumDirEntries);
        OpenFile* dirFile = new OpenFile(table[i].sector);
        subDir->FetchFrom(dirFile);
        bool success = subDir->Remove(findNxt);
        subDir->WriteBack(dirFile);
        delete dirFile;
        delete subDir;
        return success;
    }
    
    table[i].inUse = FALSE;
    return TRUE;
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory.
//----------------------------------------------------------------------

void Directory::List()
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse)
            printf("%s\n", table[i].name);
            // printf("%s %d\n", table[i].name, table[i].sector);
}

//----------------------------------------------------------------------
// Directory::RecursiveList
// 	List all the file names in the directory.
//----------------------------------------------------------------------
//以遞迴方式將目前 directory 之下的 file 和 subdirectory 全部 print 出來。
//依 lvl 作為目前 subdirectory 的層數，在該 subdirectory 之下的 file 和 directory 前做 padding
void Directory::RecursiveList(int lvl)
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse) {
            for (int j=0; j<lvl; j++) printf("    "); // padding
            if (table[i].isDir) {
                printf("[D] %s\n", table[i].name);
                OpenFile *subDirFile = new OpenFile(table[i].sector);
                Directory *subDir = new Directory(NumDirEntries);
                subDir->FetchFrom(subDirFile);
                subDir->RecursiveList(lvl+1);
                delete subDirFile;
                delete subDir;
            }
            else 
                printf("[F] %s\n", table[i].name);
        }
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void Directory::Print()
{
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse)
        {
            printf("Name: %s, Sector: %d\n", table[i].name, table[i].sector);
            hdr->FetchFrom(table[i].sector);
            hdr->Print();
        }
    printf("\n");
    delete hdr;
}


