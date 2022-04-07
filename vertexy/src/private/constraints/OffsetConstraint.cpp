// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/OffsetConstraint.h"

#include "variable/IVariableDatabase.h"
#include "ConstraintSolver.h"

using namespace Vertexy;

OffsetConstraint* OffsetConstraint::OffsetConstraintFactory::construct(const ConstraintFactoryParams& params, VarID sum, VarID term, int delta, bool bPreUnified)
{
	if (!bPreUnified)
	{
		vector<VarID> unified = params.unifyVariableDomains({sum, term});

		// (Sum - Domain(Sum).Min) = (Term - Domain(Term).Min) + Delta
		// == Sum = Term + Delta + Domain(Sum).Min - Domain(Term).Min
		return new OffsetConstraint(params, unified[0], unified[1], delta);
	}
	else
	{
		return new OffsetConstraint(params, sum, term, delta);
	}
}

OffsetConstraint::OffsetConstraint(const ConstraintFactoryParams& params, VarID sum, VarID term, int delta)
	: ISolverConstraint(params)
	, m_sum(sum)
	, m_term(term)
	, m_delta(delta)
{
}

bool OffsetConstraint::initialize(IVariableDatabase* db)
{
	m_handleSum = db->addVariableWatch(m_sum, EVariableWatchType::WatchModification, this);
	m_handleTerm = db->addVariableWatch(m_term, EVariableWatchType::WatchModification, this);

	bool temp = false;
	if (!onVariableNarrowed(db, m_sum, {}, temp))
	{
		return false;
	}

	if (!onVariableNarrowed(db, m_term, {}, temp))
	{
		return false;
	}

	return true;
}

void OffsetConstraint::reset(IVariableDatabase* db)
{
	db->removeVariableWatch(m_sum, m_handleSum, this);
	db->removeVariableWatch(m_term, m_handleTerm, this);

	m_handleSum = INVALID_WATCHER_HANDLE;
	m_handleTerm = INVALID_WATCHER_HANDLE;
}

/*static*/
ValueSet OffsetConstraint::shiftBits(const ValueSet& bits, int amount, int destSize)
{
	ValueSet output;
	if (amount > 0)
	{
		output.pad(amount, false);
		output.append(bits, min(bits.size(), destSize - amount), 0);
		output.pad(destSize, false);
	}
	else
	{
		amount *= -1;
		output.append(bits, min(bits.size() - amount, destSize), amount);
		output.pad(destSize, false);
	}
	vxy_sanity(output.size() == destSize);
	return output;
}


bool OffsetConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet&, bool&)
{
	if (variable == m_sum)
	{
		if (!db->constrainToValues(m_term, shiftBits(db->getPotentialValues(m_sum), -m_delta, db->getDomainSize(m_term)), this))
		{
			return false;
		}
	}
	else
	{
		vxy_assert(variable == m_term);
		if (!db->constrainToValues(m_sum, shiftBits(db->getPotentialValues(m_term), m_delta, db->getDomainSize(m_sum)), this))
		{
			return false;
		}
	}

	return true;
}

bool OffsetConstraint::checkConflicting(IVariableDatabase* db) const
{
	ValueSet potentialSum = shiftBits(db->getPotentialValues(m_term), m_delta, db->getDomainSize(m_sum));
	if (!db->getPotentialValues(m_sum).anyPossible(potentialSum))
	{
		return true;
	}

	ValueSet potentialTerm = shiftBits(db->getPotentialValues(m_sum), -m_delta, db->getDomainSize(m_term));
	if (!db->getPotentialValues(m_term).anyPossible(potentialTerm))
	{
		return true;
	}

	return false;
}
