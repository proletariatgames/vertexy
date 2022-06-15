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
bool ShortestPathConstraint::isPossiblyValid(const IVariableDatabase* db, const ReachabilitySourceData& src, int vertex) 
{
	// we do not need to check for reachability here because we always call isPossiblyReachable before calling this
	if (m_op == EConstraintOperator::GreaterThan || m_op == EConstraintOperator::GreaterThanEq)
	{
		if (src.minReachability->isReachable(vertex))
		{
			if (!isValidDistance(db, src.minReachability->getDistance(vertex)))
			{
				// the shortest path is too short, so our vertex is definitely unreachable (ie it cannot be a target)
				return false;
			}
			else
			{
				return true;
			}
		}
		else if (src.maxReachability->isReachable(vertex))
		{
			return true;
		}
	}
	else
	{
		if (src.minReachability->isReachable(vertex) && isValidDistance(db, src.minReachability->getDistance(vertex)))
		{
			return true;
		}
		else if (src.maxReachability->isReachable(vertex))
		{
			if (!isValidDistance(db, src.maxReachability->getDistance(vertex)))
			{
				return false;
			}

			return true;
		}
	}
	return false;
}

ITopologySearchConstraint::EValidityDetermination ShortestPathConstraint::determineValidityHelper(
	const IVariableDatabase* db,
	const ReachabilitySourceData& src,
	int vertex,
	VarID srcVertex) 
{
	if (m_op == EConstraintOperator::GreaterThan || m_op == EConstraintOperator::GreaterThanEq)
	{
		if (src.minReachability->isReachable(vertex))
		{
			if (!isValidDistance(db, src.minReachability->getDistance(vertex)))
			{
				// the shortest path is too short, so our vertex is definitely unreachable (ie it cannot be a target)
				return EValidityDetermination::DefinitelyInvalid;
			}

			if (definitelyIsSource(db, srcVertex))
			{
				return EValidityDetermination::DefinitelyValid;
			}
			else
			{
				return EValidityDetermination::PossiblyValid;
			}
		}
		else if (src.maxReachability->isReachable(vertex))
		{
			return EValidityDetermination::PossiblyValid;
		}
	}
	else
	{
		if (src.minReachability->isReachable(vertex) && isValidDistance(db, src.minReachability->getDistance(vertex)))
		{
			if (definitelyIsSource(db, srcVertex))
			{
				return EValidityDetermination::DefinitelyValid;
			}
			else
			{
				return EValidityDetermination::PossiblyValid;
			}
		}
		else if (src.maxReachability->isReachable(vertex))
		{
			if (!isValidDistance(db, src.maxReachability->getDistance(vertex)))
			{
				// undo any changes we have made during the current timestamp while processing all changes
				return EValidityDetermination::DefinitelyInvalid;
			}

			return EValidityDetermination::PossiblyValid;
		}
	}

	return EValidityDetermination::DefinitelyUnreachable;
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
			onVertexChanged(changedVertex, source, true);
		}
	});
}

EventListenerHandle ShortestPathConstraint::addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source)
{
	return maxReachable.onDistanceChanged.add([&](int changedVertex, int distance)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			onVertexChanged(changedVertex, source, false);
		}
	});
}

vector<Literal> Vertexy::ShortestPathConstraint::explainInvalid(const NarrowingExplanationParams& params)
{
	return IVariableDatabase::defaultExplainer(params);
}

void Vertexy::ShortestPathConstraint::createTempSourceData(ReachabilitySourceData& data, int vertexIndex) const
{
	ITopologySearchConstraint::createTempSourceData(data, vertexIndex);
#if REACHABILITY_USE_RAMAL_REPS
	data.minReachability = make_shared<RamalRepsType>(m_minGraph, false, false, false);
#else
	Data.minReachability = make_shared<ESTreeType>(MinGraph);
#endif
	data.minReachability->initialize(vertexIndex, &m_reachabilityEdgeLookup, m_totalNumEdges);
}

void ShortestPathConstraint::onVertexChanged(int vertexIndex, VarID sourceVar, bool inMinGraph)
{
	vxy_assert(!m_backtracking);
	vxy_assert(!m_explainingSourceRequirement);

	vxy_assert(m_edgeChangeDb != nullptr);
	vxy_assert(m_inEdgeChange);

	if (m_edgeChangeFailure)
	{
		// We already failed - avoid further failures that could confuse the conflict analyzer
		return;
	}

	auto reachability = determineValidity(m_edgeChangeDb, vertexIndex);

	// See if this vertex is definitely reachable by any source now
	//if (reachability == EReachabilityDetermination::DefinitelyValid)
	//{
	//	VarID var = m_sourceGraphData->get(vertexIndex);
	//	if (var.isValid())
	//	{
	//		if (m_edgeChangeDb->getPotentialValues(var).allPossible(m_requireValidityMask))
	//		{
	//			// only constrain if it's possible
	//			m_edgeChangeDb->constrainToValues(var, m_requireValidityMask, this);
	//		}
	//	}
	//}
	if (reachability == EValidityDetermination::DefinitelyUnreachable)
	{
		// vertexIndex became unreachable in the max graph
		VarID var = m_sourceGraphData->get(vertexIndex);
		//sanityCheckUnreachable(m_edgeChangeDb, vertexIndex);

		if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainNoReachability(params); }))
		{
			m_edgeChangeFailure = true;
		}
	}
	else if (reachability == EValidityDetermination::DefinitelyInvalid)
	{
		// vertexIndex became unreachable in the max graph
		VarID var = m_sourceGraphData->get(vertexIndex);
		//sanityCheckUnreachable(m_edgeChangeDb, vertexIndex);

		if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainInvalid(params); }))
		{
			m_edgeChangeFailure = true;
		}
	}
}


#undef SANITY_CHECKS
