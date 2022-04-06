// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/IffConstraint.h"
#include "variable/IVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"
#include <EASTL/algorithm.h>

using namespace csolver;

IffConstraint* IffConstraint::IffConstraintFactory::construct(const ConstraintFactoryParams& params, const SignedClause& head, const vector<SignedClause>& body)
{
	vector<Literal> transformedBody;
	for_each(body.begin(), body.end(), [&](auto& clause)
	{
		transformedBody.push_back(clause.translateToLiteral(params));
	});

	return new IffConstraint(params, head.variable, head.translateToInternal(params), transformedBody);
}

IffConstraint::IffConstraint(const ConstraintFactoryParams& params, VarID inHead, const ValueSet& inHeadValue, vector<Literal>& InBody)
	: ISolverConstraint(params)
	, m_head(inHead)
	, m_headValue(inHeadValue)
	, m_body(InBody)
{
}

bool IffConstraint::initialize(IVariableDatabase* db)
{
	if (m_body.empty())
	{
		// No body clauses, so head can never be true.
		return db->excludeValues(m_head, m_headValue, this);
	}

	bool fullySatisfied = false;
	if (!db->getPotentialValues(m_head).anyPossible(m_headValue))
	{
		// Head definitely false, body must be false
		if (!propagateBodyFalse(db, fullySatisfied))
		{
			return false;
		}
	}
	else if (db->getPotentialValues(m_head).isSubsetOf(m_headValue))
	{
		// Head definitely true, body must be true
		if (!propagateBodyTrue(db, fullySatisfied))
		{
			return false;
		}
	}

	if (fullySatisfied)
	{
		db->markConstraintFullySatisfied(this);
	}
	else
	{
		// Only need to watch if we're not already fully satisfied.
		cs_assert(m_headWatch == INVALID_WATCHER_HANDLE);
		m_headWatch = db->addVariableWatch(m_head, EVariableWatchType::WatchModification, this);

		cs_assert(m_bodyWatches.empty());
		for (const Literal& lit : m_body)
		{
			m_bodyWatches.push_back(db->addVariableWatch(lit.variable, EVariableWatchType::WatchModification, this));
		}
	}

	int lastSupport;
	EBodySatisfaction bodySatisfaction = getBodySatisfaction(db, lastSupport);
	if (bodySatisfaction == EBodySatisfaction::SAT)
	{
		cs_sanity(lastSupport < 0);
		// Body is definitely true, head must be true
		if (!db->constrainToValues(m_head, m_headValue, this))
		{
			return false;
		}
	}
	else if (bodySatisfaction == EBodySatisfaction::UNSAT)
	{
		cs_sanity(lastSupport < 0);
		// Body is definitely false, head must be false
		if (!db->excludeValues(m_head, m_headValue, this))
		{
			return false;
		}
	}
	else if (lastSupport >= 0 && db->getPotentialValues(m_head).isSubsetOf(m_headValue))
	{
		if (!db->constrainToValues(m_body[lastSupport].variable, m_body[lastSupport].values, this))
		{
			return false;
		}
	}

	return true;
}

void IffConstraint::reset(IVariableDatabase* db)
{
	if (m_headWatch != INVALID_WATCHER_HANDLE)
	{
		db->removeVariableWatch(m_head, m_headWatch, this);
		m_headWatch = INVALID_WATCHER_HANDLE;
	}
	for (int i = 0; i < m_bodyWatches.size(); ++i)
	{
		db->removeVariableWatch(m_body[i].variable, m_bodyWatches[i], this);
	}
	m_bodyWatches.clear();
}

bool IffConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool&)
{
	if (variable == m_head)
	{
		const ValueSet& curValue = db->getPotentialValues(m_head);
		if (curValue.anyPossible(m_headValue))
		{
			if (curValue.isSubsetOf(m_headValue))
			{
				// head is definitely true, body must be true.
				// only propagate if we became definitely true just now
				if (!prevValue.isSubsetOf(m_headValue))
				{
					bool fullySatisfied;
					if (!propagateBodyTrue(db, fullySatisfied))
					{
						return false;
					}
					if (fullySatisfied)
					{
						db->markConstraintFullySatisfied(this);
					}
				}
			}
		}
		else
		{
			// head is definitely false, body must be false.
			// only propagate if we became definitely false just now
			if (prevValue.anyPossible(m_headValue))
			{
				bool fullySatisfied;
				if (!propagateBodyFalse(db, fullySatisfied))
				{
					return false;
				}
				if (fullySatisfied)
				{
					db->markConstraintFullySatisfied(this);
				}
			}
		}
	}
	else
	{
		db->queueConstraintPropagation(this);
	}
	return true;
}


bool IffConstraint::propagate(IVariableDatabase* db)
{
	const ValueSet& curHeadValue = db->getPotentialValues(m_head);
	if (curHeadValue.anyPossible(m_headValue))
	{
		int lastSupport;
		EBodySatisfaction sat = getBodySatisfaction(db, lastSupport);
		if (sat == EBodySatisfaction::UNSAT)
		{
			// Body is false, so Head can't be HeadValue
			cs_sanity(lastSupport < 0);
			if (!db->getPotentialValues(m_head).anyPossible(m_headValue))
			{
				db->markConstraintFullySatisfied(this);
			}
			else if (!db->excludeValues(m_head, m_headValue, this, [&](auto& params) { return explainVariable(params); }))
			{
				return false;
			}
		}
		else if (sat == EBodySatisfaction::SAT)
		{
			cs_sanity(lastSupport < 0);
			if (db->getPotentialValues(m_head).isSubsetOf(m_headValue))
			{
				db->markConstraintFullySatisfied(this);
			}
			else if (!db->constrainToValues(m_head, m_headValue, this, [&](auto& params) { return explainVariable(params); }))
			{
				return false;
			}
		}
		else if (lastSupport >= 0 && curHeadValue.isSubsetOf(m_headValue))
		{
			if (!db->constrainToValues(m_body[lastSupport].variable, m_body[lastSupport].values, this, [&](auto& params) { return explainVariable(params); }))
			{
				return false;
			}
		}
	}
	else
	{
		// No need to do anything. If the head isn't possible, then we propagated false to the body already.
	}

	return true;
}

bool IffConstraint::propagateBodyTrue(IVariableDatabase* db, bool& outFullySatisfied)
{
	outFullySatisfied = false;

	int numSupports = 0;
	int lastSupportIndex = -1;
	for (int i = 0; i < m_body.size(); ++i)
	{
		auto& vals = db->getPotentialValues(m_body[i].variable);
		if (vals.anyPossible(m_body[i].values))
		{
			if (!outFullySatisfied && vals.isSubsetOf(m_body[i].values))
			{
				outFullySatisfied = true;
			}
			numSupports++;
			lastSupportIndex = i;
			if (numSupports >= 2)
			{
				break;
			}
		}
	}

	if (numSupports == 0)
	{
		// Body can't be true.
		// Just find the most recently modified body variable and constraint it (which should definitely fail)
		int mostRecentIndex = 0;
		for (int i = 1; i < m_body.size(); ++i)
		{
			if (db->getLastModificationTimestamp(m_body[i].variable) > db->getLastModificationTimestamp(m_body[mostRecentIndex].variable))
			{
				mostRecentIndex = i;
			}
		}

		bool success = db->constrainToValues(m_body[mostRecentIndex].variable, m_body[mostRecentIndex].values, this, [&](auto& params) { return explainVariable(params); });
		cs_assert(!success);

		return false;
	}
	else if (numSupports == 1)
	{
		// Last support MUST be true.
		if (!db->constrainToValues(m_body[lastSupportIndex].variable, m_body[lastSupportIndex].values, this, [&](auto& params) { return explainVariable(params); }))
		{
			return false;
		}
	}

	return true;
}

bool IffConstraint::propagateBodyFalse(IVariableDatabase* db, bool& outFullySatisfied)
{
	outFullySatisfied = true;
	for (int i = 0; i < m_body.size(); ++i)
	{
		auto& vals = db->getPotentialValues(m_body[i].variable);
		outFullySatisfied = outFullySatisfied && !vals.anyPossible(m_body[i].values);
		if (!db->excludeValues(m_body[i].variable, m_body[i].values, this, [&](auto& params) { return explainVariable(params); }))
		{
			return false;
		}
	}

	return true;
}

vector<VarID> IffConstraint::getConstrainingVariables() const
{
	vector<VarID> out;
	out.push_back(m_head);
	for (auto& lit : m_body)
	{
		out.push_back(lit.variable);
	}
	return out;
}

bool IffConstraint::checkConflicting(IVariableDatabase* db) const
{
	if (db->getPotentialValues(m_head).anyPossible(m_headValue))
	{
		if (int lastSupport; getBodySatisfaction(db, lastSupport) == EBodySatisfaction::UNSAT)
		{
			return true;
		}
	}
	else
	{
		if (int lastSupport; getBodySatisfaction(db, lastSupport) == EBodySatisfaction::SAT)
		{
			return true;
		}
	}

	return false;
}

IffConstraint::EBodySatisfaction IffConstraint::getBodySatisfaction(IVariableDatabase* db, int& lastSupport) const
{
	int numSupports = 0;
	bool foundDefinite = false;
	for (int i = 0; i < m_body.size(); ++i)
	{
		const ValueSet& val = db->getPotentialValues(m_body[i].variable);
		if (val.anyPossible(m_body[i].values))
		{
			++numSupports;
			lastSupport = i;
			if (val.isSubsetOf(m_body[i].values))
			{
				foundDefinite = true;
				break;
			}
		}
	}

	if (numSupports == 0)
	{
		return EBodySatisfaction::UNSAT;
	}
	else if (foundDefinite)
	{
		lastSupport = -1;
		return EBodySatisfaction::SAT;
	}
	else
	{
		if (numSupports > 1)
		{
			lastSupport = -1;
		}
		return EBodySatisfaction::Unknown;
	}
}

vector<Literal> IffConstraint::explainVariable(const NarrowingExplanationParams& params) const
{
	vector<Literal> output;

	auto db = params.database;
	if (params.propagatedVariable == m_head)
	{
		auto& prevVals = db->getPotentialValues(m_head);
		if (prevVals.anyPossible(m_headValue) && !params.propagatedValues.anyPossible(m_headValue))
		{
			// Head became false because all body literals were false.
			output.push_back(Literal(m_head, m_headValue.inverted()));
			output.insert(output.end(), m_body.begin(), m_body.end());
		}
		else
		{
			// Head became true because at least one body literal was true.
			output.push_back(Literal(m_head, m_headValue));
			for (auto& bodyLit : m_body)
			{
				if (db->getPotentialValues(bodyLit.variable).isSubsetOf(bodyLit.values))
				{
					output.push_back(Literal(bodyLit.variable, bodyLit.values.inverted()));
				}
			}
			cs_assert(output.size() > 1);
		}
	}
	else
	{
		auto it = find_if(m_body.begin(), m_body.end(), [&](auto& bodyLit) { return bodyLit.variable == params.propagatedVariable; });
		cs_assert(it != m_body.end());

		auto& prevVals = db->getPotentialValues(it->variable);
		if (prevVals.anyPossible(it->values) && !params.propagatedValues.anyPossible(it->values))
		{
			// Body became false because head was false.
			output.push_back(Literal(m_head, m_headValue));
			output.push_back(Literal(it->variable, it->values.inverted()));
		}
		else
		{
			// Body became true because head was true, and all other bodies were false.
			output.push_back(Literal(m_head, m_headValue.inverted()));
			output.insert(output.end(), m_body.begin(), m_body.end());
		}
	}
	return output;
}
