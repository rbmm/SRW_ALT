#include "stdafx.h"

#if 1
#define _USE_PUSH_LOCK_
#include "pushlock.h"
#endif

struct ThreadTestData 
{
	HANDLE hEvent;
	SRWLOCK SRWLock = {};
	LONG numThreads = 1;
	LONG readCounter = 0;
	LONG bug = 0;

	void EndThread()
	{
		if (!InterlockedDecrementNoFence(&numThreads))
		{
			if (!SetEvent(hEvent)) __debugbreak();
		}
	}

	void DoStuff()
	{
		AcquireSRWLockShared(&SRWLock);

		InterlockedDecrementNoFence(&readCounter);

		ULONG64 time = GetTickCount64() + 1000;

		while (readCounter)
		{
			if (GetTickCount64() > time)
			{
				if (InterlockedExchangeNoFence(&bug, TRUE))
				{
					SleepEx(INFINITE, TRUE);
				}
				else
				{
					MessageBoxW(0, 0, 0, MB_ICONHAND);
				}
				__debugbreak();
			}

			SwitchToThread();
			//Sleep(1);
		}

		ReleaseSRWLockShared(&SRWLock);

		EndThread();
	}

	static ULONG WINAPI _S_DoStuff(PVOID data)
	{
		reinterpret_cast<ThreadTestData*>(data)->DoStuff();
		return 0;
	}

	void Test(ULONG n)
	{
		if (hEvent = CreateEventW(0, 0, 0, 0))
		{
			AcquireSRWLockExclusive(&SRWLock);

			do 
			{
				numThreads++;
				readCounter++;

				if (HANDLE hThread = CreateThread(0, 0, _S_DoStuff, this, 0, 0))
				{
					CloseHandle(hThread);
				}
				else
				{
					readCounter--;
					numThreads--;
				}

			} while (--n);

			ReleaseSRWLockExclusive(&SRWLock);

			EndThread();

			if (WAIT_OBJECT_0 != WaitForSingleObject(hEvent, INFINITE))
			{
				__debugbreak();
			}

			CloseHandle(hEvent);
		}
	}
};

void DoSrwTest(ULONG nThreads)
{
	ThreadTestData data;
	data.Test(nThreads);
}

void DoSrwTest(ULONG nLoops, ULONG nThreads)
{
	do 
	{
		DoSrwTest(nThreads);
	} while (--nLoops);
}