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
	const vector<int>& edgeBlockedValues)
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

	return new ShortestPathConstraint(params, vertexData, sourceMask, needReachableMask, edgeData, edgeBlockedMask);
}

ShortestPathConstraint::ShortestPathConstraint(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
	const ValueSet& sourceMask,
	const ValueSet& requireReachableMask,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
	const ValueSet& edgeBlockedMask)
	: ITopologySearchConstraint(params, sourceGraphData, sourceMask, requireReachableMask, edgeGraphData, edgeBlockedMask, false)
{
}

bool ShortestPathConstraint::isValidDistance(int dist) const
{
	//TODO
	return true;
}

shared_ptr<RamalReps<BacktrackingDigraphTopology>> ShortestPathConstraint::makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const
{
	return make_shared<RamalRepsType>(graph, USE_RAMAL_REPS_BATCHING, false, true);
}

EventListenerHandle Vertexy::ShortestPathConstraint::addMinCallback(RamalRepsType& minReachable, VarID source)
{
	return minReachable.onDistanceChanged.add([this, source](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement && !isValidDistance(distance))
		{
			onDistanceChanged(changedVertex, source, true);
		}
	});
}

EventListenerHandle Vertexy::ShortestPathConstraint::addMaxCallback(RamalRepsType& maxReachable, VarID source)
{
	return maxReachable.onDistanceChanged.add([this, source](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement && isValidDistance(distance))
		{
			onDistanceChanged(changedVertex, source, false);
		}
	});
}



#undef SANITY_CHECKS
