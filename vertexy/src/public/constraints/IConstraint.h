// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "variable/IVariableWatchSink.h"
#include <EASTL/shared_ptr.h>

#include "SignedClause.h"

namespace Vertexy
{

class ConstraintGraphRelationInfo;
class ConstraintFactoryParams;

class ITopology;

/** Interface for constraints */
class IConstraint : public IVariableWatchSink
{
public:
	IConstraint(const ConstraintFactoryParams& params);

	virtual IConstraint* asConstraint() override final
	{
		return this;
	}

	// End IVariableWatchSink

	// Return the type of constraint. Used in place of RTTI
	virtual EConstraintType getConstraintType() const = 0;

	// Utility function: return self as a clause constraint if we are one
	virtual class ClauseConstraint* asClauseConstraint()
	{
		return nullptr;
	}

	// Return the set of variables that this constraint constrains.
	virtual vector<VarID> getConstrainingVariables() const = 0;

	// Initialize the constraint, enforcing initial arc consistency.
	virtual bool initialize(IVariableDatabase* db) = 0;
	// Alternative version if you need access to the outer constraint, for child constraints
	virtual bool initialize(IVariableDatabase* db, IConstraint* outerConstraint)
	{
		return initialize(db);
	}

	// Called when initial arc consistency has been set up for all constraints.
	virtual void onInitialArcConsistency(IVariableDatabase* db)
	{
	}

	// Called by the constraint solver if the constraint requested it be enqueued via QueueConstraintPropagation.
	// Does not need to be implemented if the constraint never does so.
	// InnerCons is used for disjunctive constraints to specify the inner constraint that requested propagation.
	virtual bool propagate(IVariableDatabase* db)
	{
		vxy_fail();
		return true;
	}

	// Whether this constraint needs to be notified whenever the solver backtracks.
	// You should not override this: inherit from IBacktrackingSolverConstraint instead.
	virtual bool needsBacktracking() const { return false; }

	// Reset any internal state for the constraint. Used if the constraint is used in a different solver.
	virtual void reset(IVariableDatabase* db) = 0;

	// Checks if the constraint is currently in conflict. Used during conflict analysis.
	virtual bool checkConflicting(IVariableDatabase* db) const = 0;

	// Explain (as a set of clause disjunctions) why a propagation happened
	virtual vector<Literal> explain(const NarrowingExplanationParams& params) const;

	inline int getID() const { return m_id; }

	const shared_ptr<ITopology>& getGraph() const;

	const ConstraintGraphRelationInfo* getGraphRelationInfo() const;

	// Given a series of literals, return a set of relations for those literals.
	// Returns false if a set of relations cannot be provided for all literals.
	virtual bool getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outInfo) const;

protected:
	// Unique ID for this instance
	int m_id;
	unique_ptr<ConstraintGraphRelationInfo> m_graphRelationInfo;
};

} // namespace Vertexy