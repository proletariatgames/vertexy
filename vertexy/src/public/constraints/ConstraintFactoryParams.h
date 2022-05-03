// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "ConstraintGraphRelationInfo.h"

namespace Vertexy
{

class SolverVariableDomain;
class ITopology;

/** Wraps data passed into constructor for constraints */
class ConstraintFactoryParams : public IVariableDomainProvider
{
public:
	ConstraintFactoryParams(ConstraintSolver& solver);
	ConstraintFactoryParams(ConstraintSolver& solver, const ConstraintGraphRelationInfo& relationInfo);
	ConstraintFactoryParams(const ConstraintFactoryParams& orig, const ConstraintGraphRelationInfo& relationInfo);

	int getNextConstraintID() const;

	// Unify all variable domains so that they start at the same base value
	vector<VarID> unifyVariableDomains(const vector<VarID>& vars, int* outNewMinDomain = nullptr) const;

	// Return a variable representing V in a different (broader) domain. Creates one if it doesn't already exist.
	VarID getOrCreateOffsetVariable(VarID varID, int minDomain, int maxDomain) const;

	ValueSet valuesToInternal(VarID var, const vector<int>& values) const;

	const ConstraintGraphRelationInfo& getGraphRelationInfo() const { return m_graphRelationInfo; }

	virtual const SolverVariableDomain& getDomain(VarID varID) const override;

	int registerGraph(const shared_ptr<ITopology>& graph) const;

	void markChildConstraint(IConstraint* cons) const;

	inline ConstraintSolver& getSolver() { return m_solver; }

private:
	ConstraintSolver& m_solver;
	ConstraintGraphRelationInfo m_graphRelationInfo;
};

} // namespace Vertexy