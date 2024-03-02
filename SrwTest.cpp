#include "stdafx.h"

#if 1
#include "pushlock.h"

#define SRWLOCK CPushLock
#define AcquireSRWLockExclusive(p)	(p)->AcquireExclusive();
#define ReleaseSRWLockExclusive(p)	(p)->ReleaseExclusive();
#define AcquireSRWLockShared(p)	(p)->AcquireShared()
#define ReleaseSRWLockShared(p)	(p)->ReleaseShared()

#endif

struct ThreadTestData 
{
	HANDLE hEvent;
	SRWLOCK SRWLock = {};
	LONG numThreads = 1;
	LONG readCounter = 0;

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

		ULONG n = 0, m = 0, k = InterlockedDecrementNoFence(&readCounter);

		while (readCounter)
		{
			switch (NtYieldExecution())
			{
			case STATUS_SUCCESS:
				n++;
				break;
			case STATUS_NO_YIELD_PERFORMED:
				m++;
				break;
			default:
				__debugbreak();
			}
		}

		ReleaseSRWLockShared(&SRWLock);

		DbgPrint("%x> %x/%x\n", k, n, m);

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