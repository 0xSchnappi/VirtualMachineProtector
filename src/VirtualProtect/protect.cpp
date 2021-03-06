// This is a personal academic project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com


#include "protect.h"
#include "Error.h"
#include "PE.h"
#include "PEUtils.h"
#include "StringOperator.h"
#pragma warning(disable:4244)
#pragma warning(disable:4996)
#pragma warning(disable:4838)
#pragma warning(disable:4309)

//jump table
BYTE condTab[16];
//TODO: random vm_instr prefix (0xFFFF)
//0xFFFE, 0xFFFD, 0xFFFC, 0xFFFB, 0xFFFA, 0xFFF9,  0xFFF8,
WORD vm_instr_prefix = 0xFFFF;
DWORD vm_key;
//vm opcode table
#define VM_INSTR_COUNT 256
BYTE opcodeTab[VM_INSTR_COUNT];

void MAKE_VM_CALL2(BYTE* funcAddr, DWORD funcRVA, DWORD vmedFuncRVA, DWORD fSize, DWORD vmStartRVA, BYTE* inLdrAddr, DWORD inLdrRVA)
{
	*(BYTE*)(funcAddr) = 0xE9;
	*(DWORD*)((BYTE*)(funcAddr)+1) = (inLdrRVA)-(funcRVA)-5;
	memset((BYTE*)(funcAddr)+5, 0x90, (fSize)-5);
	// 0x401896 - 0x4018BA处的修改


	// 新的函数开始
	*(BYTE*)(inLdrAddr) = 0xE8;
	*(DWORD*)((BYTE*)(inLdrAddr)+1) = 0;
	*((BYTE*)(inLdrAddr)+5) = 0x9C;
	*(DWORD*)((BYTE*)(inLdrAddr)+6) = 0x04246C81;
	*(DWORD*)((BYTE*)(inLdrAddr)+10) = (inLdrRVA)-(vmedFuncRVA)+5;	// ByteCode
	*((BYTE*)(inLdrAddr)+14) = 0x9D;
	*((BYTE*)(inLdrAddr)+15) = 0xE8;
	*(DWORD*)((BYTE*)(inLdrAddr)+16) = 0;
	*((BYTE*)(inLdrAddr)+20) = 0x9C;
	*(DWORD*)((BYTE*)(inLdrAddr)+21) = 0x04246C81;
	*(DWORD*)((BYTE*)(inLdrAddr)+25) = (inLdrRVA)-((funcRVA)+5) + 20;	// 40189B
	*((BYTE*)(inLdrAddr)+29) = 0x9D;
	*(BYTE*)((BYTE*)(inLdrAddr)+30) = 0xE9;
	*(DWORD*)((BYTE*)(inLdrAddr)+31) = (vmStartRVA)-((inLdrRVA)+30) - 5;
}



DWORD SectionAlignment(DWORD a, DWORD b)
{
	if (a % b)
		return (a + (b - (a % b)));
	else
		return a;
}

int GetVMByteCodeSize(int itemsSize, DWORD* protectedCodeFOA, HWND listBox, DWORD* protectedCodeRVA, PE& protectedFile)
{
	int protSize = 0;
	for (int i = 0; i < itemsSize; i++)
	{
		wchar_t temp[25];
		SendMessage(listBox, LB_GETTEXT, i, (LPARAM)temp);
		temp[8] = 0;
		swscanf(temp, TEXT("%x"), &protectedCodeRVA[i * 2]);
		swscanf(temp + 11, TEXT("%x"), &protectedCodeRVA[i * 2 + 1]);
		/*
		* items[0] = 401896
		* items[1] = 4018BA
		*/
		protectedCodeRVA[i * 2] -= protectedFile.GetNtHeaders()->OptionalHeader.ImageBase;
		protectedCodeRVA[i * 2 + 1] -= protectedFile.GetNtHeaders()->OptionalHeader.ImageBase;

		protectedCodeFOA[i * 2] = RvaToRaw(protectedFile.GetNtHeaders()->FileHeader.NumberOfSections,
			protectedFile.GetSectionHeaders(),
			protectedCodeRVA[i * 2]);
		//if (items2[i * 2] == 0xFFFFFFFF) ERROR2(TEXT("Invalid range start"));
		if (protectedCodeFOA[i * 2] == 0xFFFFFFFF) return -1;
		protectedCodeFOA[i * 2 + 1] = protectedCodeRVA[i * 2 + 1] - protectedCodeRVA[i * 2];		//size
		auto t = vm_protect(protectedFile.GetPEHandle() + protectedCodeFOA[i * 2],
			protectedCodeFOA[i * 2 + 1], 0,
			protectedCodeRVA[i * 2],
			(BYTE*)protectedFile.GetBaseRelocationTable(),
			protectedFile.GetNtHeaders()->OptionalHeader.ImageBase);	// 获取返回值
		if (t == -1)
			return -1;
		protSize += t;
	}
	return protSize;
}

bool MemoryWriteToFile(wchar_t* fileName, PE& protectedFile, DWORD oldNewSecSize, DWORD newSecSize, BYTE* hNewMem)
{
	DWORD tmp;
	// 设置文件名
	char newFName[MAX_PATH] = { 0 };
	StringOperator<wchar_t*> cFileName(fileName);
	strcpy(newFName, cFileName.wchar2char());
	if (strlen(newFName) > 4)
	{
		newFName[strlen(newFName) - 4] = 0;
	}
	strcat(newFName, "_vmed.exe");

	// 将新的节从内存写入文件
	StringOperator<char*> wNewFName(newFName);
	auto hFile = CreateFile(wNewFName.char2wchar(), GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	WriteFile(hFile, protectedFile.GetPEHandle(), protectedFile.GetPEFileSize(), &tmp, 0);
	WriteFile(hFile, hNewMem, oldNewSecSize, &tmp, 0);
	memset(hNewMem, 0, newSecSize - oldNewSecSize);
	WriteFile(hFile, hNewMem, newSecSize - oldNewSecSize, &tmp, 0);
	CloseHandle(hFile);

	return true;
}

bool AddSection(IMAGE_SECTION_HEADER* sectionHeaders, PE& protectedFile, DWORD newRVA, DWORD newSecSize)
{
	sectionHeaders += protectedFile.GetNtHeaders()->FileHeader.NumberOfSections;
	protectedFile.GetNtHeaders()->FileHeader.NumberOfSections++;
	memset(sectionHeaders, 0, sizeof(IMAGE_SECTION_HEADER));
	sectionHeaders->Characteristics = 0xE00000E0;
	sectionHeaders->Misc.VirtualSize = SectionAlignment(newSecSize, protectedFile.GetNtHeaders()->OptionalHeader.SectionAlignment);
	protectedFile.GetNtHeaders()->OptionalHeader.SizeOfImage += sectionHeaders->Misc.VirtualSize;
	memmove(sectionHeaders->Name, ".VM", 4);
	sectionHeaders->PointerToRawData = protectedFile.GetPEFileSize();
	sectionHeaders->SizeOfRawData = newSecSize;
	sectionHeaders->VirtualAddress = newRVA;
	return true;
}

bool SetVMEntryPoint(PE& protectedFile, DWORD newRVA, int vmSize, DWORD vmInit, BYTE* hNewMem, int& curPos)
{
	//setting new entry point
	auto oldEntry = protectedFile.GetNtHeaders()->OptionalHeader.AddressOfEntryPoint;
	protectedFile.GetNtHeaders()->OptionalHeader.AddressOfEntryPoint = newRVA + vmSize;

	//VirtualAlloc at 0xA0F8
	// SetVMStart
	static char VMEntry[] =
	{
		0xE8, 0x00, 0x00, 0x00, 0x00,       //CALL    vm_test2.004120DA
		0x5B,                               //POP     EBX
		0x81, 0xEB, 0xDA, 0x20, 0x01, 0x00, //SUB     EBX,120DA
		0x6A, 0x40,							//PUSH    40
		0x68, 0x00, 0x10, 0x00, 0x00,       //PUSH    1000
		0x68, 0x00, 0x10, 0x00, 0x00,       //PUSH    1000
		0x6A, 0x00,                         //PUSH    0
		0xB8, 0x34, 0x12, 0x00, 0x00,       //MOV     EAX,1234
		0x03, 0xC3,                         //ADD     EAX,EBX
		0xFF, 0x10,                         //CALL    [EAX]
		0x53,                               //PUSH    EBX
		0x05, 0x00, 0x10, 0x00, 0x00,       //ADD     EAX,1000
		0x50,                               //PUSH    EAX
		0xB8, 0x34, 0x12, 0x00, 0x00,       //MOV     EAX,1234
		0x03, 0xC3,                         //ADD     EAX,EBX
		0xFF, 0xD0,                         //CALL    EAX
		0xB8, 0x34, 0x12, 0x00, 0x00,       //MOV     EAX,1234
		0x03, 0xC3,                         //ADD     EAX,EBX
		0xFF, 0xE0                          //JMP     EAX
	};

	auto dwVirtualAllocOfAddress = SearchFunction(protectedFile.GetPEHandle(), "VirtualAlloc");// 如果返回空，无找到函数
	//DWORD vAllocbat = (DWORD)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")),"VirtualAlloc");
	//correcting loader:
	*(DWORD*)(VMEntry + 8) = newRVA + vmSize + 5;
	*(DWORD*)(VMEntry + 27) = dwVirtualAllocOfAddress;
	*(DWORD*)(VMEntry + 43) = newRVA + vmInit;
	*(DWORD*)(VMEntry + 52) = oldEntry;
	memmove(hNewMem + vmSize, VMEntry, sizeof(VMEntry));
	curPos += sizeof(VMEntry);
	return true;
}

bool SetByteCode(PE& protectedFile, int& curPos, DWORD newRVA, DWORD vmStart, DWORD* protectedCodeOffset, int itemsCnt, DWORD* items2, BYTE* hNewMem)
{
	for (int i = 0; i < itemsCnt; i++)
	{
		auto _tts = vm_protect(protectedFile.GetPEHandle() + items2[i * 2],
			items2[i * 2 + 1], hNewMem + curPos,
			protectedCodeOffset[i * 2],
			(BYTE*)protectedFile.GetBaseRelocationTable(),
			protectedFile.GetNtHeaders()->OptionalHeader.ImageBase);
		//MAKE_VM_CALL2(ntHeaders->OptionalHeader.ImageBase, protectedFile.GetPEHandle() + items2[i*2], protectedCodeOffset[i*2], newRVA + curPos, items2[i*2 + 1], newRVA + vmStart, hNewMem + curPos + _tts, newRVA + curPos + _tts);
		MAKE_VM_CALL2(protectedFile.GetPEHandle() + items2[i * 2],
			protectedCodeOffset[i * 2],
			newRVA + curPos, items2[i * 2 + 1],
			newRVA + vmStart, hNewMem + curPos + _tts,
			newRVA + curPos + _tts);
		curPos += _tts + VM_CALL_SIZE;
	}
	return true;
}

int vm_init(BYTE** lpVirtualMachine, DWORD* dwVMInit, DWORD* dwVMStart, BYTE* firstSectionAddress)
{	
	DWORD vmSize = *(DWORD*)firstSectionAddress;
	DWORD vmCodeStart = *(DWORD*)(firstSectionAddress + 4);
	DWORD dwExportTable = (*(DWORD*)(firstSectionAddress + 28))*4 + (*(DWORD*)(firstSectionAddress + 32))*8 + 4;
	*dwVMInit = *(DWORD*)(firstSectionAddress + 8) - dwExportTable;
	*dwVMStart = *(DWORD*)(firstSectionAddress + 12) - dwExportTable;
	DWORD vmPoly = *(DWORD*)(firstSectionAddress + 16);
	DWORD vmPrefix = *(DWORD*)(firstSectionAddress + 20);
	DWORD vmOpcodeTab = *(DWORD*)(firstSectionAddress + 24);
	*lpVirtualMachine = firstSectionAddress + dwExportTable;

	// 虚拟化的混淆
	GetPolyEncDec();
	memmove(firstSectionAddress + vmPoly, _vm_poly_dec, sizeof(_vm_poly_dec));	// 将解密函数放置到虚拟化引擎中

	GetPermutation(condTab, 16);
	BYTE invCondTab[16];
	memmove(invCondTab, condTab, 16);
	KeyToValue16(invCondTab);	// Key to Value
	permutateJcc((WORD*)(firstSectionAddress + vmCodeStart + 17), 16, invCondTab);
	// 设置opcode
	GetPermutation(opcodeTab, VM_INSTR_COUNT);
	memmove(firstSectionAddress + vmOpcodeTab, opcodeTab, VM_INSTR_COUNT);
	KeyToValue256(firstSectionAddress + vmOpcodeTab);
	*(WORD*)(firstSectionAddress + vmPrefix) = vm_instr_prefix;

	return vmSize;
}





int vm_protect(BYTE* codeBase, int codeSize, BYTE* outCodeBuf, DWORD inExeFuncRVA, const BYTE* relocBuf, DWORD imgBase)
{
	/*
	* codeBase 虚拟化代码FOA
	* codeSize 代码块大小
	* inExeFuncRVA 虚拟化代码RVA
	* 
	* imageBase 镜像基址
	*/
	//relocations
	DWORD* relocMap = NULL;
	int relocSize;
	if (relocBuf)
	{
		relocSize = GetRelocMap(relocBuf, inExeFuncRVA, codeSize, 0);
		relocMap = (DWORD*)malloc(4*relocSize);
		GetRelocMap(relocBuf, inExeFuncRVA, codeSize, relocMap);
	}

	//disasm_struct dis;
	struct 
	{
		DWORD disasm_len;
	} dis; 
	int curPos = 0;
	int outPos = 0;
	DWORD index = 0;

	auto instrCnt = GetCodeMap(codeBase, codeSize, 0);	// 汇编指令行数
	if (instrCnt == -1) return -1;
	DWORD* codeMap = (DWORD*)malloc(4*instrCnt + 4);
	DWORD* outCodeMap = (DWORD*)malloc(4*instrCnt + 8);	//one byte more for vm_end
	GetCodeMap(codeBase, codeSize, codeMap);
	codeMap[instrCnt] = 0;
	outCodeMap[instrCnt + 1] = 0;

	int relocPtr = 0;

	while (curPos != codeSize)
	{		
		//
		//if (outPos > 0xBD0) MessageBox(0, "d", "d", 0);

		/*
		if (!disasm(codeBase + curPos, &dis)) 
		{
		GlobalFree(outCodeMap);
		GlobalFree(codeMap);	
		return -1;
		}
		*/
		int opSplitSize = 0;
		//patch for LDE engine bug ;p (kurwa?jego ma?
		//BYTE ldeExcept[3] = {0x8D, 0x84, 0x05};
		// 获取OPCODE长度
		if ((curPos - 3 < codeSize) && 
			((((*(DWORD*)(codeBase + curPos)) & 0xFFFFFF) == 0x05848D) ||
			(((*(DWORD*)(codeBase + curPos)) & 0xFFFFFF) == 0x15948D) ||
			(((*(DWORD*)(codeBase + curPos)) & 0xFFFFFF) == 0x0D8C8D) ||
			(((*(DWORD*)(codeBase + curPos)) & 0xFFFFFF) == 0x1D9C8D)
			)) dis.disasm_len = 7;
		else dis.disasm_len = lde(codeBase + curPos);		
		//
		int prefSize = dis.disasm_len >> 8;
		dis.disasm_len &= 0xFF;
		if (!dis.disasm_len)
		{
			free(outCodeMap);
			free(codeMap);
			free(relocMap);
			return -1;
		}
		//if (outCodeBuf)
		//{	
		outCodeMap[index] = outPos;

		// 有重定位
		if (relocBuf && (codeMap[index] < relocMap[relocPtr] - inExeFuncRVA) &&
			(codeMap[index + 1] > relocMap[relocPtr] - inExeFuncRVA))
		{			
			//MessageBox(0, "rel", "", 0);
			if (outCodeBuf) 
			{ 
				// outCodeBuf 表项大小+0xFFFF+0xF0+重定位的地址位置+指令长度+计算偏移
				PUT_VM_OP_SIZE(dis.disasm_len + 5);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_VM_RELOC);	// 设置标志，此处需要重定位
				// 重定位的地址位置
				*(outCodeBuf + outPos + 4) = relocMap[relocPtr] - inExeFuncRVA - codeMap[index];
				// 指令长度
				*(outCodeBuf + outPos + 5) = dis.disasm_len; 
				memmove(outCodeBuf + outPos + 6, codeBase + curPos, dis.disasm_len);
				// 计算偏移 
				(*(DWORD*)(outCodeBuf + outPos + 6 + relocMap[relocPtr] - inExeFuncRVA - codeMap[index])) -= imgBase;	
			} 
			outPos += dis.disasm_len + 6;
			relocPtr++;
		}
		// 无重定位
		else if (!prefSize) {

			if ((((codeBase + curPos + prefSize)[0] & 0xF0) == 0x70) || (((codeBase + curPos + prefSize)[0] == 0x0F) && (((codeBase + curPos + prefSize)[1] & 0xF0) == 0x80)))
			{
				if (outCodeBuf)
				{
					//conditional jumps correct and generate
					bool shortjmp = true;
					if ((codeBase + curPos + prefSize)[0] == 0x0F) shortjmp = false;
					PUT_VM_OP_SIZE(8);
					PUT_VM_PREFIX;
					if (shortjmp) PUT_VM_OPCODE(I_COND_JMP_SHORT);	//				
					else PUT_VM_OPCODE(I_COND_JMP_LONG);			//put Jcc opcode
					BYTE condition;
					if (shortjmp) condition = (codeBase + curPos + prefSize)[0] & 0xF;
					else condition = (codeBase + curPos + prefSize)[1] & 0xF;
					*(outCodeBuf + outPos + 4) = condTab[condition];		//put condition
					DWORD delta;
					if (shortjmp) delta = (int)*(char*)(codeBase + curPos + 1);		//byte extended to dword with sign
					else delta = *(DWORD*)(codeBase + curPos + 2);
					*(DWORD*)(outCodeBuf + outPos + 5) = delta;
				}
				outPos += 9;	//fixed length for all conditional jumps (no short/long)

			}
			OPCODE_BEGIN_MAP_B
				/*
				* else if (0 || ((codeBase + curPos + prefSize)[0] == 0xE9) || ((codeBase + curPos + prefSize)[0] == 0xEB)) {
				* 
				* 
				* 
				*/
				OPCODE_MAP_ENTRY(0xE9)	// E9 jmp 长跳转16位
				OPCODE_MAP_ENTRY(0xEB)	// EB jmp 相对段跳转8位
			OPCODE_BEGIN_MAP_E
			{
				if (outCodeBuf)
				{
					PUT_VM_OP_SIZE(7);
					PUT_VM_PREFIX;
					//PUT_VM_OPCODE(I_JMP);
					if ((codeBase + curPos + prefSize)[0] == 0xE9) 
					{
						PUT_VM_OPCODE(I_JMP_LONG);
						*(DWORD*)(outCodeBuf + outPos + 4) = *(DWORD*)(codeBase + curPos + 1);
					}
					else 
					{
						/*if ((codeBase + curPos + prefSize)[1] == 1)
						{
							PUT_VM_OPCODE(I_VM_NOP);
							*(DWORD*)(outCodeBuf + outPos + 4) = rand();
							curPos++;
						}
						else*/
						{
							PUT_VM_OPCODE(I_JMP_SHORT);
							*(DWORD*)(outCodeBuf + outPos + 4) = (int)*(char*)(codeBase + curPos + 1);
						}
					}
				}
			}
			OPCODE_END(8)
				OPCODE_BEGIN(0xE3)	//JECXZ/JCXZ
			{
				PUT_VM_OP_SIZE(7);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_JECX);
				*(DWORD*)(outCodeBuf + outPos + 5) = (int)*(char*)(codeBase + curPos + 1);
			}
			OPCODE_END(8)
				OPCODE_BEGIN(0xE8)	//relative direct calls
			{
				// outCodeBuf 表项+0xFFFF+VMOPCODE+重定位的地址位置+指令长度+计算偏移
				PUT_VM_OP_SIZE(7);
				PUT_VM_PREFIX;
				if (*(DWORD*)(codeBase + curPos + 1)) PUT_VM_OPCODE(I_CALL_REL);					
				else PUT_VM_OPCODE(I_VM_FAKE_CALL);		// call 0
				*(DWORD*)(outCodeBuf + outPos + 4) = inExeFuncRVA + *(DWORD*)(codeBase + curPos + 1) + curPos + 5;
			}		
			OPCODE_END(8)
				OPCODE_BEGIN(0xC2)	//ret xxxx
			{
				PUT_VM_OP_SIZE(5);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_RET);
				*(WORD*)(outCodeBuf + outPos + 4) = *(WORD*)(codeBase + curPos + 1);
			}
			OPCODE_END(6)
				OPCODE_BEGIN(0xC3)	//ret
			{
				PUT_VM_OP_SIZE(5);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_RET);
				*(WORD*)(outCodeBuf + outPos + 4) = 0;
			}
			OPCODE_END(6)
				OPCODE_BEGIN_3(0xE0, 0xE1, 0xE2)	//loop, loope, loopne
			{
				PUT_VM_OP_SIZE(8);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_LOOPxx);
				*(outCodeBuf + outPos + 4) = (codeBase + curPos + prefSize)[0] & 0x0F;
				*(WORD*)(outCodeBuf + outPos + 5) = (int)*(char*)(codeBase + curPos + 1);
			}
			OPCODE_END(9)
				OPCODE_BEGIN_MAP_B
				OPCODE_MAP_ENTRY(0x01)	//ADD   mem, r32
				OPCODE_MAP_ENTRY(0x03)	//ADD   r32, mem
				OPCODE_MAP_ENTRY(0x09)	//OR    mem, r32
				OPCODE_MAP_ENTRY(0x0B)	//OR    r32, mem
				OPCODE_MAP_ENTRY(0x21)	//AND   mem, r32
				OPCODE_MAP_ENTRY(0x23)	//AND   r32, mem
				OPCODE_MAP_ENTRY(0x29)	//SUB   mem, r32
				OPCODE_MAP_ENTRY(0x2B)	//SUB   r32, mem
				OPCODE_MAP_ENTRY(0x31)	//XOR   mem, r32
				OPCODE_MAP_ENTRY(0x33)	//XOR   r32, mem
				OPCODE_MAP_ENTRY(0x39)	//CMP   mem, r32
				OPCODE_MAP_ENTRY(0x3B)	//CMP   r32, mem
				OPCODE_MAP_ENTRY(0x85)	//TEST  r32, mem
				OPCODE_MAP_ENTRY(0x89)	//MOV   mem, r32
				OPCODE_MAP_ENTRY(0x8B)	//MOV   r32, mem
				OPCODE_MAP_ENTRY(0x8D)	//LEA   r32, mem
				OPCODE_BEGIN_MAP_E
			{
				int _mod, _reg, _rm, _scale, _base, _index;
				_mod = ((codeBase + curPos + prefSize)[1] & 0xC0) >> 6;
				_reg = ((codeBase + curPos + prefSize)[1] & 0x38) >> 3;
				_rm = (codeBase + curPos + prefSize)[1] & 7;
				BYTE* instr = codeBase + curPos + prefSize;
				if ((instr[1] & 7) == 0x4)
				{
					_scale = (instr[2] & 0xC0) >> 6;
					_index = (instr[2] & 0x38) >> 3;
					_base = instr[2] & 7;
				}

				switch (_mod)
				{
				case 0:
					if (_rm == 4)	//SIB
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						MAKE_REAL_INSTR;
					}
					else if (_rm == 5)
					{
						MAKE_MOV_IMM(*(DWORD*)(instr + 2));
						MAKE_REAL_INSTR;
					}
					else
					{
						MAKE_MOV_REG(_rm);
						MAKE_REAL_INSTR;
					}
					break;
				case 1:
					if (_rm == 4)
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						MAKE_ADD_IMM((int)*((char*)instr + 3));
						MAKE_REAL_INSTR;
					}
					else
					{
						MAKE_MOV_REG(_rm);
						MAKE_ADD_IMM((int)*((char*)instr + 2));
						MAKE_REAL_INSTR;
					}
					break;
				case 2:
					if (_rm == 4)
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						MAKE_ADD_IMM(*(DWORD*)(instr + 3));
						MAKE_REAL_INSTR;
					}
					else
					{					
						MAKE_MOV_REG(_rm);
						MAKE_ADD_IMM(*(DWORD*)(instr + 2));
						MAKE_REAL_INSTR;
					}
					break;
				case 3:
					//original instruction processing
					MAKE_ORIG_INSTR;
					break;
				}
			}
			OPCODE_END(0)
				OPCODE_BEGIN_MAP_B
				OPCODE_MAP_ENTRY(0x81)		//INSTR    Mem, IMM32 (Rej 0..7)(add, or, adc, sbb, and, sub, xor, cmp)
				OPCODE_MAP_ENTRY(0x8F)		//POP      Mem        (Rej 0)(pop)
				OPCODE_MAP_ENTRY(0xC1)		//INSTR    Mem, Db    (Rej 0..7)(rol, ror, rcl, rcr, sal, shl, shr, sar)
				OPCODE_MAP_ENTRY(0xC7)		//MOV      Mem, IMM32 (Rej 0)(mov)
				OPCODE_MAP_ENTRY(0xD1)		//INSTR    Mem, 1     (Rej 0..7)(rol, ror, rcl, rcr, sal, shl, shr, sar)
				OPCODE_MAP_ENTRY(0xFF)		//INSTR    Mem        (Rej 2, 4, 6)(call, jmp, push)
				OPCODE_BEGIN_MAP_E
			{
				BYTE tmp_imm8;
				int _mod, _reg, _rm, _scale, _index, _base;
				BYTE* instr = codeBase + curPos + prefSize;
				_mod = (instr[1] & 0xC0) >> 6;
				_reg = (instr[1] & 0x38) >> 3;
				_rm = instr[1] & 7;			
				if (_rm == 0x4)
				{
					_scale = (instr[2] & 0xC0) >> 6;
					_index = (instr[2] & 0x38) >> 3;
					_base = instr[2] & 7;
				}
				switch (_mod)
				{
				case 0:
					if (_rm == 4)	//SIB
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						//MAKE_REAL_INSTR;
						tmp_imm8 = 3;
					}
					else if (_rm == 5)
					{
						MAKE_MOV_IMM(*(DWORD*)(instr + 2));
						//MAKE_REAL_INSTR;
						tmp_imm8 = 6;
					}
					else
					{
						MAKE_MOV_REG(_rm);
						//MAKE_REAL_INSTR;
						tmp_imm8 = 2;
					}
					break;
				case 1:
					if (_rm == 4)
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						MAKE_ADD_IMM((int)*((char*)instr + 3));
						//MAKE_REAL_INSTR;
						tmp_imm8 = 4;
					}
					else
					{
						MAKE_MOV_REG(_rm);
						MAKE_ADD_IMM((int)*((char*)instr + 2));
						//MAKE_REAL_INSTR;
						tmp_imm8 = 3;
					}
					break;
				case 2:
					if (_rm == 4)
					{
						if (_index != 4)
						{
							MAKE_MOV_REG(_index);
							MAKE_SHL_IMM(_scale);
						}
						else 
						{
							MAKE_MOV_IMM(0);
						}
						MAKE_ADD_REG(_base);
						MAKE_ADD_IMM(*(DWORD*)(instr + 3));
						//MAKE_REAL_INSTR;
						tmp_imm8 = 7;
					}
					else
					{					
						MAKE_MOV_REG(_rm);
						MAKE_ADD_IMM(*(DWORD*)(instr + 2));
						//MAKE_REAL_INSTR;
						tmp_imm8 = 6;
					}
					break;
				case 3:
					//original instruction processing
					MAKE_ORIG_INSTR;
					break;
				}

				if (_mod < 3)
				{
					if ((instr[0] == 0xC1) || (instr[0] == 0xD1))
					{

						if (instr[0] == 0xC1) tmp_imm8 = instr[tmp_imm8];
						else tmp_imm8 = 1;
						BYTE __op[8] = {I_VM_ROL, I_VM_ROR, I_VM_RCL, I_VM_RCR, I_VM_SHL, I_VM_SHR, I_VM_SAL, I_VM_SAR};					
						MAKE_XXX_REG(tmp_imm8, __op[_reg]);
					}
					else if (instr[0] == 0x81)
					{
						BYTE __op[8] = {I_VM_ADD, I_VM_OR, I_VM_ADC, I_VM_SBB, I_VM_AND, I_VM_SUB, I_VM_XOR, I_VM_CMP};
						MAKE_XXX_IMM((*(DWORD*)(instr + tmp_imm8)), __op[_reg]);
					}
					else if ((instr[0] == 0xC7) && (!_reg)) { MAKE_XXX_IMM((*(DWORD*)(instr + tmp_imm8)), I_VM_MOV); }
					else if ((instr[0] == 0x8F) && (!_reg)) { MAKE_VM_PURE(I_VM_POP); }				
					else if ((instr[0] == 0xFF) && (_reg == 2)) { MAKE_VM_PURE(I_VM_CALL); }
					else if ((instr[0] == 0xFF) && (_reg == 4)) { MAKE_VM_PURE(I_VM_JMP); }
					else if ((instr[0] == 0xFF) && (_reg == 6)) { MAKE_VM_PURE(I_VM_PUSH); }
					else { MAKE_ORIG_INSTR; }
				}

			}
			OPCODE_END(0)
			else { MAKE_ORIG_INSTR; }	// outCodeBuf 	codesize+code
		}
		else { MAKE_ORIG_INSTR; }
		//}
		curPos += dis.disasm_len;
		index++;
		if (curPos == codeSize)
		{
			//vm end
			if (outCodeBuf)
			{
				PUT_VM_OP_SIZE(3);
				PUT_VM_PREFIX;
				PUT_VM_OPCODE(I_VM_END);
			}
			outCodeMap[index] = outPos;
			outPos += 4;
			index++;
		}
	}

	outCodeMap[index] = outPos;

	//cipher loop
	//Jcc correction loop
	if (outCodeBuf)
	{
         		for (int i = 0; i < instrCnt + 1; i++)
		{
			if (*(WORD*)(outCodeBuf + outCodeMap[i] + 1) == vm_instr_prefix)
			{
				//test for Jcc
				if ((*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_COND_JMP_SHORT]) ||
					(*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_COND_JMP_LONG]) ||
					(*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JECX]) ||
					(*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_LOOPxx]) ||
					(*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JMP_LONG]) ||
					(*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JMP_SHORT])
					)
				{
					int ttt = 0;
					int jecxcorr = 0;
					if (*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_COND_JMP_LONG]) ttt = 4;
					if (*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JECX]) jecxcorr = 1;
					if (*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JMP_LONG]) 
					{
						jecxcorr = 1;
						ttt = 3;
					}
					if (*(outCodeBuf + outCodeMap[i] + 3) == opcodeTab[I_JMP_SHORT]) jecxcorr = 1;
					DWORD outDest = codeMap[i] + *(DWORD*)(outCodeBuf + outCodeMap[i] + 5 - jecxcorr) + 2 + ttt;
					//search outDest in codeMap
					for (int j = 0; j < instrCnt; j++)
					{
						if (outDest == codeMap[j])
						{
							*(DWORD*)(outCodeBuf + outCodeMap[i] + 5 - jecxcorr) = outCodeMap[j] - outCodeMap[i];
							break;
						}
					}
				}
			}
			{		
				{
					int tmpChr = 0;
					do
					{
						polyEnc(outCodeBuf + outCodeMap[i] + tmpChr + 1, *(outCodeBuf + outCodeMap[i] + tmpChr), outCodeMap[i] + tmpChr);
						BYTE __tt = *(outCodeBuf + outCodeMap[i] + tmpChr);						
						*(outCodeBuf + outCodeMap[i] + tmpChr) ^= *(outCodeBuf + outCodeMap[i] + tmpChr + 1);
						tmpChr += __tt + 1;
					}
					while (outCodeMap[i + 1] != outCodeMap[i] + tmpChr);
				}
			}
		}
	}

	free(relocMap);
	free(codeMap);
	free(outCodeMap);
	return outPos;
}