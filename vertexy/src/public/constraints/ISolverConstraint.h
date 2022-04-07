// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "ConstraintFactoryParams.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

class ITopology;

/** Interface for constraints */
class ISolverConstraint : public IVariableWatchSink
{
public:
	ISolverConstraint(const ConstraintFactoryParams& params)
		: m_id(params.getNextConstraintID())
	{
		if (params.getGraphRelationInfo().graph != nullptr)
		{
			m_graphRelationInfo = move(make_unique<ConstraintGraphRelationInfo>(params.getGraphRelationInfo()));
		}
	}

	virtual ISolverConstraint* asConstraint() override final
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
	virtual bool initialize(IVariableDatabase* db, ISolverConstraint* outerConstraint)
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

	// Optional override: if the constraint can detect conflict through a means other than narrowing a variable
	// to empty set, this will be called to explain if needed. For example, the AllDifferent constraint
	// will trigger if it detects a set of variables V within domain D where sizeof(V) > sizeof(D).
	virtual bool explainConflict(const IVariableDatabase* db, vector<Literal>& outClauses) const
	{
		return false;
	}

	inline int getID() const { return m_id; }

	inline const shared_ptr<ITopology>& getGraph() const
	{
		static shared_ptr<ITopology> nullRet = nullptr;
		return (m_graphRelationInfo != nullptr && m_graphRelationInfo->isValid) ? m_graphRelationInfo->graph : nullRet;
	}

	inline const ConstraintGraphRelationInfo* getGraphRelationInfo() const
	{
		return m_graphRelationInfo != nullptr && m_graphRelationInfo->isValid ? m_graphRelationInfo.get() : nullptr;
	}

	// Given a series of literals, return a set of relations for those literals.
	// Returns false if a set of relations cannot be provided for all literals.
	virtual bool getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outInfo) const
	{
		if (m_graphRelationInfo == nullptr || m_graphRelationInfo->graph == nullptr)
		{
			return false;
		}

		outInfo.reset(m_graphRelationInfo->graph, m_graphRelationInfo->sourceGraphVertex);
		outInfo.reserve(literals.size());
		for (auto& lit : literals)
		{
			ConstraintGraphRelation foundRelation;
			if (!m_graphRelationInfo->getRelation(lit.variable, foundRelation))
			{
				outInfo.clear();
				return false;
			}
			outInfo.addRelation(lit.variable, foundRelation);
		}

		vxy_assert(outInfo.relations.size() == literals.size());
		return true;
	}

protected:
	// Unique ID for this instance
	int m_id;
	unique_ptr<ConstraintGraphRelationInfo> m_graphRelationInfo;
};

} // namespace Vertexy