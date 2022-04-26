// Copyright Proletariat, Inc. All Rights Reserved.

#include "constraints/InequalityConstraint.h"
#include "variable/IVariableDatabase.h"
#include "variable/SolverVariableDatabase.h"

using namespace Vertexy;

InequalityConstraint* InequalityConstraint::InequalityConstraintFactory::construct(const ConstraintFactoryParams& params, VarID a, EConstraintOperator op, VarID b)
{
	vector<VarID> unified = params.unifyVariableDomains({a, b});
	return new InequalityConstraint(params, unified[0], op, unified[1]);
}

InequalityConstraint::InequalityConstraint(const ConstraintFactoryParams& params, VarID leftVar, EConstraintOperator op, VarID rightVar)
	: IConstraint(params)
	, m_a(leftVar)
	, m_b(rightVar)
	, m_operator(op)
	, m_mirrorOperator(getMirrorOperator(m_operator))
{
}

bool InequalityConstraint::initialize(IVariableDatabase* db)
{
	switch (m_operator)
	{
	case EConstraintOperator::LessThan:
	case EConstraintOperator::LessThanEq:
		m_handleA = db->addVariableWatch(m_a, EVariableWatchType::WatchLowerBoundChange, this);
		m_handleB = db->addVariableWatch(m_b, EVariableWatchType::WatchUpperBoundChange, this);
		break;
	case EConstraintOperator::GreaterThan:
	case EConstraintOperator::GreaterThanEq:
		m_handleA = db->addVariableWatch(m_a, EVariableWatchType::WatchUpperBoundChange, this);
		m_handleB = db->addVariableWatch(m_b, EVariableWatchType::WatchLowerBoundChange, this);
		break;
	case EConstraintOperator::NotEqual:
		m_handleA = db->addVariableWatch(m_a, EVariableWatchType::WatchSolved, this);
		m_handleB = db->addVariableWatch(m_b, EVariableWatchType::WatchSolved, this);
		break;
	default:
		vxy_fail();
		break;
	}

	if (!applyOperator(db, m_mirrorOperator, m_a))
	{
		return false;
	}

	if (!applyOperator(db, m_operator, m_b))
	{
		return false;
	}

	return true;
}

void InequalityConstraint::reset(IVariableDatabase* db)
{
	db->removeVariableWatch(m_a, m_handleA, this);
	db->removeVariableWatch(m_b, m_handleB, this);

	m_handleA = INVALID_WATCHER_HANDLE;
	m_handleB = INVALID_WATCHER_HANDLE;
}

bool InequalityConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet&, bool&)
{
	if (variable == m_a)
	{
		return applyOperator(db, m_operator, m_b);
	}
	else
	{
		vxy_assert(variable == m_b);
		return applyOperator(db, m_mirrorOperator, m_a);
	}
}

bool InequalityConstraint::applyOperator(IVariableDatabase* db, EConstraintOperator op, VarID rhs)
{
	VarID lhs = rhs == m_a ? m_b : m_a;

	// constrain Rhs to be within the allowed inequality range.
	switch (op)
	{
	case EConstraintOperator::LessThan:
		{
			if (db->getMaximumPossibleValue(lhs) < db->getMinimumPossibleValue(rhs))
			{
				db->markConstraintFullySatisfied(this);
			}
			else
			{
				const int minPossible = db->getMinimumPossibleValue(lhs);
				if (!db->excludeValuesLessThan(rhs, minPossible + 1, this))
				{
					return false;
				}
			}
		}
		break;
	case EConstraintOperator::LessThanEq:
		{
			if (db->getMaximumPossibleValue(lhs) <= db->getMinimumPossibleValue(rhs))
			{
				db->markConstraintFullySatisfied(this);
			}
			else
			{
				const int minPossible = db->getMinimumPossibleValue(lhs);
				if (!db->excludeValuesLessThan(rhs, minPossible, this))
				{
					return false;
				}
			}
		}
		break;
	case EConstraintOperator::GreaterThan:
		{
			if (db->getMinimumPossibleValue(lhs) > db->getMaximumPossibleValue(rhs))
			{
				db->markConstraintFullySatisfied(this);
			}
			else
			{
				const int maxPossible = db->getMaximumPossibleValue(lhs);
				if (!db->excludeValuesGreaterThan(rhs, maxPossible - 1, this))
				{
					return false;
				}
			}
		}
		break;
	case EConstraintOperator::GreaterThanEq:
		{
			if (db->getMinimumPossibleValue(lhs) >= db->getMaximumPossibleValue(rhs))
			{
				db->markConstraintFullySatisfied(this);
			}
			else
			{
				const int maxPossible = db->getMaximumPossibleValue(lhs);
				if (!db->excludeValuesGreaterThan(rhs, maxPossible, this))
				{
					return false;
				}
			}
		}
		break;
	case EConstraintOperator::NotEqual:
		if (db->isSolved(lhs))
		{
			int solvedValue = db->getSolvedValue(lhs);
			if (!db->excludeValue(rhs, solvedValue, this))
			{
				return false;
			}
		}
	}
	return true;
}

vector<Literal> InequalityConstraint::explain(const NarrowingExplanationParams& params) const
{
	auto db = params.database;

	vxy_assert(params.propagatedVariable == m_a || params.propagatedVariable == m_b);
	VarID rhs = params.propagatedVariable;
	VarID lhs = rhs == m_a ? m_b : m_a;

	ValueSet lhsVals, rhsVals;
	lhsVals.init(db->getDomainSize(lhs), false);
	rhsVals.init(db->getDomainSize(rhs), false);

	EConstraintOperator op = lhs == m_a ? m_operator : m_mirrorOperator;
	switch (op)
	{
	case EConstraintOperator::LessThan:
		{
			const int minValue = db->getMinimumPossibleValue(lhs);

			// Lhs > M implies Rhs > M+1
			lhsVals.setRange(0, minValue, true);
			rhsVals.setRange(minValue+1, rhsVals.size(), true);
		}
		break;
	case EConstraintOperator::LessThanEq:
		{
			const int minValue = db->getMinimumPossibleValue(lhs);

			// Lhs > M implies Rhs >= M+1
			lhsVals.setRange(0, minValue, true);
			rhsVals.setRange(minValue, rhsVals.size(), true);
		}
		break;
	case EConstraintOperator::GreaterThan:
		{
			const int maxValue = db->getMaximumPossibleValue(lhs);

			// Lhs < M implies Rhs < M-1
			lhsVals.setRange(maxValue+1, lhsVals.size(), true);
			rhsVals.setRange(0, maxValue, true);
		}
		break;
	case EConstraintOperator::GreaterThanEq:
		{
			const int maxValue = db->getMaximumPossibleValue(lhs);

			// Lhs < M implies Rhs <= M-1
			lhsVals.setRange(maxValue+1, lhsVals.size(), true);
			rhsVals.setRange(0, maxValue+1, true);
		}
		break;
	case EConstraintOperator::NotEqual:
		vxy_fail(); // handled by default explainer
		break;
	}

	return {Literal(lhs, lhsVals), Literal(rhs, rhsVals)};
}

bool InequalityConstraint::checkConflicting(IVariableDatabase* db) const
{
	switch (m_operator)
	{
	case EConstraintOperator::GreaterThan:
		return db->getMaximumPossibleValue(m_a) <= db->getMinimumPossibleValue(m_b);
	case EConstraintOperator::GreaterThanEq:
		return db->getMaximumPossibleValue(m_a) < db->getMinimumPossibleValue(m_b);
	case EConstraintOperator::LessThan:
		return db->getMinimumPossibleValue(m_a) >= db->getMaximumPossibleValue(m_b);
	case EConstraintOperator::LessThanEq:
		return db->getMinimumPossibleValue(m_a) > db->getMinimumPossibleValue(m_b);
	case EConstraintOperator::NotEqual:
		return (db->isSolved(m_a) && db->isPossible(m_b, db->getSolvedValue(m_a))) ||
			(db->isSolved(m_b) && db->isPossible(m_a, db->getSolvedValue(m_b)));
	}

	vxy_fail();
	return true;
}

/*static*/
EConstraintOperator InequalityConstraint::getMirrorOperator(EConstraintOperator op)
{
	switch (op)
	{
	case EConstraintOperator::LessThan: return EConstraintOperator::GreaterThan;
	case EConstraintOperator::GreaterThan: return EConstraintOperator::LessThan;
	case EConstraintOperator::LessThanEq: return EConstraintOperator::GreaterThanEq;
	case EConstraintOperator::GreaterThanEq: return EConstraintOperator::LessThanEq;
	case EConstraintOperator::NotEqual: return EConstraintOperator::NotEqual;
	}
	vxy_fail();
	return EConstraintOperator::NotEqual;
}
