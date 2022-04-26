// Copyright Proletariat, Inc. All Rights Reserved.

#include "constraints/SumConstraint.h"

#include "variable/IVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"

using namespace Vertexy;

SumConstraint* SumConstraint::SumConstraintFactory::construct(const ConstraintFactoryParams& params, VarID inSum, VarID inTerm1, VarID inTerm2)
{
	vector<VarID> unified = params.unifyVariableDomains({ inSum, inTerm1, inTerm2 });
	return new SumConstraint(params, unified[0], unified[1], unified[2]);
}

SumConstraint::SumConstraint(const ConstraintFactoryParams& params, VarID inSum, VarID inTerm1, VarID inTerm2)
	: IConstraint(params)
	, m_sum(inSum)
	, m_term1(inTerm1)
	, m_term2(inTerm2)
	, m_minVal(params.getDomain(inSum).getMin())
{
}

vector<VarID> SumConstraint::getConstrainingVariables() const
{
	return { m_sum, m_term1, m_term2 };
}

bool SumConstraint::initialize(IVariableDatabase* db)
{
	// Set variable watches
	m_sumWatch = db->addVariableWatch(m_sum, EVariableWatchType::WatchModification, this);
	m_term1Watch = db->addVariableWatch(m_term1, EVariableWatchType::WatchModification, this);
	m_term2Watch = db->addVariableWatch(m_term2, EVariableWatchType::WatchModification, this);

	return true;
}

void SumConstraint::reset(IVariableDatabase* db)
{
	db->removeVariableWatch(m_sum, m_sumWatch, this);
	db->removeVariableWatch(m_term1, m_term1Watch, this);
	db->removeVariableWatch(m_term2, m_term2Watch, this);
}

bool SumConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& bRemoveWatch)
{
	if (variable == m_sum)
	{
		ValueSet potentialTerm1 = combineValueSets(db, db->getDomainSize(m_term1), m_sum, m_term2, false);
		if (!db->constrainToValues(m_term1, potentialTerm1, this))
		{
			return false;
		}

		ValueSet potentialTerm2 = combineValueSets(db, db->getDomainSize(m_term2), m_sum, m_term1, false);
		if (!db->constrainToValues(m_term2, potentialTerm2, this))
		{
			return false;
		}
	}
	else if (variable == m_term1)
	{
		ValueSet potentialSum = combineValueSets(db, db->getDomainSize(m_sum), m_term1, m_term2, true);
		if (!db->constrainToValues(m_sum, potentialSum, this))
		{
			return false;
		}

		ValueSet potentialTerm2 = combineValueSets(db, db->getDomainSize(m_term2), m_sum, m_term1, false);
		if (!db->constrainToValues(m_term2, potentialTerm2, this))
		{
			return false;
		}
	}
	else if (variable == m_term2)
	{
		ValueSet potentialSum = combineValueSets(db, db->getDomainSize(m_sum), m_term1, m_term2, true);
		if (!db->constrainToValues(m_sum, potentialSum, this))
		{
			return false;
		}

		ValueSet potentialTerm1 = combineValueSets(db, db->getDomainSize(m_term1), m_sum, m_term2, false);
		if (!db->constrainToValues(m_term1, potentialTerm1, this))
		{
			return false;
		}
	}
	else
	{
		vxy_assert(false);
	}

	return true;
}

bool SumConstraint::checkConflicting(IVariableDatabase* db) const
{
	ValueSet potentialSum = combineValueSets(db, db->getDomainSize(m_sum), m_term1, m_term2, true);
	if (!db->getPotentialValues(m_sum).anyPossible(potentialSum))
	{
		return true;
	}

	ValueSet potentialTerm1 = combineValueSets(db, db->getDomainSize(m_term1), m_sum, m_term2, false);
	if (!db->getPotentialValues(m_term1).anyPossible(potentialTerm1))
	{
		return true;
	}

	ValueSet potentialTerm2 = combineValueSets(db, db->getDomainSize(m_term2), m_sum, m_term1, false);
	if (!db->getPotentialValues(m_term2).anyPossible(potentialTerm2))
	{
		return true;
	}

	return false;
}

ValueSet SumConstraint::combineValueSets(IVariableDatabase* db, int destSize, VarID one, VarID two, bool addSets) const
{
	ValueSet outSet(destSize);
	ValueSet potentialOne = db->getPotentialValues(one);
	ValueSet potentialTwo = db->getPotentialValues(two);

	for (auto itOne = potentialOne.beginSetBits(), itOneEnd = potentialOne.endSetBits(); itOne != itOneEnd; ++itOne)
	{
		for (auto itTwo = potentialTwo.beginSetBits(), itTwoEnd = potentialTwo.endSetBits(); itTwo != itTwoEnd; ++itTwo)
		{
			// For each pair of set bits, add or subtract their values, using minVal as the offset and enable that bit in the result. Consider only values in our result's domain.
			int val = *itOne + (*itTwo + m_minVal) * (addSets ? 1 : -1);
			if (val >= 0 && val < outSet.size())
			{
				outSet[val] = 1;
			}
		}
	}

	return outSet;
}