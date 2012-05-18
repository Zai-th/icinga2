/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "i2-base.h"

using namespace icinga;

/**
 * Helper function that deals with OS-specific differences in the thread
 * proc's function signature.
 */
#ifdef _WIN32
static DWORD WINAPI ThreadStartProc(LPVOID param)
{
	ThreadParameters *tparam = (ThreadParameters *)param;
	tparam->Callback(tparam->UserParams);
	delete tparam;
	return 0;
}
#else /* _WIN32 */
static void *ThreadStartProc(void *param)
{
	ThreadParameters *tparam = (ThreadParameters *)param;
	tparam->Callback(tparam->UserParams);
	delete tparam;
	return NULL;
}
#endif /* _WIN32 */

/**
 * Constructor for the thread class. Creates a new thread that begins
 * executing immediately.
 */
Thread::Thread(ThreadProc callback, void *param)
{
	ThreadParameters *tparam = new ThreadParameters();

	if (tparam == NULL)
		throw OutOfMemoryException("Out of memory");

	tparam->Callback = callback;
	tparam->UserParams = param;

#ifdef _WIN32
	m_Thread = CreateThread(NULL, 0, ThreadStartProc, tparam, CREATE_SUSPENDED, NULL);

	if (m_Thread == NULL)
		throw Win32Exception("CreateThread failed.", GetLastError());
#else /* _WIN32 */
	if (pthread_create(&m_Thread, NULL, ThreadStartProc, &tparam) < 0)
		throw PosixException("pthread_create failed.", errno);
#endif /* _WIN32 */
}

/**
 * Destructor for the Thread class. Cleans up the resources associated
 * with the thread.
 */
Thread::~Thread(void)
{
#ifdef _WIN32
	CloseHandle(m_Thread);
#else /* _WIN32 */
	/* nothing to do here */
#endif
}

/**
 * Waits until the thread has finished executing.
 */
void Thread::Join(void)
{
#ifdef _WIN32
	WaitForSingleObject(m_Thread, INFINITE);
#else /* _WIN32 */
	pthread_join(m_Thread, NULL);
#endif
}