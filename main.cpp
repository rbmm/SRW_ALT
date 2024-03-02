#include "stdafx.h"

void DoSrwTest(ULONG nLoops, ULONG nThreads);

ULONG GetNumberOfProcessors()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
}

BOOL IsRunOk(NTSTATUS status, ULONG nLoops, ULONG nThreads)
{
	wchar_t msg[0x100];
	if (status)
	{
		if (FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
			GetModuleHandleW(L"ntdll"), status, 0, msg, _countof(msg), 0))
		{
			MessageBoxW(0, msg, 0, MB_ICONWARNING);
		}

		return FALSE;
	}
	swprintf_s(msg, _countof(msg), L"run %u loops with %u threads ?", nLoops, nThreads);

	return MessageBoxW(0, msg, L"Test", MB_ICONQUESTION | MB_OKCANCEL) == IDOK;
}

void WINAPI ep(PWSTR pszCmdLine)
{
	pszCmdLine = GetCommandLineW();

	NTSTATUS status = STATUS_INVALID_PARAMETER_1;

	ULONG nLoops = 0, nThreads = 0;
	// *nLoops[*nThreads]
	if (pszCmdLine = wcschr(pszCmdLine, '*'))
	{
		if (nLoops = wcstoul(pszCmdLine + 1, &pszCmdLine, 10))
		{
			switch (*pszCmdLine)
			{
			case '*':
				if (nThreads = wcstoul(pszCmdLine + 1, &pszCmdLine, 10))
				{
					break;
				}
				[[fallthrough]];
			case 0:
				SYSTEM_INFO si;
				GetSystemInfo(&si);
				nThreads = GetNumberOfProcessors();
				break;
			}

			status = *pszCmdLine || nThreads > 0xFF ? STATUS_INVALID_PARAMETER_2 : STATUS_SUCCESS;
		}
	}

	if (IsRunOk(status, nLoops, nThreads))
	{
		DoSrwTest(nLoops, nThreads);
		MessageBoxW(0, 0, L"Done !", MB_ICONINFORMATION);
	}

	ExitProcess(status);
}