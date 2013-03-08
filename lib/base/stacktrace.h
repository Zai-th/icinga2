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

#ifndef STACKTRACE_H
#define STACKTRACE_H

namespace icinga
{

/**
 * A stacktrace.
 *
 * @ingroup base
 */
class StackTrace
{
public:
	StackTrace(void);
#ifdef _WIN32
	StackTrace(PEXCEPTION_POINTERS exi);
#endif /* _WIN32 */

	void Print(ostream& fp, int ignoreFrames = 0) const;

private:
	void *m_Frames[64];
	int m_Count;

	static boost::once_flag m_OnceFlag;

	static void Initialize(void);
};

I2_BASE_API ostream& operator<<(ostream& stream, const StackTrace& trace);

}

#endif /* UTILITY_H */