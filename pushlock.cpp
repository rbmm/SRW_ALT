#include "stdafx.h"

#pragma warning(disable : 4706)

#define ASSERT(x) if (!(x)) __debugbreak();

#include "pushlock.h"

EXTERN_C_START

NTSYSCALLAPI
NTSTATUS
NTAPI
NtAlertThreadByThreadId(
						_In_ HANDLE ThreadId
						);

// rev
NTSYSCALLAPI
NTSTATUS
NTAPI
NtWaitForAlertByThreadId(
						 _In_ PVOID Address,
						 _In_opt_ PLARGE_INTEGER Timeout
						 );

EXTERN_C_END

ULONG CPushLock::WaitBlock::SpinCount = 0x400;

inline void CPushLock::WaitBlock::Wake()
{
	if (!InterlockedBitTestAndResetRelease(&Flags, BIT_SPINNING))
	{
		// thread begin wait
		NtAlertThreadByThreadId(ThreadId);
	}
}

void CPushLock::WakeExclusiveWaiter(WaitBlock* last, WaitBlock* prev, ULONG_PTR CurrentValue)
{
	if (prev)
	{
		// was multiple waiters. unlink last
		prev->Next = 0;
	}
	else
	{
		// was single exclusive waiter

		ULONG_PTR NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
			(void**)&Value, (void*)(FLAG_LOCKED | FLAG_EXCLUSIVE), (void*)CurrentValue);

		if (NewValue != CurrentValue)
		{
			// additional threads insert wait block

			prev = (WaitBlock*)(NewValue & ~BITS_MASK);

			WaitBlock* Next;

			while ((Next = prev->Next) != last)
			{
				prev = Next;
			}

			// unlink last
			prev->Next = 0;
		}
	}

	last->Wake();
}

bool CPushLock::EnterWithWait(ULONG_PTR CurrentValue, LONG Flags)
{
	WaitBlock wb;

	wb.Flags = Flags, wb.ThreadId = (HANDLE)(ULONG_PTR)GetCurrentThreadId();

	if (CurrentValue & FLAG_EXCLUSIVE)
	{
		wb.Next = (WaitBlock*)(CurrentValue & ~BITS_MASK);
		wb.SC = 0;
	}
	else
	{
		wb.Next = 0;
		wb.SC = (LONG)(CurrentValue >> BITS_COUNT);
	}

	// push wait block

	ULONG_PTR NewValue = (ULONG_PTR)&wb | FLAG_EXCLUSIVE | FLAG_LOCKED;

	NewValue = (ULONG_PTR)InterlockedCompareExchangePointerAcquire(
		(void**)&Value, (void*)NewValue, (void*)CurrentValue);

	if (NewValue != CurrentValue)
	{
		return false;
	}

	// try spin first
	ULONG SpinCount = WaitBlock::SpinCount;

	do 
	{
		if (!BitTest(&wb.Flags, WaitBlock::BIT_SPINNING))
		{
			// we enter
			break;
		}
		YieldProcessor();
	} while (--SpinCount);

	if (InterlockedBitTestAndResetAcquire(&wb.Flags, WaitBlock::BIT_SPINNING))
	{
		NtWaitForAlertByThreadId(this, 0);
	}

	// we enter
	return true;
}

void CPushLock::AcquireExclusive()
{
	for (ULONG_PTR CurrentValue = Value; ; CurrentValue = Value)
	{
		if (CurrentValue)
		{
			if (EnterWithWait(CurrentValue, WaitBlock::FLAG_SPINNING | WaitBlock::FLAG_EXCLUSIVE))
			{
				return;
			}
		}
		else
		{
			if ((ULONG_PTR)InterlockedCompareExchangePointerAcquire((void**)&Value, 
				(void*)(FLAG_LOCKED | FLAG_EXCLUSIVE), 0) == 0)
			{
				return;
			}
		}
	}
}

void CPushLock::AcquireShared()
{
	for (ULONG_PTR CurrentValue = Value; ; CurrentValue = Value)
	{
		if (CurrentValue & FLAG_EXCLUSIVE)
		{
			if (EnterWithWait(CurrentValue, WaitBlock::FLAG_SPINNING))
			{
				return;
			}
		}
		else
		{
			if ((ULONG_PTR)InterlockedCompareExchangePointerAcquire((void**)&Value, 
				(void*)((CurrentValue + SHARE_INC) | FLAG_LOCKED), 
				(void*)CurrentValue) == CurrentValue)
			{
				return;
			}
		}
	}
}

void CPushLock::ReleaseShared()
{
	ULONG_PTR NewValue;

	for (ULONG_PTR CurrentValue = Value; ; CurrentValue = NewValue)
	{
		if (CurrentValue & FLAG_EXCLUSIVE)
		{
			WaitBlock* first = (WaitBlock*)(CurrentValue & ~BITS_MASK);
			WaitBlock* prev = 0, *pwb, *last = first;

			while (pwb = last->Next)
			{
				prev = last, last = pwb;
			}

			// first waiter must be exclusive !
			ASSERT(_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE));

			if (InterlockedDecrementRelease(&last->SC) == 0)
			{
				// last shared release
				// wake only single exclusive entry

				WakeExclusiveWaiter(last, prev, CurrentValue);
			}

			return ;
		}
		else
		{
			// nobody wait yet

			NewValue = CurrentValue - SHARE_INC;

			if (!(NewValue >> BITS_COUNT))
			{
				// last shared release
				NewValue &= ~FLAG_LOCKED;
			}

			NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
				(void**)&Value, (void*)NewValue, (void*)CurrentValue);

			if (NewValue == CurrentValue)
			{
				return;
			}

			// somebody begin wait
		}
	}
}

void CPushLock::ReleaseExclusive()
{
	ULONG_PTR NewValue;

	for (ULONG_PTR CurrentValue = Value; ; CurrentValue = NewValue)
	{
		ASSERT(CurrentValue & FLAG_EXCLUSIVE);

		if (WaitBlock* first = (WaitBlock*)(CurrentValue & ~BITS_MASK))
		{
			WaitBlock* prev = 0, *pwb, *lastExclusive = 0, *last = first;

			ULONG SC = 1;

			while (pwb = last->Next)
			{
				if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
				{
					lastExclusive = last;
				}

				prev = last, last = pwb, SC++;
			}

			if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
			{
				// wake only single exclusive entry

				WakeExclusiveWaiter(last, prev, CurrentValue);

				return ;
			}
			else
			{
				// need wake multiple shared entries

				for (;;)
				{
					if (lastExclusive)
					{
						// first shared waiter
						first = lastExclusive->Next;

						// unlink shared waiters
						lastExclusive->Next = 0;
						break;
					}

					// only shared waiters

					NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
						(void**)&Value, (void*)(ULONG_PTR)(FLAG_LOCKED | (SC << BITS_COUNT)), (void*)CurrentValue);

					if (NewValue == CurrentValue)
					{
						break;
					}

					// additional threads insert wait block

					CurrentValue = NewValue;

					pwb = last = (WaitBlock*)(NewValue & ~BITS_MASK);

					do 
					{
						if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
						{
							lastExclusive = last;
						}

						SC++;

					} while ((last = last->Next) != first);

					first = pwb;
				}

				// build wake list (in FIFO order) and calc Shared Count
				WaitBlock* WakeList = 0;
				SC = 0;
				do 
				{
					SC++;
					prev = first;
					first = first->Next;
					prev->Next = WakeList;
					WakeList = prev;
				} while (first);

				if (lastExclusive)
				{
					lastExclusive->SC = SC;
				}

				// wake threads from wakelist

				do 
				{
					pwb = WakeList;
					WakeList = WakeList->Next;
					pwb->Wake();
				} while (WakeList);

				return;
			}
		}
		else
		{
			// nobody wait yet

			ASSERT(CurrentValue == (FLAG_LOCKED | FLAG_EXCLUSIVE));

			NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
				(void**)&Value, 0, (void*)(FLAG_LOCKED | FLAG_EXCLUSIVE));

			if (NewValue == (FLAG_LOCKED | FLAG_EXCLUSIVE))
			{
				return;
			}

			// somebody begin wait
		}
	}
}

bool CPushLock::TryAcquireShared()
{
	ULONG_PTR CurrentValue = Value;

	if (!(CurrentValue & FLAG_EXCLUSIVE))
	{
		if ((ULONG_PTR)InterlockedCompareExchangePointerAcquire((void**)&Value, 
			(void*)((CurrentValue + SHARE_INC) | FLAG_LOCKED), 
			(void*)CurrentValue) == CurrentValue)
		{
			return true;
		}
	}

	return false;
}

bool CPushLock::TryAcquireExclusive()
{
	ULONG_PTR CurrentValue = Value;

	if (!CurrentValue)
	{
		if ((ULONG_PTR)InterlockedCompareExchangePointerAcquire((void**)&Value, 
			(void*)(FLAG_LOCKED | FLAG_EXCLUSIVE), 0) == 0)
		{
			return true;
		}
	}

	return false;
}

void CPushLock::ConvertExclusiveToShared()
{
	ULONG_PTR NewValue;

	for (ULONG_PTR CurrentValue = Value; ; CurrentValue = NewValue)
	{
		ASSERT(CurrentValue & FLAG_EXCLUSIVE);

		if (WaitBlock* first = (WaitBlock*)(CurrentValue & ~BITS_MASK))
		{
			WaitBlock* prev = 0, *pwb, *lastExclusive = 0, *last = first;

			ULONG SC = 2; // !! we will be +1 shared

			while (pwb = last->Next)
			{
				if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
				{
					lastExclusive = last;
				}

				prev = last, last = pwb, SC++;
			}

			if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
			{
				ASSERT(last->SC == 0);
				last->SC = 1;
				// nothing todo, any shared entry can not be waked

				return ;
			}
			else
			{
				// need wake multiple shared entries

				for (;;)
				{
					if (lastExclusive)
					{
						// first shared waiter
						first = lastExclusive->Next;

						// unlink shared waiters
						lastExclusive->Next = 0;
						break;
					}

					// only shared waiters

					NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
						(void**)&Value, (void*)(ULONG_PTR)(FLAG_LOCKED | (SC << BITS_COUNT)), (void*)CurrentValue);

					if (NewValue == CurrentValue)
					{
						break;
					}

					// additional threads insert wait block

					CurrentValue = NewValue;

					pwb = last = (WaitBlock*)(NewValue & ~BITS_MASK);

					do 
					{
						if (_bittest(&last->Flags, WaitBlock::BIT_EXCLUSIVE))
						{
							lastExclusive = last;
						}

						SC++;

					} while ((last = last->Next) != first);

					first = pwb;
				}

				// build wake list (in FIFO order) and calc Shared Count
				WaitBlock* WakeList = 0;
				SC = 1; // !! we will be +1 shared
				do 
				{
					SC++;
					prev = first;
					first = first->Next;
					prev->Next = WakeList;
					WakeList = prev;
				} while (first);

				if (lastExclusive)
				{
					lastExclusive->SC = SC;
				}

				// wake threads from wakelist

				do 
				{
					pwb = WakeList;
					WakeList = WakeList->Next;
					pwb->Wake();
				} while (WakeList);

				return;
			}
		}
		else
		{
			// nobody wait yet

			ASSERT(CurrentValue == (FLAG_LOCKED | FLAG_EXCLUSIVE));

			NewValue = (ULONG_PTR)InterlockedCompareExchangePointerRelease(
				(void**)&Value, (void*)(FLAG_LOCKED | SHARE_INC), (void*)(FLAG_LOCKED | FLAG_EXCLUSIVE));

			if (NewValue == (FLAG_LOCKED | SHARE_INC))
			{
				return;
			}

			// somebody begin wait
		}
	}

}