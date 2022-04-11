// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ConstraintFactoryParams.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

static const ConstraintGraphRelationInfo NO_RELATION_INFO;

ConstraintFactoryParams::ConstraintFactoryParams(ConstraintSolver& solver)
	: m_solver(solver)
	, m_graphRelationInfo(NO_RELATION_INFO)
{
}

ConstraintFactoryParams::ConstraintFactoryParams(ConstraintSolver& solver, const ConstraintGraphRelationInfo& relationInfo)
	: m_solver(solver)
	, m_graphRelationInfo(relationInfo)
{
}

ConstraintFactoryParams::ConstraintFactoryParams(const ConstraintFactoryParams& orig, const ConstraintGraphRelationInfo& relationInfo)
	: m_solver(orig.m_solver)
	, m_graphRelationInfo(relationInfo)
{
}


int ConstraintFactoryParams::getNextConstraintID() const
{
	return m_solver.getNextConstraintID();
}

VarID ConstraintFactoryParams::getOrCreateOffsetVariable(VarID varID, int minDomain, int maxDomain) const
{
	return m_solver.getOrCreateOffsetVariable(varID, minDomain, maxDomain);
}

vector<VarID> ConstraintFactoryParams::unifyVariableDomains(const vector<VarID>& vars, int* outNewMinDomain) const
{
	return m_solver.unifyVariableDomains(vars, outNewMinDomain);
}

const SolverVariableDomain& ConstraintFactoryParams::getDomain(VarID varID) const
{
	return m_solver.getDomain(varID);
}

ValueSet ConstraintFactoryParams::valuesToInternal(VarID var, const vector<int>& values) const
{
	auto& domain = getDomain(var);
	SolverVariableDomain internalDomain(0, domain.getDomainSize() - 1);

	ValueSet output(internalDomain.getDomainSize(), false);
	for (int value : values)
	{
		output[internalDomain.getIndexForValue(value)] = true;
	}
	return output;
}

int ConstraintFactoryParams::registerGraph(const shared_ptr<ITopology>& graph) const
{
	int id = indexOf(m_solver.m_graphs.begin(), m_solver.m_graphs.end(), graph);
	if (id < 0)
	{
		id = m_solver.m_graphs.size();
		m_solver.m_graphs.push_back(graph);
	}
	return id;
}

void ConstraintFactoryParams::markChildConstraint(IConstraint* cons) const
{
	vxy_sanity(m_solver.m_constraints[cons->getID()].get() == cons);
	m_solver.m_constraintIsChild[cons->getID()] = true;
}
