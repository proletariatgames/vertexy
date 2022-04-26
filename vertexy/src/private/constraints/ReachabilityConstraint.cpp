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

bool ReachabilityConstraint::isReachable(const IVariableDatabase* db, const ReachabilitySourceData& src, int vertex) const
{
	return src.maxReachability->isReachable(vertex);
}

ITopologySearchConstraint::EReachabilityDetermination ReachabilityConstraint::determineReachabilityHelper(
	const IVariableDatabase* db, 
	const ReachabilitySourceData& src, 
	int vertex, 
	VarID srcVertex) const
{
	if (src.minReachability->isReachable(vertex))
	{
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
		return EReachabilityDetermination::PossiblyReachable;
	}

	return EReachabilityDetermination::DefinitelyUnreachable;
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

vector<Literal> ReachabilityConstraint::explainNoReachability(const NarrowingExplanationParams& params) const
{
	//return IVariableDatabase::defaultExplainer(params);

	vxy_assert_msg(m_variableToSourceVertexIndex.find(params.propagatedVariable) != m_variableToSourceVertexIndex.end(), "Not a vertex variable?");

	auto db = params.database;
	int conflictVertex = m_variableToSourceVertexIndex.find(params.propagatedVariable)->second;

	vector<Literal> lits;
	lits.push_back(Literal(params.propagatedVariable, m_notReachableMask));

	static hash_set<VarID> edgeVarsRecorded;
	edgeVarsRecorded.clear();

	ValueSet visited;
	visited.init(m_initialPotentialSources.size(), false);

	// For each source that could possibly exist...
	for (int potentialSrcIndex = 0; potentialSrcIndex < m_initialPotentialSources.size(); ++potentialSrcIndex)
	{
		if (visited[potentialSrcIndex])
		{
			continue;
		}
		visited[potentialSrcIndex] = true;

		VarID potentialSource = m_initialPotentialSources[potentialSrcIndex];

		int sourceVertex = m_variableToSourceVertexIndex.find(potentialSource)->second;
		if (sourceVertex == conflictVertex)
		{
			// Reachability sources cannot provide reachability to themselves
			continue;
		}

		// If this is currently a potential source...
		if (db->getPotentialValues(potentialSource).anyPossible(m_sourceMask))
		{
			//
			// Find the minimum cut of edges that would make this reachable
			//

			// Temporarily rewind the explanation graph to this time.
			// This will trigger OnExplanationGraphEdgeChange() for any edges re-added, so that FlowGraphEdges
			// will be the same state as when we processed the input variable.

			m_explanationGraph->rewindUntil(params.timestamp);

			vxy_sanity(!TopologySearchAlgorithm::canReach(m_explanationGraph, sourceVertex, conflictVertex));

			// Find the minimum cut in the maximum flow graph. This will correspond to edges that are disabled, because
			// A) we know that sourceVertex can't reach conflictVertex without going through a disabled edge
			// B) blocked edges are set to a flow capacity of 1, and blocked edges have infinite flow
			vector<tuple<int, int>> cutEdges;
			m_maxFlowAlgo.getMaxFlow(*m_sourceGraph.get(), sourceVertex, conflictVertex, m_flowGraphEdges, m_flowGraphLookup, &cutEdges);
			vxy_assert(!cutEdges.empty());

			// Now that we've found the cut, bring the explanation graph back to current state.
			m_explanationGraph->fastForward();

			for (tuple<int, int>& edge : cutEdges)
			{
				int edgeNode = m_edgeGraph->getVertexForSourceEdge(get<0>(edge), get<1>(edge));
				VarID edgeVar = m_edgeGraphData->get(edgeNode);
				if (edgeVarsRecorded.find(edgeVar) == edgeVarsRecorded.end())
				{
					edgeVarsRecorded.insert(edgeVar);

					vxy_assert(!db->anyPossible(edgeVar, m_edgeOpenMask)); //hits this with distance = 100, 200
					lits.push_back(Literal(edgeVar, m_edgeOpenMask));
				}
			}

			// For every other potential source, see if this graph also holds. It holds if the other source is on the
			// same side as this source, hence would have to cross the same edge boundary.
			for (int j = potentialSrcIndex + 1; j < m_initialPotentialSources.size(); ++j)
			{
				if (visited[j])
				{
					continue;
				}

				int vertex = m_variableToSourceVertexIndex.find(potentialSource)->second;
				if (vertex == conflictVertex)
				{
					continue;
				}

				if (db->getPotentialValues(m_initialPotentialSources[j]).anyPossible(m_sourceMask))
				{
					if (!m_maxFlowAlgo.onSinkSide(vertex, m_flowGraphEdges, m_flowGraphLookup))
					{
						visited[j] = true;
					}
				}
			}
		}
		// Not currently a potential source. For now, the conservative explanation is that we'd be able to reach if it
		// was.
		else
		{
			lits.push_back(Literal(potentialSource, m_sourceMask));
		}
	}

	return lits;
}

vector<Literal> ReachabilityConstraint::explainRequiredSource(const NarrowingExplanationParams& params, VarID removedSource)
{
	//return IVariableDatabase::defaultExplainer(params);

	vxy_assert(!m_explainingSourceRequirement);
	ValueGuard<bool> guard(m_explainingSourceRequirement, true);

	VarID sourceVar = params.propagatedVariable;
	int sourceVertex = m_variableToSourceVertexIndex[sourceVar];

	auto db = params.database;

	vector<Literal> lits;
	lits.push_back(Literal(sourceVar, m_sourceMask));

	m_maxGraph->rewindUntil(params.timestamp);

#if REACHABILITY_USE_RAMAL_REPS
	{
		// Batch-update to rewound graph state
		for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
		{
			it->second.maxReachability->refresh();
		}
	}
#endif

	// Create any sources that might've gotten removed since this happened.
	// (We'll clean them up after)
	vector<VarID> tempSources;
	for (VarID potentialSource : m_initialPotentialSources)
	{
		if (db->anyPossible(potentialSource, m_sourceMask))
		{
			if (m_reachabilitySources.find(potentialSource) == m_reachabilitySources.end())
			{
				tempSources.push_back(potentialSource);

				int vertexIndex = m_variableToSourceVertexIndex[potentialSource];

				ReachabilitySourceData data;
#if REACHABILITY_USE_RAMAL_REPS
				data.maxReachability = make_shared<RamalRepsType>(m_maxGraph, false, false, false);
#else
				Data.maxReachability = make_shared<ESTreeType>(MaxGraph);
#endif
				data.maxReachability->initialize(vertexIndex, &m_reachabilityEdgeLookup, m_totalNumEdges);

				m_reachabilitySources[potentialSource] = data;
			}
		}
		else if (potentialSource != sourceVar)
		{
			lits.push_back(Literal(potentialSource, m_sourceMask));
		}
	}

	int removedSourceLitIdx = -1;
	if (removedSource.isValid())
	{
		// This became a required source because RemovedSource was removed, and some definitely-reachable vertices were
		// only reachable by this source.
		vxy_assert(!db->anyPossible(removedSource, m_sourceMask));
		removedSourceLitIdx = indexOfPredicate(lits.begin(), lits.end(), [&](auto& lit) { return lit.variable == removedSource; });
		vxy_assert(removedSourceLitIdx >= 0);
	}

	//
	// This became a required source because some variable(s) were marked as required, and we are the only
	// source that can reach them. Find those variables.
	//
	bool foundSupports = false;
	auto& ourReachability = m_reachabilitySources[sourceVar].maxReachability;
	auto searchCallback = [&](int vertex, int parent, int edgeIndex)
	{
		if (ourReachability->isReachable(vertex))
		{
			VarID vertexVar = m_sourceGraphData->get(vertex);
			if (!db->anyPossible(vertexVar, m_notReachableMask))
			{
				bool reachableFromAnotherSource = false;
				for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
				{
					if (it->first != sourceVar && it->first != vertexVar && it->second.maxReachability->isReachable(vertex))
					{
						reachableFromAnotherSource = true;
						break;
					}
				}

				if (!reachableFromAnotherSource)
				{
					vxy_assert(m_reachabilitySources[sourceVar].maxReachability->isReachable(vertex));
					// make sure we don't add the same literal twice!
					auto found = find_if(lits.begin(), lits.end(), [&](auto& lit) { return lit.variable == vertexVar; });
					if (found != lits.end())
					{
						vxy_assert(found->variable == vertexVar);
						found->values.include(m_notReachableMask);
					}
					else
					{
						lits.push_back(Literal(vertexVar, m_notReachableMask));
					}
					foundSupports = true;
				}
			}
			return ETopologySearchResponse::Continue;
		}
		else
		{
			return ETopologySearchResponse::Skip;
		}
	};
	m_dfs.search(*m_sourceGraph.get(), m_variableToSourceVertexIndex[sourceVar], searchCallback);
	vxy_assert(foundSupports);

	for (VarID tempSource : tempSources)
	{
		m_reachabilitySources.erase(tempSource);
	}

	m_maxGraph->fastForward();
	return lits;
}

#undef SANITY_CHECKS
