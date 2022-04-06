// Copyright Proletariat, Inc. All Rights Reserved.
#include "variable/HistoricalVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"

using namespace csolver;

SolverDecisionLevel HistoricalVariableDatabase::getDecisionLevel() const
{
	return m_db->getDecisionLevel();
}

SolverTimestamp HistoricalVariableDatabase::getTimestamp() const
{
	return m_db->getTimestamp();
}

SolverDecisionLevel HistoricalVariableDatabase::getDecisionLevelForVariable(VarID varID) const
{
	return m_db->getDecisionLevelForVariable(varID);
}

const ValueSet& HistoricalVariableDatabase::getPotentialValues(VarID varID) const
{
	return m_db->getValueBefore(varID, m_timestamp);
}

SolverTimestamp HistoricalVariableDatabase::getLastModificationTimestamp(VarID varID) const
{
	return m_db->getModificationTimePriorTo(varID, m_timestamp);
}

const ValueSet& HistoricalVariableDatabase::getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp) const
{
	cs_assert(timestamp <= m_timestamp);
	return m_db->getValueBefore(variable, timestamp, outTimestamp);
}

const ValueSet& HistoricalVariableDatabase::getValueAfter(VarID variable, SolverTimestamp timestamp) const
{
	cs_assert(timestamp <= m_timestamp);
	return m_db->getValueAfter(variable, timestamp);
}

SolverDecisionLevel HistoricalVariableDatabase::getDecisionLevelForTimestamp(SolverTimestamp timestamp) const
{
	return m_db->getDecisionLevelForTimestamp(timestamp);
}

SolverTimestamp HistoricalVariableDatabase::getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const
{
	cs_assert(timestamp <= m_timestamp);
	return m_db->getModificationTimePriorTo(variable, timestamp);
}

const ValueSet& HistoricalVariableDatabase::getInitialValues(VarID varID) const
{
	return m_db->getInitialValues(varID);
}
