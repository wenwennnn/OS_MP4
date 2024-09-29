// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	//MP4-2
	// nextFileHeader = NULL;
	nextFileHeaderSector = -1;

	numBytes = -1;
	numSectors = -1;
	memset(dataSectors, -1, sizeof(dataSectors));
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{
	// nothing to do now
	//MP4-2
	// if(nextFileHeader != NULL) delete nextFileHeader;
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//	
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{
	//MP4-2
	// numBytes = fileSize; //需要檢查numbytes是否小於現在的最大直
	//std::cout << "in allocate\n" ;
	if(fileSize <= MaxFileSize) numBytes = fileSize;
	else numBytes = MaxFileSize;
	DEBUG('f', "Allocate " << fileSize << " file header");
	numSectors = divRoundUp(numBytes, SectorSize);
	if (freeMap->NumClear() < numSectors)
		return FALSE; // not enough space

	for (int i = 0; i < numSectors; i++)
	{
		dataSectors[i] = freeMap->FindAndSet();
		DEBUG('f', "Allocate sector " << dataSectors[i]);

		// since we checked that there was enough free space,
		// we expect this to succeed
		ASSERT(dataSectors[i] >= 0);
	}

	//MP4-2
	// nextFileHeaderSector = -1;
	if(fileSize > MaxFileSize){
		nextFileHeaderSector = freeMap->FindAndSet();

		DEBUG('f', "Set next file header at sector " << nextFileHeaderSector);
		if(nextFileHeaderSector == -1) return false;

		FileHeader* nextFileHeader = new FileHeader;
		bool success = nextFileHeader->Allocate(freeMap, fileSize-MaxFileSize);
		nextFileHeader->WriteBack(nextFileHeaderSector);
		delete nextFileHeader;
		return success;
	}
	DEBUG('f', "Finish allocating header, size: " << numBytes << ", sectorNum: " << numSectors << ", next header: " << nextFileHeaderSector);

	//std::cout << "out allocate\n" ;
	return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(PersistentBitmap *freeMap)
{
	//	std::cout << "in deallocate \n";
	for (int i = 0; i < numSectors; i++)
	{
		ASSERT(freeMap->Test((int)dataSectors[i])); // ought to be marked!
		freeMap->Clear((int)dataSectors[i]);
	}
	//MP4-2
	if(nextFileHeaderSector != -1) {
		FileHeader* nextFileHeader = new FileHeader;
		nextFileHeader->FetchFrom(nextFileHeaderSector);
		nextFileHeader->Deallocate(freeMap);
		delete nextFileHeader;
	}
	//std::cout << "out deallocate \n";
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
	//std::cout << "in fetchfrom \n";
	// kernel->synchDisk->ReadSector(sector, (char *)this+sizeof(FileHeader*));
	kernel->synchDisk->ReadSector(sector, (char *)this);

	/*
		MP4 Hint:	
		After you add some in-core informations, you will need to rebuild the header's structure
	*/	
	//MP4-2
	//std::cout << nextFileHeaderSector << std::endl;
	// if(nextFileHeaderSector != -1){ //接下來還有東西需要讀取
	// 	DEBUG('f', "From sector " << sector << " find next header " << nextFileHeaderSector)
	// 	nextFileHeader = new FileHeader; //可能存在另一個block
	// 	nextFileHeader->FetchFrom(nextFileHeaderSector);
	// }
	//std::cout << "out fetchfrom \n";
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
	//MP4-2

	kernel->synchDisk->WriteSector(sector, ((char *)this));
	// if(nextFileHeaderSector != NULL) {
	// 	FileHeader* nextFileHeader = new FileHeader;
	// 	nextFileHeader->WriteBack(nextFileHeaderSector);
	// } //MP4-2

	//std::cout << "out write back \n";

	/*
		MP4 Hint:
		After you add some in-core informations, you may not want to write all fields into disk.
		Use this instead:
		char buf[SectorSize];
		memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
		...
	*/
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{
    int sector = offset / SectorSize;
    if (sector >= NumDirect) {
		FileHeader* nextFileHeader = new FileHeader;
		nextFileHeader->FetchFrom(nextFileHeaderSector);
		int b2sec = nextFileHeader->ByteToSector(offset - MaxFileSize); 
		delete nextFileHeader;
		return b2sec;
	}
    else return (dataSectors[sector]);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
	//return numBytes;
	// DEBUG('f', "Get file length: " << numBytes << ", next header: " << nextFileHeaderSector);
	if(nextFileHeaderSector == -1) return numBytes;
	else {
		FileHeader* nextFileHeader = new FileHeader;
		nextFileHeader->FetchFrom(nextFileHeaderSector);
		int length = numBytes + nextFileHeader->FileLength();
		delete nextFileHeader;
		return length;
	}
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print()
{
	int i, j, k;
	char *data = new char[SectorSize];

	printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	for (i = 0; i < numSectors; i++)
		printf("%d ", dataSectors[i]);
		printf("\n");
	// printf("\nFile contents:\n");
	// for (i = k = 0; i < numSectors; i++)
	// {
	// 	kernel->synchDisk->ReadSector(dataSectors[i], data);
	// 	for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
	// 	{
	// 		if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
	// 			printf("%c", data[j]);
	// 		else
	// 			printf("\\%x", (unsigned char)data[j]);
	// 	}
	// 	printf("\n");
	// }
	if(nextFileHeaderSector != -1) {
		FileHeader* nextFileHeader = new FileHeader;
		nextFileHeader->FetchFrom(nextFileHeaderSector);
		nextFileHeader->Print();
		delete nextFileHeader;
	} //MP4-2

	delete[] data;
}
