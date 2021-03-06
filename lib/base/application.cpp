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

#include "base/application.h"
#include "base/stacktrace.h"
#include "base/timer.h"
#include "base/logger_fwd.h"
#include "base/exception.h"
#include "base/objectlock.h"
#include "base/utility.h"
#include "base/debug.h"
#include <sstream>
#include <boost/algorithm/string/classification.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/exception/errinfo_api_function.hpp>
#include <boost/exception/errinfo_errno.hpp>
#include <boost/exception/errinfo_file_name.hpp>
#include <iostream>

using namespace icinga;

Application *Application::m_Instance = NULL;
bool Application::m_ShuttingDown = false;
bool Application::m_Debugging = false;
String Application::m_PrefixDir;
String Application::m_LocalStateDir;
String Application::m_PkgLibDir;
String Application::m_PkgDataDir;
int Application::m_ArgC;
char **Application::m_ArgV;

/**
 * Constructor for the Application class.
 */
void Application::Start(void)
{
	DynamicObject::Start();

	m_PidFile = NULL;

#ifdef _WIN32
	/* disable GUI-based error messages for LoadLibrary() */
	SetErrorMode(SEM_FAILCRITICALERRORS);

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("WSAStartup")
		    << errinfo_win32_error(WSAGetLastError()));
	}
#endif /* _WIN32 */

#ifdef _WIN32
	if (IsDebuggerPresent())
		m_Debugging = true;
#endif /* _WIN32 */

	ASSERT(m_Instance == NULL);
	m_Instance = this;
}

/**
 * Destructor for the application class.
 */
void Application::Stop(void)
{
	m_ShuttingDown = true;

#ifdef _WIN32
	WSACleanup();
#endif /* _WIN32 */

	ClosePidFile();
}

Application::~Application(void)
{
	m_Instance = NULL;
}

/**
 * Retrieves a pointer to the application singleton object.
 *
 * @returns The application object.
 */
Application::Ptr Application::GetInstance(void)
{
	if (!m_Instance)
		return Application::Ptr();

	return m_Instance->GetSelf();
}

int Application::GetArgC(void)
{
	return m_ArgC;
}

void Application::SetArgC(int argc)
{
	m_ArgC = argc;
}

char **Application::GetArgV(void)
{
	return m_ArgV;
}

void Application::SetArgV(char **argv)
{
	m_ArgV = argv;
}

void Application::ShutdownTimerHandler(void)
{
	if (m_ShuttingDown) {
		Log(LogInformation, "base", "Shutting down Icinga...");
		Application::GetInstance()->OnShutdown();

		DynamicObject::StopObjects();
		GetTP().Stop();
		m_ShuttingDown = false;
	}
}

/**
 * Processes events for registered sockets and timers and calls whatever
 * handlers have been set up for these events.
 */
void Application::RunEventLoop(void) const
{
	/* Start the system time watch thread. */
	boost::thread t(&Application::TimeWatchThreadProc);
	t.detach();

	/* Set up a timer that watches the m_Shutdown flag. */
	Timer::Ptr shutdownTimer = boost::make_shared<Timer>();
	shutdownTimer->OnTimerExpired.connect(boost::bind(&Application::ShutdownTimerHandler));
	shutdownTimer->SetInterval(0.5);
	shutdownTimer->Start();

	Timer::Initialize();

	GetTP().Join();

	Timer::Uninitialize();
}

/**
 * Watches for changes to the system time. Adjusts timers if necessary.
 */
void Application::TimeWatchThreadProc(void)
{
	double lastLoop = Utility::GetTime();

	for (;;) {
		Utility::Sleep(5);

		double now = Utility::GetTime();
		double timeDiff = lastLoop - now;

		if (abs(timeDiff) > 15) {
			/* We made a significant jump in time. */
			std::ostringstream msgbuf;
			msgbuf << "We jumped "
			       << (timeDiff < 0 ? "forward" : "backward")
			       << " in time: " << abs(timeDiff) << " seconds";
			Log(LogInformation, "base", msgbuf.str());

			Timer::AdjustTimers(-timeDiff);
		}

		lastLoop = now;
	}
}

/**
 * Signals the application to shut down during the next
 * execution of the event loop.
 */
void Application::RequestShutdown(void)
{
	m_ShuttingDown = true;
}

/**
 * Retrieves the full path of the executable.
 *
 * @param argv0 The first command-line argument.
 * @returns The path.
 */
String Application::GetExePath(const String& argv0)
{
	String executablePath;

#ifndef _WIN32
	char buffer[MAXPATHLEN];
	if (getcwd(buffer, sizeof(buffer)) == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("getcwd")
		    << boost::errinfo_errno(errno));
	}

	String workingDirectory = buffer;

	if (argv0[0] != '/')
		executablePath = workingDirectory + "/" + argv0;
	else
		executablePath = argv0;

	bool foundSlash = false;
	for (size_t i = 0; i < argv0.GetLength(); i++) {
		if (argv0[i] == '/') {
			foundSlash = true;
			break;
		}
	}

	if (!foundSlash) {
		const char *pathEnv = getenv("PATH");
		if (pathEnv != NULL) {
			std::vector<String> paths;
			boost::algorithm::split(paths, pathEnv, boost::is_any_of(":"));

			bool foundPath = false;
			BOOST_FOREACH(String& path, paths) {
				String pathTest = path + "/" + argv0;

				if (access(pathTest.CStr(), X_OK) == 0) {
					executablePath = pathTest;
					foundPath = true;
					break;
				}
			}

			if (!foundPath) {
				executablePath.Clear();
				BOOST_THROW_EXCEPTION(std::runtime_error("Could not determine executable path."));
			}
		}
	}

	if (realpath(executablePath.CStr(), buffer) == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("realpath")
		    << boost::errinfo_errno(errno)
		    << boost::errinfo_file_name(executablePath));
	}

	return buffer;
#else /* _WIN32 */
	char FullExePath[MAXPATHLEN];

	if (!GetModuleFileName(NULL, FullExePath, sizeof(FullExePath)))
		BOOST_THROW_EXCEPTION(win32_error()
		    << boost::errinfo_api_function("GetModuleFileName")
		    << errinfo_win32_error(GetLastError()));

	return FullExePath;
#endif /* _WIN32 */
}

/**
 * Sets whether debugging is enabled.
 *
 * @param debug Whether to enable debugging.
 */
void Application::SetDebugging(bool debug)
{
	m_Debugging = debug;
}

/**
 * Retrieves the debugging mode of the application.
 *
 * @returns true if the application is being debugged, false otherwise
 */
bool Application::IsDebugging(void)
{
	return m_Debugging;
}

/**
 * Displays a message that tells users what to do when they encounter a bug.
 */
void Application::DisplayBugMessage(void)
{
	std::cerr << "***" << std::endl
		  << "*** This would indicate a bug in Icinga 2. Please submit a bug report at https://dev.icinga.org/ and include" << std::endl
		  << "*** this stack trace as well as any other information that might be useful in order to reproduce this problem." << std::endl
		  << "***" << std::endl
		  << std::endl;
}

#ifndef _WIN32
/**
 * Signal handler for SIGINT. Prepares the application for cleanly
 * shutting down during the next execution of the event loop.
 *
 * @param - The signal number.
 */
void Application::SigIntHandler(int)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sa, NULL);

	Application::Ptr instance = Application::GetInstance();

	if (!instance)
		return;

	instance->RequestShutdown();
}

/**
 * Signal handler for SIGABRT. Helps with debugging ASSERT()s.
 *
 * @param - The signal number.
 */
void Application::SigAbrtHandler(int)
{
#ifndef _WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGABRT, &sa, NULL);
#endif /* _WIN32 */

	std::cerr << "Caught SIGABRT." << std::endl;

	StackTrace trace;
	trace.Print(std::cerr, 1);

	DisplayBugMessage();
}
#else /* _WIN32 */
/**
 * Console control handler. Prepares the application for cleanly
 * shutting down during the next execution of the event loop.
 */
BOOL WINAPI Application::CtrlHandler(DWORD type)
{
	Application::Ptr instance = Application::GetInstance();

	if (!instance)
		return TRUE;

	instance->RequestShutdown();

	SetConsoleCtrlHandler(NULL, FALSE);
	return TRUE;
}
#endif /* _WIN32 */

/**
 * Handler for unhandled exceptions.
 */
void Application::ExceptionHandler(void)
{
#ifndef _WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGABRT, &sa, NULL);
#endif /* _WIN32 */

	bool has_trace = false;

	try {
		throw;
	} catch (const std::exception& ex) {
		std::cerr << std::endl
			  << boost::diagnostic_information(ex)
			  << std::endl;

		has_trace = (boost::get_error_info<StackTraceErrorInfo>(ex) != NULL);
	} catch (...) {
		std::cerr << "Exception of unknown type." << std::endl;
	}

	if (!has_trace) {
		StackTrace trace;
		trace.Print(std::cerr, 1);
	}

	DisplayBugMessage();

	abort();
}

#ifdef _WIN32
LONG CALLBACK Application::SEHUnhandledExceptionFilter(PEXCEPTION_POINTERS exi)
{
	std::cerr << "Unhandled SEH exception." << std::endl;

	StackTrace trace(exi);
	trace.Print(std::cerr, 1);

	DisplayBugMessage();

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif /* _WIN32 */

/**
 * Installs the exception handlers.
 */
void Application::InstallExceptionHandlers(void)
{
	std::set_terminate(&Application::ExceptionHandler);

#ifndef _WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &Application::SigAbrtHandler;
	sigaction(SIGABRT, &sa, NULL);
#else /* _WIN32 */
	SetUnhandledExceptionFilter(&Application::SEHUnhandledExceptionFilter);
#endif /* _WIN32 */
}

/**
 * Runs the application.
 *
 * @returns The application's exit code.
 */
int Application::Run(void)
{
	int result;

#ifndef _WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &Application::SigIntHandler;
	sigaction(SIGINT, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
#else /* _WIN32 */
	SetConsoleCtrlHandler(&Application::CtrlHandler, TRUE);
#endif /* _WIN32 */

	result = Main();

	return result;
}

/**
 * Grabs the PID file lock and updates the PID. Terminates the application
 * if the PID file is already locked by another instance of the application.
 *
 * @param filename The name of the PID file.
 */
void Application::UpdatePidFile(const String& filename)
{
	ASSERT(!OwnsLock());
	ObjectLock olock(this);

	if (m_PidFile != NULL)
		fclose(m_PidFile);

	/* There's just no sane way of getting a file descriptor for a
	 * C++ ofstream which is why we're using FILEs here. */
	m_PidFile = fopen(filename.CStr(), "r+");

	if (m_PidFile == NULL)
		m_PidFile = fopen(filename.CStr(), "w");

	if (m_PidFile == NULL)
		BOOST_THROW_EXCEPTION(std::runtime_error("Could not open PID file '" + filename + "'"));

#ifndef _WIN32
	int fd = fileno(m_PidFile);

	Utility::SetCloExec(fd);

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		Log(LogCritical, "base", "Could not lock PID file. Make sure that only one instance of the application is running.");

		_exit(EXIT_FAILURE);
	}

	if (ftruncate(fd, 0) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("ftruncate")
		    << boost::errinfo_errno(errno));
	}
#endif /* _WIN32 */

	fprintf(m_PidFile, "%d", Utility::GetPid());
	fflush(m_PidFile);
}

/**
 * Closes the PID file. Does nothing if the PID file is not currently open.
 */
void Application::ClosePidFile(void)
{
	ASSERT(!OwnsLock());
	ObjectLock olock(this);

	if (m_PidFile != NULL)
		fclose(m_PidFile);

	m_PidFile = NULL;
}

/**
 * Retrieves the path of the installation prefix.
 *
 * @returns The path.
 */
String Application::GetPrefixDir(void)
{
	if (m_PrefixDir.IsEmpty())
		return ".";
	else
		return m_PrefixDir;
}

/**
 * Sets the path for the installation prefix.
 *
 * @param path The new path.
 */
void Application::SetPrefixDir(const String& path)
{
	m_PrefixDir = path;
}

/**
 * Retrieves the path for the local state dir.
 *
 * @returns The path.
 */
String Application::GetLocalStateDir(void)
{
	if (m_LocalStateDir.IsEmpty())
		return "./var";
	else
		return m_LocalStateDir;
}

/**
 * Sets the path for the local state dir.
 *
 * @param path The new path.
 */
void Application::SetLocalStateDir(const String& path)
{
	m_LocalStateDir = path;
}

/**
 * Retrieves the path for the package lib dir.
 *
 * @returns The path.
 */
String Application::GetPkgLibDir(void)
{
	if (m_PkgLibDir.IsEmpty())
		return ".";
	else
		return m_PkgLibDir;
}

/**
 * Sets the path for the package lib dir.
 *
 * @param path The new path.
 */
void Application::SetPkgLibDir(const String& path)
{
	m_PkgLibDir = path;
}

/**
 * Retrieves the path for the package data dir.
 *
 * @returns The path.
 */
String Application::GetPkgDataDir(void)
{
        if (m_PkgDataDir.IsEmpty())
                return ".";
        else
                return m_PkgDataDir;
}

/**
 * Sets the path for the package data dir.
 *
 * @param path The new path.
 */
void Application::SetPkgDataDir(const String& path)
{
        m_PkgDataDir = path;
}

/**
 * Returns the global thread pool.
 *
 * @returns The global thread pool.
 */
ThreadPool& Application::GetTP(void)
{
	static ThreadPool tp;
	return tp;
}
