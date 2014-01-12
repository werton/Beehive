///////////////////////////////////////////////////
// File:		Semaphore.cpp
// Date:		8th January 2014
// Authors:		Matt Phillips
// Description:	Threading and synchronisation
///////////////////////////////////////////////////

#include "core/thread/Semaphore.h"

namespace ion
{
	namespace thread
	{
		Semaphore::Semaphore(int maxSignalCount)
		{
			#if defined ION_PLATFORM_WINDOWS
			mSemaphoreHndl = CreateSemaphore(NULL, 0, maxSignalCount, NULL);
			#endif
		}

		Semaphore::~Semaphore()
		{
			#if defined ION_PLATFORM_WINDOWS
			CloseHandle(mSemaphoreHndl);
			#endif
		}

		void Semaphore::Signal()
		{
			#if defined ION_PLATFORM_WINDOWS
			ReleaseSemaphore(mSemaphoreHndl, 1, NULL);
			#endif
		}

		void Semaphore::Wait()
		{
			#if defined ION_PLATFORM_WINDOWS
			WaitForSingleObject(mSemaphoreHndl, INFINITE);
			#endif
		}
	}
}