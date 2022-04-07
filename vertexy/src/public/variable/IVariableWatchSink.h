// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"

namespace Vertexy
{
class IVariableDatabase;

// Interface for classes that wish to register for variable changes
class IVariableWatchSink
{
public:
	virtual ~IVariableWatchSink()
	{
	}

	// Called when the watch for the specified variable is triggered.
	// If false is returned, propagation halts and conflict analysis begins.
	// Note that only constraints should return false!
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle) = 0;

	// Return true if this sink is a constraint, or acting on behalf of a constraint.
	virtual class ISolverConstraint* asConstraint() { return nullptr; }
};

} // namespace Vertexy