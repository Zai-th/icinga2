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

#include "livestatus/timeperiodstable.h"
#include "icinga/icingaapplication.h"
#include "icinga/timeperiod.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/convert.h"
#include "base/utility.h"
#include <boost/foreach.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace icinga;
using namespace livestatus;

TimePeriodsTable::TimePeriodsTable(void)
{
	AddColumns(this);
}

void TimePeriodsTable::AddColumns(Table *table, const String& prefix,
    const Column::ObjectAccessor& objectAccessor)
{
	table->AddColumn(prefix + "name", Column(&TimePeriodsTable::NameAccessor, objectAccessor));
	table->AddColumn(prefix + "alias", Column(&TimePeriodsTable::AliasAccessor, objectAccessor));
	table->AddColumn(prefix + "in", Column(&TimePeriodsTable::InAccessor, objectAccessor));
}

String TimePeriodsTable::GetName(void) const
{
	return "timeperiod";
}

void TimePeriodsTable::FetchRows(const AddRowFunction& addRowFn)
{
	BOOST_FOREACH(const TimePeriod::Ptr& tp, DynamicType::GetObjects<TimePeriod>()) {
		addRowFn(tp);
	}
}

Value TimePeriodsTable::NameAccessor(const Value& row)
{
	return static_cast<TimePeriod::Ptr>(row)->GetName();
}

Value TimePeriodsTable::AliasAccessor(const Value& row)
{
	return static_cast<TimePeriod::Ptr>(row)->GetDisplayName();
}

Value TimePeriodsTable::InAccessor(const Value& row)
{
	return (static_cast<TimePeriod::Ptr>(row)->IsInside(Utility::GetTime()) ? 1 : 0);
}


