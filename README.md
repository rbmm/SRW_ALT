##SRW lock - when shared request suddenly transformed to exclusive.

the following [question](https://stackoverflow.com/questions/78090862/stdshared-mutexunlock-shared-blocks-even-though-there-are-no-active-exclus) was asked about the problem with SRW lock


- Main thread acquires exclusive lock
- Main thread creates N children threads
- Main thread releases exclusive lock and at same time Each child thread try Acquires a shared lock
- Spins until all children have acquired a shared lock
- Releases the shared lock

This code works most of the time. But sometime it deadlocks.

more exactly - one (always one !) children thread was inside lock and all another wait in call `AcquireSRWLockShared`
but why another children threads can not enter lock ?! Main thread correct release exclusive access, so why ?

in comments developer also provide some values from SRW lock during deadlock

> 0x7263aff6f3, 0xedf26ffb63, 0xb5606ff843, 0x1e6e8ff753, 0xa811bffae3 - Always ends with a 3.

really SRW lock have identical [implementation](https://github.com/mic101/windows/blob/master/WRK-v1.2/base/ntos/ex/pushlock.c) and structure as PushLock in kernel

this values (3 is L == Lock bit + W == Waiters present ) and P instead SC suggest that thread ( 25108 on picture )

![threads stacks](https://i.stack.imgur.com/0z66o.png)

hold lock in exclusive mode instead shared. despite thread ask for shared

and for check this - if we with debugger or bit another code - break thread (25108) from loop - all another threads will be wake from` AcquireSRWLockShared`

at first i create demo project, which show that after child thread release SRW lock (by timeout ), deadlock is gone

then under debugger (by synthetically suspend and resume threads) i reproduce situation.

steps to reproduce - we need only 2 working threads - **A** and **B**. and main thread **M**.

thread **A** call `AcquireSRWLockShared` and begin wait
thread **M** call `ReleaseSRWLockExclusive`, when he enter `RtlpWakeSRWLock` we need suspend M at this point ( in real case sheduler can do it ). at this point section already unlocked !
thread **B** call `AcquireSRWLockShared` and enter lock, because SRW is unlocked
thread **M** continue execution `RtlpWakeSRWLock` but now NOT wake thread **A**
when (and if in case your code) thread B call `ReleaseSRWLockShared`, `RtlpWakeSRWLock` will be called again and now thread **A** will be waked

or by another words - `ReleaseSRWLockExclusive` first remove **L**ock bit and then, if **W**aiters present, walk by Wait Blocks for wake waiters ( `RtlpWakeSRWLock` )
but this 2 operations not atomic. in between, just after **L**ock bit removed, another thread can acquire the lock. and in this case acquire always will be exclusive by fact, even if thread ask for shared access only
