#pragma once
#ifndef __ERROR_H__
#define __ERROR_H__
#include <iostream>

void Error(const std::wstring);


// ERROR2 ���ع�
#define ERROR2(a) \
	{ \
		MessageBox(0, a, TEXT("Error"), MB_ICONERROR); \
		GlobalFree(protectedCodeOffset); \
		GlobalFree(items2); \
		vm_free(); \
		return; \
	}
#endif // !__ERROR_H__

