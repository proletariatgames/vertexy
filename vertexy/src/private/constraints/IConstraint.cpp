// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/IConstraint.h"
#include "ConstraintSolver.h"
#include "constraints/ConstraintFactoryParams.h"

using namespace Vertexy;

IConstraint::IConstraint(const ConstraintFactoryParams& params)
    : m_id(params.getNextConstraintID())
{
    if (params.getGraphRelationInfo().getGraph() != nullptr)
    {
        m_graphRelationInfo = move(make_unique<ConstraintGraphRelationInfo>(params.getGraphRelationInfo()));
    }
}

vector<Literal> IConstraint::explain(const NarrowingExplanationParams& params) const
{
    // Find all dependent variables for this constraint that were previously narrowed, and add their (inverted) value to the list.
    // The clause will look like:
    // (Arg1 != Arg1Values OR Arg2 != Arg2Values OR [...] OR PropagatedVariable == PropagatedValues)
    const vector<VarID>& constraintVars = params.solver->getVariablesForConstraint(params.constraint);
    vector<Literal> clauses;
    clauses.reserve(constraintVars.size());

    bool foundPropagated = false;
    for (int i = 0; i < constraintVars.size(); ++i)
    {
        VarID arg = constraintVars[i];
        clauses.push_back(Literal(arg, params.database->getPotentialValues(arg)));
        clauses.back().values.invert();

        if (arg == params.propagatedVariable)
        {
            foundPropagated = true;
            clauses.back().values.pad(params.propagatedValues.size(), false);
            clauses.back().values.include(params.propagatedValues);
        }
    }

    vxy_assert(foundPropagated || params.propagatedVariable == VarID::INVALID);
    return clauses;
}

const shared_ptr<ITopology>& IConstraint::getGraph() const
{
    static shared_ptr<ITopology> nullRet = nullptr;
    return (m_graphRelationInfo != nullptr && m_graphRelationInfo->isValid()) ? m_graphRelationInfo->getGraph() : nullRet;
}

const ConstraintGraphRelationInfo* IConstraint::getGraphRelationInfo() const
{
    return m_graphRelationInfo != nullptr && m_graphRelationInfo->isValid() ? m_graphRelationInfo.get() : nullptr;
}

bool IConstraint::getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outInfo) const
{
    if (m_graphRelationInfo == nullptr || m_graphRelationInfo->getGraph() == nullptr)
    {
        return false;
    }

    outInfo.reset(m_graphRelationInfo->getGraph(), m_graphRelationInfo->getSourceGraphVertex());
    for (auto& lit : literals)
    {			
        if (GraphVariableRelationPtr varRelation;
            m_graphRelationInfo->getVariableRelation(lit.variable, varRelation))
        {
            outInfo.addVariableRelation(lit.variable, varRelation);
        }
        else if (GraphLiteralRelationPtr litRelation;
            m_graphRelationInfo->getLiteralRelation(lit, litRelation))
        {
            outInfo.addLiteralRelation(lit, litRelation);
        }
        else
        {
            outInfo.invalidate();
            return false;
        }
    }

    return true;
}
