// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ShortestPathConstraint.h"

#include "variable/IVariableDatabase.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

#define SANITY_CHECKS VERTEXY_SANITY_CHECKS

static constexpr int OPEN_EDGE_FLOW = INT_MAX >> 1;
static constexpr int CLOSED_EDGE_FLOW = 1;
static constexpr bool USE_RAMAL_REPS_BATCHING = true;

ShortestPathConstraint* ShortestPathConstraint::ShortestPathFactory::construct(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& vertexData,
	const vector<int>& sourceValues,
	const vector<int>& needReachableValues,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeData,
	const vector<int>& edgeBlockedValues,
	EConstraintOperator op,
	VarID distance)
{
	// Get an example graph variable
	VarID graphVar;
	for (int i = 0; i < vertexData->getSource()->getNumVertices(); ++i)
	{
		if (vertexData->get(i).isValid())
		{
			graphVar = vertexData->get(i);
			break;
		}
	}
	vxy_assert(graphVar.isValid());

	// Get an example edge variable
	VarID edgeVar;
	for (int i = 0; i < edgeData->getSource()->getNumVertices(); ++i)
	{
		if (edgeData->get(i).isValid())
		{
			edgeVar = edgeData->get(i);
			break;
		}
	}
	vxy_assert(edgeVar.isValid());

	ValueSet sourceMask = params.valuesToInternal(graphVar, sourceValues);
	ValueSet needReachableMask = params.valuesToInternal(graphVar, needReachableValues);
	ValueSet edgeBlockedMask = params.valuesToInternal(edgeVar, edgeBlockedValues);

	return new ShortestPathConstraint(params, vertexData, sourceMask, needReachableMask, edgeData, edgeBlockedMask, op, distance);
}

ShortestPathConstraint::ShortestPathConstraint(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
	const ValueSet& sourceMask,
	const ValueSet& requireReachableMask,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
	const ValueSet& edgeBlockedMask,
	EConstraintOperator op,
	VarID distance)
	: ITopologySearchConstraint(params, sourceGraphData, sourceMask, requireReachableMask, edgeGraphData, edgeBlockedMask)
	, m_op(op)
	, m_distance(distance)
{
	vxy_assert(m_op != EConstraintOperator::NotEqual); //NotEqual not supported
}

bool ShortestPathConstraint::isValidDistance(const IVariableDatabase* db, int dist) const
{
	switch (m_op)
	{
	case EConstraintOperator::GreaterThan:
		return dist > db->getMinimumPossibleValue(m_distance);
	case EConstraintOperator::GreaterThanEq:
		return dist >= db->getMinimumPossibleValue(m_distance);
	case EConstraintOperator::LessThan:
		return dist < db->getMaximumPossibleValue(m_distance);
	case EConstraintOperator::LessThanEq:
		return dist <= db->getMaximumPossibleValue(m_distance);
	}

	vxy_assert(false); //NotEqual not supported
	return false;
}

shared_ptr<RamalReps<BacktrackingDigraphTopology>> ShortestPathConstraint::makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const
{
	return make_shared<RamalRepsType>(graph, USE_RAMAL_REPS_BATCHING, false, true);
}

EventListenerHandle Vertexy::ShortestPathConstraint::addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source)
{
	return minReachable.onDistanceChanged.add([this, db, source](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement && !isValidDistance(db, distance))
		{
			onReachabilityChanged(changedVertex, source, true);
		}
	});
}

EventListenerHandle Vertexy::ShortestPathConstraint::addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source)
{
	return maxReachable.onDistanceChanged.add([this, db, source](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement && isValidDistance(db, distance))
		{
			onReachabilityChanged(changedVertex, source, false);
		}
	});
}



#undef SANITY_CHECKS
