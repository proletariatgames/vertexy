// Copyright Proletariat, Inc. All Rights Reserved.
#include "variable/CommittableVariableDatabase.h"

using namespace Vertexy;

const ValueSet& CommittableVariableDatabase::getPotentialValues(VarID varID) const
{
	int ridx = indexOfPredicate(m_modifications.rbegin(), m_modifications.rend(), [&](auto& mod) { return mod.variable == varID; });
	if (ridx >= 0)
	{
		int idx = (m_modifications.size()-1) - ridx;
		vxy_sanity(m_modifications[idx].variable == varID);
		return m_modifications[idx].value;
	}
	else
	{
		return m_parent->getPotentialValues(varID);
	}
}

const ValueSet& CommittableVariableDatabase::getInitialValues(VarID variable) const
{
	return m_parent->getInitialValues(variable);
}

bool CommittableVariableDatabase::commitPastAndFutureChanges()
{
	if (!m_committed)
	{
		m_committed = true;

		vxy_assert(!m_hasContradiction);
		vxy_assert(!m_lockedVar.isValid());
		for (auto& mod : m_modifications)
		{
			if (!m_parent->constrainToValues(mod.variable, mod.value, m_outerCons, m_outerSink->committableDatabaseWrapExplanation(*this, mod.explainer)))
			{
				m_hasContradiction = true;
				return false;
			}
		}
		m_modifications.clear();
	}
	vxy_assert(m_modifications.empty());
	return true;
}

ValueSet& CommittableVariableDatabase::lockVariableImpl(VarID varID)
{
	vxy_assert(!m_lockedVar.isValid());
	m_lockedVar = varID;
	m_lockedValues = getPotentialValues(varID);
	return m_lockedValues;
}

void CommittableVariableDatabase::unlockVariableImpl(VarID varID, bool wasChanged, IConstraint* constraint, ExplainerFunction explainer)
{
	vxy_assert(m_lockedVar == varID);
	vxy_assert(!m_hasContradiction);
	m_lockedVar = VarID::INVALID;
	if (wasChanged)
	{
		if (m_committed)
		{
			m_parent->constrainToValues(varID, m_lockedValues, m_outerCons, m_outerSink->committableDatabaseWrapExplanation(*this, explainer));
		}
		else
		{
			m_modifications.push_back({varID, m_lockedValues, constraint, explainer});
		}
	}
}

void CommittableVariableDatabase::onContradiction(VarID varID, IConstraint* constraint, const ExplainerFunction& explainer)
{
	vxy_assert(!m_hasContradiction);
	m_hasContradiction = true;
	m_outerSink->committableDatabaseContradictionFound(*this, varID, constraint, explainer);
}

void CommittableVariableDatabase::queueConstraintPropagation(IConstraint* constraint)
{
	m_outerSink->committableDatabaseQueueRequest(*this, constraint);
}

SolverDecisionLevel CommittableVariableDatabase::getDecisionLevelForVariable(VarID varID) const
{
	return m_parent->getDecisionLevelForVariable(varID);
}

SolverTimestamp CommittableVariableDatabase::getLastModificationTimestamp(VarID variable) const
{
	int ridx = indexOfPredicate(m_modifications.rbegin(), m_modifications.rend(), [&](auto& mod) { return mod.variable == variable; });
	if (ridx >= 0)
	{
		int idx = (m_modifications.size()-1) - ridx;
		return m_parent->getTimestamp() + idx;
	}
	else
	{
		return m_parent->getLastModificationTimestamp(variable);
	}
}

void CommittableVariableDatabase::markConstraintFullySatisfied(IConstraint* constraint)
{
	m_outerSink->committableDatabaseConstraintSatisfied(*this, constraint);
}

const ValueSet& CommittableVariableDatabase::getValueBefore(VarID variable, SolverTimestamp timestamp, SolverTimestamp* outTimestamp) const
{
	if (timestamp > m_parent->getTimestamp())
	{
		int curTime = getTimestamp();
		for (int i = m_modifications.size()-1; i >= 0; --i, --curTime)
		{
			auto& mod = m_modifications[i];
			if (curTime < timestamp && mod.variable == variable)
			{
				if (outTimestamp)
				{
					*outTimestamp = curTime;
				}
				return mod.value;
			}
		}
	}
	return m_parent->getValueBefore(variable, timestamp, outTimestamp);
}

const ValueSet& CommittableVariableDatabase::getValueAfter(VarID variable, SolverTimestamp timestamp) const
{
	for (int i = timestamp-m_parent->getTimestamp(); i < m_modifications.size(); ++i)
	{
		auto& mod = m_modifications[i];
		if (mod.variable == variable)
		{
			return mod.value;
		}
	}

	return m_parent->getValueAfter(variable, timestamp);
}

SolverTimestamp CommittableVariableDatabase::getModificationTimePriorTo(VarID variable, SolverTimestamp timestamp) const
{
	SolverTimestamp out;
	getValueBefore(variable, timestamp, &out);
	return out;
}

SolverDecisionLevel CommittableVariableDatabase::getDecisionLevelForTimestamp(SolverTimestamp timestamp) const
{
	return m_parent->getDecisionLevelForTimestamp(timestamp);
}

WatcherHandle CommittableVariableDatabase::addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink)
{
	return m_outerSink->committableDatabaseAddWatchRequest(*this, varID, watchType, sink);
}

WatcherHandle CommittableVariableDatabase::addVariableValueWatch(VarID varID, const ValueSet& values, IVariableWatchSink* sink)
{
	return m_outerSink->committableDatabaseAddValueWatchRequest(*this, varID, values, sink);
}

void CommittableVariableDatabase::disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink)
{
	return m_outerSink->committableDatabaseDisableWatchRequest(*this, handle, variable, sink);
}

void CommittableVariableDatabase::removeVariableWatch(VarID varID, WatcherHandle handle, IVariableWatchSink* sink)
{
	return m_outerSink->committableDatabaseRemoveWatchRequest(*this, varID, handle, sink);
}