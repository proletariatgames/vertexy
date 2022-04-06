// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/DisjunctionConstraint.h"

#include "constraints/ClauseConstraint.h"
#include "variable/CommittableVariableDatabase.h"
#include "variable/HistoricalVariableDatabase.h"

using namespace csolver;

DisjunctionConstraint* DisjunctionConstraint::Factory::construct(const ConstraintFactoryParams& params, ISolverConstraint* innerConsA, ISolverConstraint* innerConsB)
{
	params.markChildConstraint(innerConsA);
	params.markChildConstraint(innerConsB);
	return new DisjunctionConstraint(params, innerConsA, innerConsB);
}

DisjunctionConstraint::DisjunctionConstraint(const ConstraintFactoryParams& params, ISolverConstraint* innerConsA, ISolverConstraint* innerConsB)
	: IBacktrackingSolverConstraint(params)
	, m_innerCons{innerConsA, innerConsB}
	, m_fullySatLevel{-1,-1}
	, m_constraintQueued{false,false}
	, m_lastPropagation{-1,-1}
{
}

vector<VarID> DisjunctionConstraint::getConstrainingVariables() const
{
	auto vars = m_innerCons[0]->getConstrainingVariables();
	auto otherVars = m_innerCons[1]->getConstrainingVariables();
	for (VarID var : otherVars)
	{
		if (!contains(vars.begin(), vars.end(), var))
		{
			vars.push_back(var);
		}
	}
	return vars;
}

bool DisjunctionConstraint::initialize(IVariableDatabase* db)
{
	for (int i = 0; i < 2; ++i)
	{
		auto cdb = createCommittableDB(db, i);
		if (!m_innerCons[i]->initialize(&cdb, this))
		{
			if (!markUnsat(cdb, i))
			{
				return false;
			}
		}
	}
	return true;
}

void DisjunctionConstraint::reset(IVariableDatabase* db)
{
	for (int i = 0; i < 2; ++i)
	{
		m_sinkWrappers[i].clear();
		m_unsatInfo[i].reset();
		m_constraintQueued[i] = false;
		m_lastPropagation[i] = -1;
		m_fullySatLevel[i] = -1;
	}
}

bool DisjunctionConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch)
{
	cs_fail_msg("Should never be called!");
	return true;
}

bool DisjunctionConstraint::forwardVariableNarrowed(IVariableDatabase* db, IVariableWatchSink* innerSink, int innerConsIndex, VarID var, const ValueSet& previousValue, bool& removeHandle)
{
	cs_sanity(innerConsIndex == 0 || innerConsIndex == 1);
	int otherConsIndex = innerConsIndex == 0 ? 1 : 0;
	if (!m_unsatInfo[innerConsIndex].isUnsat() && m_fullySatLevel[otherConsIndex] < 0)
	{
		m_lastPropagation[innerConsIndex] = db->getTimestamp();

		auto cdb = createCommittableDB(db, innerConsIndex);
		if (!innerSink->onVariableNarrowed(&cdb, var, previousValue, removeHandle))
		{
			return markUnsat(cdb, innerConsIndex);
		}
	}
	else
	{
		cs_sanity(m_fullySatLevel[otherConsIndex] < 0 || !m_innerCons[otherConsIndex]->checkConflicting(db));
	}

	cs_sanity(!m_unsatInfo[0].isUnsat() || !m_unsatInfo[1].isUnsat());
	return true;
}

bool DisjunctionConstraint::propagate(IVariableDatabase* db)
{
	for (int i = 0; i < 2; ++i)
	{
		int oi = i == 0 ? 1 : 0;
		if (m_constraintQueued[i] && !m_unsatInfo[i].isUnsat() && m_fullySatLevel[oi] < 0)
		{
			auto cdb = createCommittableDB(db, 0);
			if (!m_innerCons[i]->propagate(&cdb))
			{
				if (!markUnsat(cdb, i))
				{
					return false;
				}
			}
		}
		else
		{
			cs_sanity(m_fullySatLevel[oi] < 0 || !m_innerCons[oi]->checkConflicting(db));
		}
		m_constraintQueued[i] = false;
	}

	return true;
}

void DisjunctionConstraint::backtrack(const IVariableDatabase* db, SolverDecisionLevel level)
{
	for (int i = 0; i < 2; ++i)
	{
		m_constraintQueued[i] = false;
		if (m_unsatInfo[i].level > level)
		{
			m_unsatInfo[i].reset();
		}
		if (m_fullySatLevel[i] > level)
		{
			m_fullySatLevel[i] = -1;
		}
	}
}

bool DisjunctionConstraint::explainConflict(const IVariableDatabase* db, vector<Literal>& outClauses) const
{
	cs_assert(m_unsatInfo[0].isUnsat() && m_unsatInfo[1].isUnsat());

	outClauses.clear();
	outClauses.reserve(m_unsatInfo[0].explanation.size() + m_unsatInfo[1].explanation.size());
	outClauses.insert(outClauses.end(), m_unsatInfo[0].explanation.begin(), m_unsatInfo[0].explanation.end());
	outClauses.insert(outClauses.end(), m_unsatInfo[1].explanation.begin(), m_unsatInfo[1].explanation.end());
	return true;
}

bool DisjunctionConstraint::checkConflicting(IVariableDatabase* db) const
{
	if (m_innerCons[0]->checkConflicting(db) && m_innerCons[1]->checkConflicting(db))
	{
		return true;
	}
	return false;
}

bool DisjunctionConstraint::markUnsat(const CommittableVariableDatabase& cdb, int innerConsIndex, VarID contradictingVar, const ExplainerFunction& explainer)
{
	cs_sanity(innerConsIndex == 0 || innerConsIndex == 1);
	cs_assert(m_fullySatLevel[innerConsIndex] < 0);
	if (!m_unsatInfo[innerConsIndex].isUnsat())
	{
		vector<Literal> literals;
		if (contradictingVar != VarID::INVALID)
		{
			cs_assert(cdb.getPotentialValues(contradictingVar).isZero());

			if (auto clauseCons = m_innerCons[innerConsIndex]->asClauseConstraint())
			{
				literals = clauseCons->getLiteralsCopy();
			}
			else
			{
				cs_assert(m_lastPropagation[innerConsIndex] >= 0);
				HistoricalVariableDatabase hdb(&cdb, m_lastPropagation[innerConsIndex]);
				NarrowingExplanationParams explParams(cdb.getSolver(), &hdb, m_innerCons[innerConsIndex], contradictingVar, cdb.getPotentialValues(contradictingVar).isZero(), m_lastPropagation[innerConsIndex]);
				literals = explainer(explParams);
			}
		}
		else
		{
			m_innerCons[innerConsIndex]->explainConflict(&cdb, literals);
		}

		m_unsatInfo[innerConsIndex].markUnsat(cdb.getDecisionLevel(), literals);
	}
	bool result = !m_unsatInfo[0].isUnsat() || !m_unsatInfo[1].isUnsat();
	return result;
}

CommittableVariableDatabase DisjunctionConstraint::createCommittableDB(IVariableDatabase* db, int innerConsIndex)
{
	cs_sanity(innerConsIndex == 0 || innerConsIndex == 1);
	CommittableVariableDatabase cdb(db, this, this, innerConsIndex);
	int otherConsIndex = (innerConsIndex == 0) ? 1 : 0;
	if (m_unsatInfo[otherConsIndex].isUnsat())
	{
		cdb.commitPastAndFutureChanges();
	}
	return cdb;
}

void DisjunctionConstraint::committableDatabaseContradictionFound(const CommittableVariableDatabase& db, VarID varID, ISolverConstraint* source, const ExplainerFunction& explainer)
{
	const int innerConsIndex = db.getOuterSinkID();
	cs_sanity(innerConsIndex == 0 || innerConsIndex == 1);
	cs_sanity(source == m_innerCons[innerConsIndex]);

	markUnsat(db, innerConsIndex, varID, explainer);
}

WatcherHandle DisjunctionConstraint::committableDatabaseAddWatchRequest(const CommittableVariableDatabase& db, VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink)
{
	int innerConsIdx = db.getOuterSinkID();
	cs_sanity(innerConsIdx == 0 || innerConsIdx == 1);
	auto it = m_sinkWrappers[innerConsIdx].find(sink);
	if (it == m_sinkWrappers[innerConsIdx].end())
	{
		it = m_sinkWrappers[innerConsIdx].insert(make_pair(sink, make_unique<SinkWrapper>(this, sink, innerConsIdx))).first;
	}
	WatcherHandle handle = db.getParent()->addVariableWatch(varID, watchType, it->second.get());
	it->second->handles.push_back(make_tuple(handle, varID));
	return handle;
}

WatcherHandle DisjunctionConstraint::committableDatabaseAddValueWatchRequest(const CommittableVariableDatabase& db, VarID varID, const ValueSet& values, IVariableWatchSink* sink)
{
	int innerConsIdx = db.getOuterSinkID();
	cs_sanity(innerConsIdx == 0 || innerConsIdx == 1);
	auto it = m_sinkWrappers[innerConsIdx].find(sink);
	if (it == m_sinkWrappers[innerConsIdx].end())
	{
		it = m_sinkWrappers[innerConsIdx].insert(make_pair(sink, make_unique<SinkWrapper>(this, sink, innerConsIdx))).first;
	}
	WatcherHandle handle = db.getParent()->addVariableValueWatch(varID, values, it->second.get());
	it->second->handles.push_back(make_tuple(handle, varID));
	return handle;
}

void DisjunctionConstraint::committableDatabaseDisableWatchRequest(const CommittableVariableDatabase& db, WatcherHandle handle, VarID variable, IVariableWatchSink* sink)
{
	int innerConsIdx = db.getOuterSinkID();
	cs_sanity(innerConsIdx == 0 || innerConsIdx == 1);
	auto it = m_sinkWrappers[innerConsIdx].find(sink);
	if (it != m_sinkWrappers[innerConsIdx].end())
	{
		return db.getParent()->disableWatcherUntilBacktrack(handle, variable, it->second.get());
	}
}

void DisjunctionConstraint::committableDatabaseRemoveWatchRequest(const CommittableVariableDatabase& db, VarID varID, WatcherHandle handle, IVariableWatchSink* sink)
{
	int innerConsIdx = db.getOuterSinkID();
	cs_sanity(innerConsIdx == 0 || innerConsIdx == 1);
	auto it = m_sinkWrappers[innerConsIdx].find(sink);
	if (it != m_sinkWrappers[innerConsIdx].end())
	{
		db.getParent()->removeVariableWatch(varID, handle, it->second.get());
		it->second->handles.erase_first_unsorted(make_tuple(handle, varID));
	}
}

ExplainerFunction DisjunctionConstraint::committableDatabaseWrapExplanation(const CommittableVariableDatabase& db, const ExplainerFunction& innerExpl)
{
	int innerConsIndex = db.getOuterSinkID();
	return [this, innerConsIndex, innerExpl](const NarrowingExplanationParams& params)
	{
		return explainInner(params, innerConsIndex, innerExpl);
	};
}

void DisjunctionConstraint::committableDatabaseConstraintSatisfied(const CommittableVariableDatabase& db, ISolverConstraint* constraint)
{
	cs_sanity(constraint == m_innerCons[0] || constraint == m_innerCons[1]);
	int innerConsIndex = constraint == m_innerCons[0] ? 0 : 1;
	cs_assert(!m_unsatInfo[innerConsIndex].isUnsat());
	if (m_fullySatLevel[innerConsIndex] < 0)
	{
		m_fullySatLevel[innerConsIndex] = db.getDecisionLevel();
	}
}

void DisjunctionConstraint::committableDatabaseQueueRequest(const CommittableVariableDatabase& db, ISolverConstraint* cons)
{
	cs_sanity(cons == m_innerCons[0] || cons == m_innerCons[1]);
	int innerConsIndex = cons == m_innerCons[0] ? 0 : 1;
	m_constraintQueued[innerConsIndex] = true;
	db.getParent()->queueConstraintPropagation(this);
}

vector<Literal> DisjunctionConstraint::explainInner(const NarrowingExplanationParams& params, int innerConsIndex, const ExplainerFunction& innerExpl) const
{
	cs_sanity(innerConsIndex == 0 || innerConsIndex == 1);
	int otherConsIndex = (innerConsIndex == 0) ? 1 : 0;
	cs_assert(m_unsatInfo[otherConsIndex].isUnsat());

	vector<Literal> expl;
	if (auto clauseCons = m_innerCons[innerConsIndex]->asClauseConstraint())
	{
		expl = clauseCons->getLiteralsCopy();
	}
	else
	{
		NarrowingExplanationParams explParams(params.solver, params.database, m_innerCons[innerConsIndex], params.propagatedVariable, params.propagatedValues, params.timestamp);
		expl = innerExpl(explParams);
	}
	expl.reserve(expl.size() + m_unsatInfo[otherConsIndex].explanation.size());
	expl.insert(expl.end(), m_unsatInfo[otherConsIndex].explanation.begin(), m_unsatInfo[otherConsIndex].explanation.end());

	return expl;
}
