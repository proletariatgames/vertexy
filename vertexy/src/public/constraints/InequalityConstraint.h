// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ConstraintOperator.h"
#include "ISolverConstraint.h"

namespace Vertexy
{

/** Represents an inequality between two variables e.g. "X <= Y" */
class InequalityConstraint : public ISolverConstraint
{
public:
	InequalityConstraint(const ConstraintFactoryParams& params, VarID a, EConstraintOperator op, VarID b);

	struct InequalityConstraintFactory
	{
		static InequalityConstraint* construct(const ConstraintFactoryParams& params, VarID a, EConstraintOperator op, VarID b);
	};

	using Factory = InequalityConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Inequality; }
	virtual vector<VarID> getConstrainingVariables() const override { return {m_a, m_b}; }
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;

protected:
	vector<Literal> explainer(const NarrowingExplanationParams& params) const;
	bool applyOperator(IVariableDatabase* db, EConstraintOperator op, VarID lhs);
	static EConstraintOperator getMirrorOperator(EConstraintOperator op);

	VarID m_a;
	VarID m_b;

	WatcherHandle m_handleA = INVALID_WATCHER_HANDLE;
	WatcherHandle m_handleB = INVALID_WATCHER_HANDLE;

	EConstraintOperator m_operator;
	EConstraintOperator m_mirrorOperator;
};

} // namespace Vertexy