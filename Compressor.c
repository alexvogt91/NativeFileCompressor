#include <Windows.h>
#include <stdio.h>
#include "ntddk.h"


/*
Native file compressor using only Native Apis @ NTDLL.dll
*/


/*//////////////////////////////////////
/
/	Function: CompressBuffer
/
/	Purpose:
/		Compress buffer given some data
/
*////////////////////////////////////////


#define STATUS_UNSUPPORTED_COMPRESSION ((NTSTATUS)0xC000025FUL)
#define STATUS_SUCCESS                 ((NTSTATUS)0x00000000UL)
#define NtCurrentProcess() ( (HANDLE)(LONG_PTR) -1 ) 

#define SIZE_NUMB 16

unsigned char *CompressBuffer(unsigned char *Buf, size_t sizeofbuffer, unsigned long *outsize)
{
	NTSTATUS st = 0;
	unsigned char *container = NULL;
	unsigned char *mem = NULL;
	unsigned long size1 = 0, size2 = 0;
	size_t Size = sizeofbuffer * SIZE_NUMB;

	// Get Compression Size

	st = RtlGetCompressionWorkSpaceSize(COMPRESSION_FORMAT_LZNT1 | COMPRESSION_ENGINE_MAXIMUM, &size1, &size2);
	if (st == STATUS_UNSUPPORTED_COMPRESSION || st == STATUS_INVALID_PARAMETER)
		return NULL;

	// Allocate both buffers

	container = (unsigned char*)RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, (unsigned long)Size);
	mem = (unsigned char*)RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, (unsigned long)size1);

	if (container == 0 || mem == 0)
		return NULL;

	// Compress Buffer

	st = RtlCompressBuffer(
		COMPRESSION_FORMAT_LZNT1 |
		COMPRESSION_ENGINE_MAXIMUM,
		Buf,
		sizeofbuffer,
		container,
		Size,
		0x1000,
		outsize,
		mem);
	if (NT_SUCCESS(st))
	{
		free(mem);
	}

	return container; //return de compressed data

}



int wmain(int argc, wchar_t *argv[])
{
	OBJECT_ATTRIBUTES Obja1, Obja2;
	UNICODE_STRING sourcefile, destfile;
	HANDLE openhandle, writehandle;
	IO_STATUS_BLOCK io, io2;
	LARGE_INTEGER large, large2;
	NTSTATUS st;
	FILE_STANDARD_INFORMATION fileinfo = { 0 };
	unsigned long outsize = 0;
	unsigned char *kmalloc = 0;

	RtlSecureZeroMemory(&sourcefile, sizeof(sourcefile));
	RtlSecureZeroMemory(&destfile, sizeof(destfile));


	RtlInitUnicodeString(&sourcefile, (wchar_t*)argv[1]);
	if (RtlDosPathNameToNtPathName_U(argv[1], &sourcefile, NULL, 0) == NULL)
	{
		wprintf(L"[!] Error converting string from Dos to NT: %d", RtlGetLastWin32Error());
		return -1;
	}


	InitializeObjectAttributes(&Obja1, &sourcefile, OBJ_CASE_INSENSITIVE, 0, 0);

	large.QuadPart = 2048;

	// Open File with read parameters

	st = NtCreateFile(&openhandle, FILE_GENERIC_READ, &Obja1, &io, &large, FILE_ATTRIBUTE_NORMAL, 
			  FILE_SHARE_READ, FILE_OPEN, 
			  FILE_NON_DIRECTORY_FILE | 
			  FILE_SYNCHRONOUS_IO_NONALERT, 
			  NULL, 
			  0);
	
	if (NT_SUCCESS(st))
	{
		wprintf(L"[+] File -> %ws Opened successfully\r\n", sourcefile.Buffer);
		wprintf(L"[+] Getting file size...\r\n");
		st = NtQueryInformationFile(openhandle, &io, &fileinfo, 
					    sizeof(FILE_STANDARD_INFORMATION), 
					    FileStandardInformation);	// GetFileSize @ win32 api
		if (NT_SUCCESS(st))
		{
			wprintf(L"[+] File size: %ld bytes\r\n", fileinfo.EndOfFile.QuadPart);

			// Allocate memory

			st = NtAllocateVirtualMemory(NtCurrentProcess(), 
						     (void**)&kmalloc, 
						     0, 
						     (unsigned long*)&fileinfo.EndOfFile.QuadPart, 
						     MEM_COMMIT | 
						     MEM_RESERVE, 
						     PAGE_READWRITE);
			
			if (NT_SUCCESS(st))
			{

				wprintf(L"[+] Memory allocated !\r\n");
				wprintf(L"[+] Reading file...\r\n");
				// Read file
				st = NtReadFile(openhandle, 
						NULL, 
						NULL, 
						NULL, 
						&io, 
						kmalloc, 
						fileinfo.EndOfFile.QuadPart,
						NULL, 
						0);		
				if (NT_SUCCESS(st))
				{
					wprintf(L"[+] File successfully read !\r\n");
					NtClose(openhandle);


					wprintf(L"[+] Compressing data...\r\n");
					// Compress data function
					unsigned char *GetBuffer = CompressBuffer(kmalloc, (size_t)fileinfo.EndOfFile.QuadPart, &outsize);	
					if (GetBuffer != NULL)
					{

						wprintf(L"[+] Data successfully compressed !\r\n");

						RtlInitUnicodeString(&destfile, (wchar_t*)argv[2]);

						// Again convert the second parameter to NT Format \\??\\

						if (RtlDosPathNameToNtPathName_U(argv[2], &destfile, NULL, 0) == NULL)
						{
							wprintf(L"[!] Error converting string from Dos to NT: %d", RtlGetLastWin32Error());
							return -1;
						}

						InitializeObjectAttributes(&Obja2, &destfile, OBJ_CASE_INSENSITIVE, NULL, NULL);
						large2.QuadPart = 2048;

						// Create file with right parameters 

						st = NtCreateFile(&writehandle,
								  FILE_GENERIC_WRITE, 
								  &Obja2, 
								  &io2, 
								  &large2, 
								  FILE_ATTRIBUTE_NORMAL, 
								  FILE_SHARE_WRITE, 
								  FILE_CREATE, 
								  FILE_NON_DIRECTORY_FILE | 
								  FILE_SYNCHRONOUS_IO_NONALERT,
								  NULL, 
								  0);
						if (NT_SUCCESS(st))
						{
							wprintf(L"[+] File -> %ws Created successfully\r\n", destfile.Buffer);
							wprintf(L"[+] Writing data to file...\r\n");

							// Write compressed data to new file

							st = NtWriteFile(
								writehandle, 
								NULL, 
								NULL,
								NULL, 
								&io2, 
								GetBuffer, 
								outsize, 
								NULL, 
								0);
							if (NT_SUCCESS(st))
							{
								wprintf(L"[+] File filled with data !\r\n");

								unsigned long prebytes = (unsigned long)fileinfo.EndOfFile.QuadPart / 1024;
								unsigned long finbytes = outsize / 1024;

								wprintf(L"[+] Previous file size: %ld KiloBytes\r\n", prebytes);
								wprintf(L"[+] Final size: %ld KiloBytes\r\n", finbytes);

								unsigned long dif = prebytes - finbytes;
								wprintf(L"[+] Total bytes compressed: %ld KiloBytes\r\n", dif);

								NtClose(writehandle);
							}

						}

					}
				}

			}

		}
	}

	system("PAUSE");

	return EXIT_SUCCESS;

}
