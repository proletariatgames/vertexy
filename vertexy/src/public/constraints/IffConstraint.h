// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include "IConstraint.h"

namespace Vertexy
{

/**
 * Given a variable h and a valueset H, and a set of body literals (variable+value tuples) L1...LN:
 *		V == H iff L1 or L2 or ...
 */
class IffConstraint : public IConstraint
{
public:
	IffConstraint(const ConstraintFactoryParams& params, VarID head, const ValueSet& headValue, vector<Literal>& body);

	struct IffConstraintFactory
	{
		static IffConstraint* construct(const ConstraintFactoryParams& params, const SignedClause& head, const vector<SignedClause>& body);
	};

	using Factory = IffConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Iff; }
	virtual vector<VarID> getConstrainingVariables() const override;
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual void explain(const NarrowingExplanationParams& params, vector<Literal>& outExplanation) const override;

protected:
	enum class EBodySatisfaction : uint8_t
	{
		Unknown,
		SAT,
		UNSAT,
	};

	bool propagateBodyFalse(IVariableDatabase* db, bool &outFullySatisfied);
	bool propagateBodyTrue(IVariableDatabase* db, bool &outFullySatisfied);
	EBodySatisfaction getBodySatisfaction(IVariableDatabase* db, int& lastSupport) const;

	VarID m_head;
	ValueSet m_headValue;
	vector<Literal> m_body;
	WatcherHandle m_headWatch = INVALID_WATCHER_HANDLE;
	vector<WatcherHandle> m_bodyWatches;
};

} // namespace Vertexy