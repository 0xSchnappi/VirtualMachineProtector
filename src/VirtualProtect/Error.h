#pragma once
#ifndef __ERROR_H__
#define __ERROR_H__
#include <iostream>
#include <assert.h>
void Error(const std::wstring);


// ERROR2 ���ع�
#define ERROR2(a) \
	{ \
		MessageBox(0, a, TEXT("Error"), MB_ICONERROR); \
		free(protectedCodeOffset); \
		free(items2); \
		return; \
	}
#endif // !__ERROR_H__

