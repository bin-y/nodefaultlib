// nodefaultlib.cpp : 定义控制台应用程序的入口点。
//

#include <atlbase.h>
#include <atlfile.h>
#include <vector>
#include <string>

using std::vector;
using std::string;

#define REVERSEWORD(w)MAKEWORD(HIBYTE(w), LOBYTE(w))
#define REVERSELONG(l)MAKELONG(REVERSEWORD(HIWORD(l)), REVERSEWORD(LOWORD(l)))
#define ARCHIVE_PAD (IMAGE_ARCHIVE_PAD[0])

static const char* const ppszDefaultLibraryNames[] =
{
	"libc",
	"libcmt",
	"msvcrt",
	"libcd",
	"libcmtd",
	"msvcrtd"
};

#define DEFAULT_LIBRARY_NAME_NUM (sizeof(ppszDefaultLibraryNames) / sizeof(const char*))

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc != 2)
	{
		_tprintf(_T("Usage:nodefaultlib filename\n"));
		_tprintf(_T("Example:nodefaultlib tommath.lib\n"));
		return 0;
	}

	CAtlFile LibraryFile;
	LibraryFile.Create(argv[1], GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING);
	CAtlFileMappingBase LibraryFileMap;
	LibraryFileMap.MapFile(LibraryFile, 0, 0, PAGE_READWRITE, FILE_MAP_READ | FILE_MAP_WRITE);
	BYTE *pbLibraryFileData = (BYTE *)LibraryFileMap.GetData();

	vector<string> vLinkerOptionToRemove;

	for (int i = 0; i < DEFAULT_LIBRARY_NAME_NUM; i++)
	{
		char szLinkerParameter[32];
		sprintf_s(szLinkerParameter, "/DEFAULTLIB:%s", ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:\"%s\"", ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:%s.lib", ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);

		sprintf_s(szLinkerParameter, "/DEFAULTLIB:\"%s.lib\"", ppszDefaultLibraryNames[i]);
		vLinkerOptionToRemove.push_back(szLinkerParameter);
	}

	if (memcmp(pbLibraryFileData, IMAGE_ARCHIVE_START, IMAGE_ARCHIVE_START_SIZE) != 0)
	{
		_tperror(_T("Not a lib file.\n"));
		return 0;
	}

	unsigned __int64 uiOffset = IMAGE_ARCHIVE_START_SIZE;
	unsigned long ulSymbolCount;
	unsigned long* pulSymbolOffsetArray;
	char *pszLongnames;
	//Process First Linker Member
	{
		PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
			(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbLibraryFileData + uiOffset);
		PBYTE pbMemberContent = pbLibraryFileData + uiOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
		unsigned __int64 iMemberSize = (unsigned)_atoi64((char*)pMemberHeader->Size);
		uiOffset += iMemberSize + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		ulSymbolCount = (unsigned)REVERSELONG(*(unsigned long*)pbMemberContent);
		pulSymbolOffsetArray = (unsigned long*)(pbMemberContent + 4);
	}

	//Skip padding
	if (pbLibraryFileData[uiOffset] == ARCHIVE_PAD)
		uiOffset++;

	//Skip Second Linker Member
	{
		PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
			(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbLibraryFileData + uiOffset);
		PBYTE pbMemberContent = pbLibraryFileData + uiOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
		unsigned __int64 iMemberSize = (unsigned)_atoi64((char*)pMemberHeader->Size);
		uiOffset += iMemberSize + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
	}

	//Skip padding
	if (pbLibraryFileData[uiOffset] == ARCHIVE_PAD)
		uiOffset++;

	//Process Longnames Member
	{
		PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
			(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbLibraryFileData + uiOffset);
		PBYTE pbMemberContent = pbLibraryFileData + uiOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
		unsigned __int64 iMemberSize = (unsigned)_atoi64((char*)pMemberHeader->Size);
		uiOffset += iMemberSize + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		pszLongnames = (char *)pbMemberContent;
	}

	//Process OBJ files
	unsigned long ulLastCoffOffset = 0;
	for (unsigned long i = 0; i < ulSymbolCount; i++)
	{
		unsigned long ulCoffOffset = (unsigned)REVERSELONG(pulSymbolOffsetArray[i]);
		if (ulLastCoffOffset == ulCoffOffset)
			continue;
		ulLastCoffOffset = ulCoffOffset;
		PIMAGE_ARCHIVE_MEMBER_HEADER pMemberHeader =
			(PIMAGE_ARCHIVE_MEMBER_HEADER)(pbLibraryFileData + ulCoffOffset);
		PBYTE pbMemberContent = pbLibraryFileData + ulCoffOffset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);

		//Print OBJ file name
		if (pMemberHeader->Name[0] == '/')
		{
			//Name = /n, The name of the archive member is located at offset n within the longnames member
			unsigned __int64 iNameOffset = (unsigned)_atoi64((char*)&pMemberHeader->Name[1]);
			printf("%s\n", pszLongnames + iNameOffset);
		}
		else
		{
			char * pszEndOfName = strchr((char*)pMemberHeader->Name, '/');
			printf("%.*s\n", pszEndOfName - (char*)pMemberHeader->Name, pMemberHeader->Name);
		}

		PIMAGE_FILE_HEADER pCoffHeader = (PIMAGE_FILE_HEADER)pbMemberContent;
		PIMAGE_SECTION_HEADER pSectionTable = (PIMAGE_SECTION_HEADER)(pbMemberContent + sizeof(IMAGE_FILE_HEADER));

		//pecoff_v83 p12:Windows loader limits the number of sections to 96.
		if (pCoffHeader->NumberOfSections > 96)
			continue;

		for (WORD j = 0; j < pCoffHeader->NumberOfSections; j++)
		{
			//Find .drectve section
			if (strcmp(".drectve", (char*)pSectionTable[j].Name) != 0)
				continue;

			char *pszLinkerOptions = (char *)(pbMemberContent + pSectionTable[j].PointerToRawData);

			for (DWORD k = 0; k < pSectionTable[j].SizeOfRawData; k++)
			{
				if (pszLinkerOptions[k] != '/')
					continue;

				for (auto iOptionToRemove = vLinkerOptionToRemove.begin();
					iOptionToRemove != vLinkerOptionToRemove.end();
					iOptionToRemove++)
				{
					auto nOptionLength = iOptionToRemove->length();
					if (nOptionLength > (pSectionTable[j].SizeOfRawData - k))
						continue;

					if (_strnicmp(&pszLinkerOptions[k],
						iOptionToRemove->c_str(),
						nOptionLength
						) != 0)
						continue;


					if ((k + nOptionLength) != pSectionTable[j].SizeOfRawData)
					{
						if (pszLinkerOptions[k + nOptionLength] != ' ')
						{
							//Skip if option not completely matched
							continue;
						}
						else
						{
							//Deal with spaces between options
							nOptionLength++;
						}
					}

					printf("\tOld:%.*s\n", pSectionTable[j].SizeOfRawData, pszLinkerOptions);
					memmove(&pszLinkerOptions[k],
						&pszLinkerOptions[k + nOptionLength],
						pSectionTable[j].SizeOfRawData - k - nOptionLength
						);
					pSectionTable[j].SizeOfRawData -= nOptionLength;
					printf("\tNew:%.*s\n", pSectionTable[j].SizeOfRawData, pszLinkerOptions);
					break;
				}
			}
		}
	}

	return 0;
}

