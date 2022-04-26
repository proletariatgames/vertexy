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
		return dist > db->getMinimumPossibleDomainValue(m_distance);
	case EConstraintOperator::GreaterThanEq:
		return dist >= db->getMinimumPossibleDomainValue(m_distance);
	case EConstraintOperator::LessThan:
		return dist < db->getMaximumPossibleDomainValue(m_distance);
	case EConstraintOperator::LessThanEq:
		return dist <= db->getMaximumPossibleDomainValue(m_distance);
	}

	vxy_assert(false); //NotEqual not supported
	return false;
}

//is possibly reachable
bool ShortestPathConstraint::isReachable(const IVariableDatabase* db, const ReachabilitySourceData& src, int vertex) const
{
	if (!src.maxReachability->isReachable(vertex))
	{
		return false;
	}

	if (m_op == EConstraintOperator::LessThan || m_op == EConstraintOperator::LessThanEq)
	{
		return isValidDistance(db, src.maxReachability->getDistance(vertex));
	}
	else
	{
		return isValidDistance(db, src.minReachability->getDistance(vertex));
	}
}

ITopologySearchConstraint::EReachabilityDetermination ShortestPathConstraint::determineReachabilityHelper(
	const IVariableDatabase* db,
	const ReachabilitySourceData& src,
	int vertex,
	VarID srcVertex) const
{
	//if (src.minReachability->isReachable(vertex))
	//{
	//	if (definitelyIsSource(db, srcVertex))
	//	{
	//		return EReachabilityDetermination::DefinitelyReachable;
	//	}
	//	else
	//	{
	//		return EReachabilityDetermination::PossiblyReachable;
	//	}
	//}
	//else if (src.maxReachability->isReachable(vertex))
	//{
	//	return EReachabilityDetermination::PossiblyReachable;
	//}

	//return EReachabilityDetermination::DefinitelyUnreachable;

	if (src.minReachability->isReachable(vertex))
	{
		if ((m_op == EConstraintOperator::GreaterThan || m_op == EConstraintOperator::GreaterThanEq) && !isValidDistance(db, src.minReachability->getDistance(vertex)))
		{
			return EReachabilityDetermination::DefinitelyUnreachable;
		}

		if (definitelyIsSource(db, srcVertex))
		{
			return EReachabilityDetermination::DefinitelyReachable;
		}
		else
		{
			return EReachabilityDetermination::PossiblyReachable;
		}
	}
	else if (src.maxReachability->isReachable(vertex))
	{
		if ((m_op == EConstraintOperator::LessThan || m_op == EConstraintOperator::LessThanEq) && !isValidDistance(db, src.maxReachability->getDistance(vertex)))
		{
			return EReachabilityDetermination::DefinitelyUnreachable;
		}

		return EReachabilityDetermination::PossiblyReachable;
	}

	return EReachabilityDetermination::DefinitelyUnreachable;
}

shared_ptr<RamalReps<BacktrackingDigraphTopology>> ShortestPathConstraint::makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const
{
	return make_shared<RamalRepsType>(graph, USE_RAMAL_REPS_BATCHING, false, true);
}

EventListenerHandle ShortestPathConstraint::addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source)
{
	return minReachable.onDistanceChanged.add([&](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			if ((m_op == EConstraintOperator::GreaterThan || m_op == EConstraintOperator::GreaterThanEq))
			{
				if (isValidDistance(m_edgeChangeDb, distance))
				{
					onReachabilityChanged(changedVertex, source, true);
				}
			}
			else
			{
				if (minReachable.isReachable(changedVertex)) //TODO: could be an assert
				{
					onReachabilityChanged(changedVertex, source, true);
				}
			}
		}
	});
}

EventListenerHandle ShortestPathConstraint::addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source)
{
	return maxReachable.onDistanceChanged.add([&](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			if ((m_op == EConstraintOperator::LessThan || m_op == EConstraintOperator::LessThanEq))
			{
				if (!isValidDistance(m_edgeChangeDb, distance))
				{
					onReachabilityChanged(changedVertex, source, false);
				}
			}
			else
			{
				if (!maxReachable.isReachable(changedVertex))
				{
					onReachabilityChanged(changedVertex, source, false);
				}
			}
		}
	});
}

//EventListenerHandle ShortestPathConstraint::addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source)
//{
//	return minReachable.onReachabilityChanged.add([this, source](int changedVertex, bool isReachable)
//	{
//		if (!m_backtracking && !m_explainingSourceRequirement)
//		{
//			vxy_assert(isReachable);
//			onReachabilityChanged(changedVertex, source, true);
//		}
//	});
//}
//
//EventListenerHandle ShortestPathConstraint::addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source)
//{
//	return maxReachable.onReachabilityChanged.add([this, source](int changedVertex, bool isReachable)
//	{
//		if (!m_backtracking && !m_explainingSourceRequirement)
//		{
//			vxy_assert(!isReachable);
//			onReachabilityChanged(changedVertex, source, false);
//		}
//	});
//}



#undef SANITY_CHECKS
