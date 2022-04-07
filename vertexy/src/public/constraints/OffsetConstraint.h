// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ISolverConstraint.h"

namespace Vertexy
{

/** Constraint X = Y + Delta */
class OffsetConstraint : public ISolverConstraint
{
public:
	OffsetConstraint(const ConstraintFactoryParams& params, VarID sum, VarID term, int delta);

	struct OffsetConstraintFactory
	{
		static OffsetConstraint* construct(const ConstraintFactoryParams& params, VarID sum, VarID term, int delta, bool isPreunified = false);
	};

	using Factory = OffsetConstraintFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Offset; }
	virtual vector<VarID> getConstrainingVariables() const override { return {m_sum, m_term}; }
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;

protected:
	// Shifts the value set by a set amount. Returned set will be clamped/padded to size DestSize
	static ValueSet shiftBits(const ValueSet& bits, int amount, int destSize);

	VarID m_sum;
	VarID m_term;

	WatcherHandle m_handleSum = INVALID_WATCHER_HANDLE;
	WatcherHandle m_handleTerm = INVALID_WATCHER_HANDLE;

	int m_delta;
};

} // namespace Vertexy