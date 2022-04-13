// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ReachabilityConstraint.h"

#include "variable/IVariableDatabase.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

#define SANITY_CHECKS VERTEXY_SANITY_CHECKS

static constexpr int OPEN_EDGE_FLOW = INT_MAX >> 1;
static constexpr int CLOSED_EDGE_FLOW = 1;
static constexpr bool USE_RAMAL_REPS_BATCHING = true;

ReachabilityConstraint* ReachabilityConstraint::ReachabilityFactory::construct(
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

	return new ReachabilityConstraint(params, vertexData, sourceMask, needReachableMask, edgeData, edgeBlockedMask);
}

ReachabilityConstraint::ReachabilityConstraint(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
	const ValueSet& sourceMask,
	const ValueSet& requireReachableMask,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
	const ValueSet& edgeBlockedMask)
	: ITopologySearchConstraint(params, sourceGraphData, sourceMask, requireReachableMask, edgeGraphData, edgeBlockedMask)
{

}

bool ReachabilityConstraint::isValidDistance(const IVariableDatabase* db, int dist) const
{
	return dist < INT_MAX;
}

shared_ptr<RamalReps<BacktrackingDigraphTopology>> ReachabilityConstraint::makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const
{
	return make_shared<RamalRepsType>(graph, USE_RAMAL_REPS_BATCHING, true, false);
}


EventListenerHandle ReachabilityConstraint::addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source)
{
	return minReachable.onReachabilityChanged.add([this, source](int changedVertex, bool isReachable)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			vxy_assert(isReachable);
			onReachabilityChanged(changedVertex, source, true);
		}
	});
}

EventListenerHandle ReachabilityConstraint::addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source)
{
	return maxReachable.onReachabilityChanged.add([this, source](int changedVertex, bool isReachable)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			vxy_assert(!isReachable);
			onReachabilityChanged(changedVertex, source, false);
		}
	});
}

#undef SANITY_CHECKS
