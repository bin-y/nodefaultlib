// nodefaultlib.cpp : 定义控制台应用程序的入口点。
//

#include <atlbase.h>
#include <atlfile.h>
#include <vector>
#include <string>

using std::vector;
using std::string;

#define ARCHIVE_PAD (IMAGE_ARCHIVE_PAD[0])

static const char* const g_ppszDefaultLibraryNames[] =
{
	"libc",
	"libcmt",
	"msvcrt",
	"libcd",
	"libcmtd",
	"msvcrtd"
};

#define DEFAULT_LIBRARY_NAME_NUM (sizeof(g_ppszDefaultLibraryNames) / sizeof(const char*))

static __inline BOOL IsCommonObject(const BYTE* pbObjectData);
static __inline BOOL IsImportObject(const BYTE* pbObjectData);
static __inline BOOL IsAnonymousObject(const BYTE* pbObjectData);
static void RemoveLinkerOptionFromCommonObject(BYTE* pbCoffData, const vector<string>& vLinkerOptionToRemove);
static void RemoveComplierOptionFromAnonymousObject(BYTE* pbCoffData, const vector<string>& vComplierOptionToRemove);
static BOOL MoveToNextMember(const BYTE* pbArchiveBase, const ULONGLONG& ullArchiveSize, ULONGLONG& ullOffset);

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc != 2)
	{
		_tprintf(_T("Usage:nodefaultlib filename\n"));
		_tprintf(_T("Example:nodefaultlib tommath.lib\n"));
		return 0;
	}

	CAtlFile TargetFile;
	if (FAILED(TargetFile.Create(argv[1], GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING)))
	{
		_ftprintf(stderr, _T("Unable to open file %s.\n"), argv[1]);
		return 0;
	}

	CAtlFileMappingBase TargetFileMap;
	if (FAILED(TargetFileMap.MapFile(TargetFile, 0, 0, PAGE_READWRITE, FILE_MAP_READ | FILE_MAP_WRITE)))
	{
		_ftprintf(stderr, _T("Unable to map file %s.\n"), argv[1]);
		return 0;
	}
	BYTE *pbFileData = (BYTE *)TargetFileMap.GetData();
	
	vector<string> vLinkerOptionToRemove;

	for (int i = 0; i < DEFAULT_LIBRARY_NAME_NUM; i++)
	{
		char szLinkerParameter[32];
		sprintf_s(szLinkerParameter, "/DEFAULTLIB:%s", g_ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:\"%s\"", g_ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:%s.lib", g_ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:\"%s.lib\"", g_ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);
	}

	static const vector<string> vComplierOptionToRemove = 
	{
		"-MT", "-MD", "-ML"
	};


	if (memcmp(pbFileData, IMAGE_ARCHIVE_START, IMAGE_ARCHIVE_START_SIZE) != 0)
	{
		_tprintf(_T("Not a lib file.\n"));
		RemoveLinkerOptionFromCommonObject(pbFileData, vLinkerOptionToRemove);
		return 0;
	}

	ULONGLONG ullArchiveFileSize = 0;
	TargetFile.GetSize(ullArchiveFileSize);

	ULONGLONG ullOffset = IMAGE_ARCHIVE_START_SIZE;
	char *pszLongnames;
	// Skip First Linker Member
	MoveToNextMember(pbFileData, ullArchiveFileSize, ullOffset);

	unsigned long ulNumberOfMembers;
	unsigned long* pulMemberOffsetTable;
	// Process Second Linker Member
	{
		auto memberContent = pbFileData + ullOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		ulNumberOfMembers = *(unsigned long*)(memberContent);
		pulMemberOffsetTable = (unsigned long*)(memberContent + 4);
	}
	MoveToNextMember(pbFileData, ullArchiveFileSize, ullOffset);

	// Process Longnames Member
	{
		BYTE* pbMemberContent = pbFileData + ullOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		pszLongnames = (char *)pbMemberContent;
	}

	// Process OBJ files
	for (unsigned long i = 0; i < ulNumberOfMembers; i++)
	{
		PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
			(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbFileData + pulMemberOffsetTable[i]);
		BYTE* pbMemberContent = pbFileData + pulMemberOffsetTable[i] + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		// Skip import object
		
		if (IsImportObject(pbMemberContent))
		{
			continue;
		}

		// Print OBJ file name
		if (pMemberHeader->Name[0] == '/')
		{
			// Name = /n, The name of the archive member is located at offset n within the longnames member
			unsigned __int64 iNameOffset = (unsigned)_atoi64((char*)&pMemberHeader->Name[1]);
			printf("%s\n", pszLongnames + iNameOffset);
		}
		else
		{
			char * pszEndOfName = strchr((char*)pMemberHeader->Name, '/');
			printf("%.*s\n", pszEndOfName - (char*)pMemberHeader->Name, pMemberHeader->Name);
		}
		if (IsCommonObject(pbMemberContent))
		{
			RemoveLinkerOptionFromCommonObject(pbMemberContent, vLinkerOptionToRemove);
		}
		else
		{
			RemoveComplierOptionFromAnonymousObject(pbMemberContent, vComplierOptionToRemove);
		}
	}

	return 0;
}

BOOL IsCommonObject(const BYTE* pbObjectData)
{
	IMPORT_OBJECT_HEADER * pImportHeader = (IMPORT_OBJECT_HEADER*)pbObjectData;
	if (pImportHeader->Sig1 == IMAGE_FILE_MACHINE_UNKNOWN
		&& pImportHeader->Sig2 == IMPORT_OBJECT_HDR_SIG2)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL IsImportObject(const BYTE* pbObjectData)
{
	if (!IsCommonObject(pbObjectData))
	{
		IMPORT_OBJECT_HEADER * pImportHeader = (IMPORT_OBJECT_HEADER*)pbObjectData;
		return (pImportHeader->Version == 0);
	}
	return FALSE;
}

BOOL IsAnonymousObject(const BYTE* pbObjectData)
{
	if (!IsCommonObject(pbObjectData))
	{
		IMPORT_OBJECT_HEADER * pImportHeader = (IMPORT_OBJECT_HEADER*)pbObjectData;
		return (pImportHeader->Version >= 1);
	}
	return FALSE;
}

void RemoveLinkerOptionFromCommonObject(BYTE* pbObjectData, const vector<string>& vLinkerOptionToRemove)
{
	PIMAGE_FILE_HEADER pCommonObjectHeader = (PIMAGE_FILE_HEADER)pbObjectData;
	PIMAGE_SECTION_HEADER pSectionTable = (PIMAGE_SECTION_HEADER)(pbObjectData + sizeof(IMAGE_FILE_HEADER));

	// pecoff_v83 p12:Windows loader limits the number of sections to 96.
	if (pCommonObjectHeader->NumberOfSections > 96)
		return;

	for (WORD i = 0; i < pCommonObjectHeader->NumberOfSections; i++)
	{
		// Find .drectve section
		if (strncmp(".drectve", (char*)pSectionTable[i].Name, sizeof(pSectionTable[i].Name)) != 0)
			continue;

		char *pszLinkerOptions = (char *)(pbObjectData + pSectionTable[i].PointerToRawData);

		for (DWORD j = 0; j < pSectionTable[i].SizeOfRawData; j++)
		{
			if (pszLinkerOptions[j] != '/')
				continue;

			for (auto iOptionToRemove = vLinkerOptionToRemove.begin();
				iOptionToRemove != vLinkerOptionToRemove.end();
				iOptionToRemove++)
			{
				auto nOptionLength = iOptionToRemove->length();
				if (nOptionLength >(pSectionTable[i].SizeOfRawData - j))
					continue;

				if (_strnicmp(&pszLinkerOptions[j],
					iOptionToRemove->c_str(),
					nOptionLength
					) != 0)
					continue;


				if ((j + nOptionLength) != pSectionTable[i].SizeOfRawData)
				{
					if (pszLinkerOptions[j + nOptionLength] != ' ')
					{
						// Skip if option not completely matched
						continue;
					}
					else
					{
						// Deal with spaces between options
						nOptionLength++;
					}
				}

				printf("\tRemove:%s\n", iOptionToRemove->c_str());
				memmove(&pszLinkerOptions[j],
					&pszLinkerOptions[j + nOptionLength],
					pSectionTable[i].SizeOfRawData - j - nOptionLength
					);
				pSectionTable[i].SizeOfRawData -= nOptionLength;
				break;
			}
		}
	}
}

// XXX: ANON_OBJECT_HEADER is undocumented, following code is EXPERIMENTAL!!
void RemoveComplierOptionFromAnonymousObject(BYTE* pbObjectData, const vector<string>& vComplierOptionToRemove)
{
	ANON_OBJECT_HEADER* pAnonymousObjectHeader = (ANON_OBJECT_HEADER*)pbObjectData;
	BYTE* pbAnonymousObjectContent;
	if (pAnonymousObjectHeader->Version == 1)
	{
		pbAnonymousObjectContent = pbObjectData + sizeof(ANON_OBJECT_HEADER);

		// COFF object struct inside anonymous object 
		PIMAGE_FILE_HEADER pCommonObjectHeader = (PIMAGE_FILE_HEADER)pbAnonymousObjectContent;
		PIMAGE_SECTION_HEADER pSectionTable = (PIMAGE_SECTION_HEADER)(pbAnonymousObjectContent + sizeof(IMAGE_FILE_HEADER));

		for (WORD i = 0; i < pCommonObjectHeader->NumberOfSections; i++)
		{
			// Find .cil$fg section
			if (strncmp(".cil$fg", (char*)pSectionTable[i].Name, sizeof(pSectionTable[i].Name)) != 0)
				continue;
			
			BYTE* pSectionData = pbAnonymousObjectContent + pSectionTable[i].PointerToRawData;
			unsigned long* pulOptionCount = (unsigned long*)pSectionData;
			char *pszComplierOption = (char *)(pSectionData + 4);

			for (unsigned long j = 0; j < *pulOptionCount; j++)
			{
				BOOL bRemove = FALSE;
				size_t nOptionLength;
				for (auto iOptionToRemove = vComplierOptionToRemove.begin();
					iOptionToRemove != vComplierOptionToRemove.end();
					iOptionToRemove++)
				{
					nOptionLength = iOptionToRemove->length();

					if (_stricmp(pszComplierOption,
						iOptionToRemove->c_str()
						) != 0)
						continue;

					// Add the size of NULL terminator
					nOptionLength += 1;
					bRemove = TRUE;
					break;
				}

				if (bRemove)
				{
					printf("\tRemove:%s\n", pszComplierOption);
					memmove(pszComplierOption,
						pszComplierOption + nOptionLength,
						pSectionTable[i].SizeOfRawData - nOptionLength
						);
					pSectionTable[i].SizeOfRawData -= nOptionLength;
					(*pulOptionCount)--;
				}
				else
				{
					pszComplierOption += strlen(pszComplierOption) + 1;
				}
			}
			break;
		}
	}
	else
	{
		//FIXME: Haven't ever seen undocumented ANON_OBJECT_HEADER_V2 object
	}
}

BOOL MoveToNextMember(const BYTE* pbArchiveBase, const ULONGLONG& ullArchiveSize, ULONGLONG& ullOffset)
{
	// Skip current member
	PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
		(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbArchiveBase + ullOffset);
	unsigned __int64 iMemberSize = (unsigned)_atoi64((char*)pMemberHeader->Size);
	ullOffset += iMemberSize + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

	// Skip padding
	if (ullOffset < ullArchiveSize)
	{
		if (pbArchiveBase[ullOffset] == ARCHIVE_PAD)
			ullOffset++;
	}
	return (ullOffset < ullArchiveSize);
}
