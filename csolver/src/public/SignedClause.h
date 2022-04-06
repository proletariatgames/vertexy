// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "variable/SolverVariableDomain.h"

namespace csolver
{

class ConstraintFactoryParams;

enum class EClauseSign
{
	Inside,
	Outside
};

template <typename VariableType>
class TSignedClause
{
public:
	TSignedClause()
		: variable()
		, sign(EClauseSign::Inside)
	{
	}

	TSignedClause(const VariableType& inVariable, EClauseSign sign, const vector<int>& values)
		: variable(inVariable)
		, sign(sign)
		, values(values)
	{
	}

	TSignedClause(const VariableType& inVariable, const vector<int>& values)
		: variable(inVariable)
		, sign(EClauseSign::Inside)
		, values(values)
	{
	}

	static TSignedClause<VariableType> createFromRange(const VariableType& variable, int domainSize, int min, int max, EClauseSign sign = EClauseSign::Inside)
	{
		ValueSet values;
		values.pad(domainSize, false);
		for (int i = max(0, min); i <= min(domainSize - 1, max); ++i)
		{
			values[i] = true;
		}
		return SignedClause(variable, sign, values);
	}

	ValueSet translateToDomain(const SolverVariableDomain& destDomain, bool allowOutsideDomainValues = true) const
	{
		// Create a new translated clause with a domain the same as the variable's.
		ValueSet translated;
		translated.pad(destDomain.getDomainSize(), false);

		for (int value : values)
		{
			int destIndex;
			if (destDomain.getIndexForValue(value, destIndex))
			{
				translated[destIndex] = true;
			}
			else if (!allowOutsideDomainValues)
			{
				cs_assert_msg(false, "Value %d does not fit in domain", value);
			}
		}

		if (sign == EClauseSign::Outside)
		{
			translated.invert();
		}

		return translated;
	}

	inline ValueSet translateToInternal(const ConstraintFactoryParams& params, bool allowOutsideDomainValues = true) const
	{
		return translateToDomain(params.getDomain(variable), allowOutsideDomainValues);
	}

	inline Literal translateToLiteral(const ConstraintFactoryParams& params, bool allowOutsideDomainValues = true) const
	{
		return Literal(variable, translateToInternal(params, allowOutsideDomainValues));
	}

	bool operator==(const TSignedClause& rhs) const
	{
		return variable == rhs.variable && sign == rhs.sign &&
			values.size() == rhs.values.size() &&
			!containsPredicate(values.begin(), values.end(), [&](auto&& val)
			{
				return !contains(rhs.values.begin(), rhs.values.end(), val);
			});
	}

	bool operator!=(const TSignedClause& rhs) const
	{
		return !(*this == rhs);
	}

	VariableType variable;
	EClauseSign sign;
	vector<int> values;

	TSignedClause<VariableType> invert() const
	{
		return TSignedClause(variable, sign == EClauseSign::Inside ? EClauseSign::Outside : EClauseSign::Inside, values);
	}
};

using SignedClause = TSignedClause<VarID>;

template <typename T>
class IGraphRelation;
using IGraphVariableRelation = IGraphRelation<VarID>;
using IGraphClauseRelation = IGraphRelation<SignedClause>;
using IGraphLiteralRelation = IGraphRelation<Literal>;

using GraphVariableRelationPtr = shared_ptr<const IGraphVariableRelation>;
using GraphClauseRelationPtr = shared_ptr<const IGraphClauseRelation>;
using GraphLiteralRelationPtr = shared_ptr<const IGraphLiteralRelation>;

using GraphRelationClause = TSignedClause<GraphVariableRelationPtr>;

} // namespace csolver