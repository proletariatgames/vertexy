// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/TopologySearchConstraint.h"

#include "variable/IVariableDatabase.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

#define SANITY_CHECKS VERTEXY_SANITY_CHECKS

static constexpr int OPEN_EDGE_FLOW = INT_MAX >> 1;
static constexpr int CLOSED_EDGE_FLOW = 1;
static constexpr bool USE_RAMAL_REPS_BATCHING = true;

ITopologySearchConstraint::ITopologySearchConstraint(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
	const ValueSet& sourceMask,
	const ValueSet& requireReachableMask,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
	const ValueSet& edgeBlockedMask)
	: IBacktrackingSolverConstraint(params) //ApplyGraphRelation(Params, SourceGraphData, EdgeGraphData))
	, m_edgeWatcher(*this)
	, m_sourceGraphData(sourceGraphData)
	, m_sourceGraph(sourceGraphData->getSource())
	, m_edgeGraphData(edgeGraphData)
	, m_edgeGraph(edgeGraphData->getSource()->getImplementation<EdgeTopology>())
	, m_minGraph(make_shared<BacktrackingDigraphTopology>())
	, m_maxGraph(make_shared<BacktrackingDigraphTopology>())
	, m_explanationGraph(make_shared<BacktrackingDigraphTopology>())
	, m_sourceMask(sourceMask)
	, m_requireReachableMask(requireReachableMask)
	, m_edgeBlockedMask(edgeBlockedMask)
{
	m_notSourceMask = sourceMask.inverted();
	m_notReachableMask = requireReachableMask.inverted();
	m_edgeOpenMask = edgeBlockedMask.inverted();

	for (int i = 0; i < m_sourceGraph->getNumVertices(); ++i)
	{
		VarID var = sourceGraphData->get(i);
		if (var.isValid())
		{
			m_variableToSourceVertexIndex[var] = i;
		}
	}

	vxy_assert(m_edgeGraph->getSource() == m_sourceGraph);
	for (int i = 0; i < m_edgeGraph->getNumVertices(); ++i)
	{
		VarID var = edgeGraphData->get(i);
		if (var.isValid())
		{
			m_variableToSourceEdgeIndex[var] = i;
		}
	}
}

bool ITopologySearchConstraint::getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outRelations) const
{
	// First search through all provided literals to find the minimum graph vertex.
	// Note: some vertices will be edges, some will be tiles.
	int minGraphVertex = m_sourceGraph->getNumVertices();

	vector<int> vertices;
	vector<bool> isEdgeNode;

	vertices.reserve(literals.size());
	isEdgeNode.reserve(literals.size());

	for (auto& lit : literals)
	{
		int graphVertex = m_sourceGraphData->indexOf(lit.variable);
		if (graphVertex < 0)
		{
			int edgeNode = m_edgeGraphData->indexOf(lit.variable);
			vxy_assert(edgeNode >= 0);

			vertices.push_back(edgeNode);
			isEdgeNode.push_back(true);

			int edgeFrom, edgeTo;
			bool bidirectional;
			m_edgeGraph->getSourceEdgeForVertex(edgeNode, edgeFrom, edgeTo, bidirectional);

			graphVertex = min(edgeFrom, edgeTo);
		}
		else
		{
			vertices.push_back(graphVertex);
			isEdgeNode.push_back(false);
		}
		vxy_assert(graphVertex >= 0);
		minGraphVertex = min(graphVertex, minGraphVertex);
	}

	// We always provide relations in terms of the source graph.
	// Relations are anchored to the minimum vertex ID found (maps to top-leftmost in a grid graph).
	outRelations.reset(m_sourceGraph, minGraphVertex);

	// create the relations!
	outRelations.reserve(literals.size());
	for (int i = 0; i != literals.size(); ++i)
	{
		bool isEdge = isEdgeNode[i];

		if (!isEdge)
		{
			int vertex = vertices[i];
			if (vertex != minGraphVertex)
			{
				TopologyLink link;
				if (!m_sourceGraph->getTopologyLink(minGraphVertex, vertex, link))
				{
					// no path specified in graph
					// TODO: assert?
					outRelations.clear();
					return false;
				}

				auto linkRel = make_shared<TTopologyLinkGraphRelation<VarID>>(m_sourceGraphData, link);
				outRelations.addRelation(m_sourceGraphData->get(vertex), linkRel);
			}
			else
			{
				auto selfRel = make_shared<TVertexToDataGraphRelation<VarID>>(m_sourceGraphData);
				outRelations.addRelation(m_sourceGraphData->get(vertex), selfRel);
			}
		}
		else
		{
			int edgeNode = vertices[i];

			// edge variable: get the source node if the edge
			int edgeOrigin, edgeDestination;
			bool bidirectional;
			m_edgeGraph->getSourceEdgeForVertex(edgeNode, edgeOrigin, edgeDestination, bidirectional);

			int nodeEdgeIndex = -1;
			for (int e = 0; e < m_sourceGraph->getNumOutgoing(edgeOrigin); ++e)
			{
				if (int testDest; m_sourceGraph->getOutgoingDestination(edgeOrigin, e, testDest) && testDest == edgeDestination)
				{
					nodeEdgeIndex = e;
					break;
				}
			}
			if (nodeEdgeIndex < 0)
			{
				vxy_assert_msg(false, "Edge node %d has source graph node origin %d, but can't find edgeIndex in source graph!", edgeNode, edgeOrigin);
				outRelations.clear();
				return false;
			}

			auto nodeToEdgeNodeRel = make_shared<TVertexEdgeToEdgeGraphVertexGraphRelation<ITopology>>(m_sourceGraph, m_edgeGraph, nodeEdgeIndex);
			auto nodeToEdgeVarRel = nodeToEdgeNodeRel->map(make_shared<TVertexToDataGraphRelation<VarID>>(m_edgeGraphData));

			if (edgeOrigin != minGraphVertex)
			{
				TopologyLink link;
				if (!m_sourceGraph->getTopologyLink(minGraphVertex, edgeOrigin, link))
				{
					vxy_assert_msg(false, "expected link between vertices %d -> %d", minGraphVertex, edgeOrigin);
					outRelations.clear();
					return false;
				}

				auto linkRel = make_shared<TopologyLinkIndexGraphRelation>(m_sourceGraph, link);
				outRelations.addRelation(m_edgeGraphData->get(edgeNode), linkRel->map(nodeToEdgeVarRel));
			}
			else
			{
				outRelations.addRelation(m_edgeGraphData->get(edgeNode), nodeToEdgeVarRel);
			}
		}
	}

	vxy_assert(outRelations.relations.size() == literals.size());
	return true;
}

bool ITopologySearchConstraint::initialize(IVariableDatabase* db)
{
	// Add all vertices to min/max graphs
	for (int vertexIndex = 0; vertexIndex < m_sourceGraph->getNumVertices(); ++vertexIndex)
	{
		VarID vertexVar = m_sourceGraphData->get(vertexIndex);

		int addedIdx = m_maxGraph->addVertex();
		vxy_assert(addedIdx == vertexIndex);

		addedIdx = m_minGraph->addVertex();
		vxy_assert(addedIdx == vertexIndex);

		addedIdx = m_explanationGraph->addVertex();
		vxy_assert(addedIdx == vertexIndex);

		if (vertexVar.isValid())
		{
			m_vertexWatchHandles[vertexVar] = db->addVariableWatch(vertexVar, EVariableWatchType::WatchModification, this);
		}
	}

	hash_map<int, hash_map<int, int>> edgeCapacities;

	m_totalNumEdges = 0;

	// Add all definitely-open edges to the min graph, and all possibly-open edges to the max graph
	m_reachabilityEdgeLookup.resize(m_sourceGraph->getNumVertices());
	for (int sourceVertex = 0; sourceVertex < m_sourceGraph->getNumVertices(); ++sourceVertex)
	{
		auto foundVertexEdges = edgeCapacities.find(sourceVertex);
		if (foundVertexEdges == edgeCapacities.end())
		{
			foundVertexEdges = edgeCapacities.insert(sourceVertex).first;
		}

		m_reachabilityEdgeLookup[sourceVertex].reserve(m_sourceGraph->getNumOutgoing(sourceVertex));
		for (int edgeIndex = 0; edgeIndex < m_sourceGraph->getNumOutgoing(sourceVertex); ++edgeIndex)
		{
			int destVertex;
			if (m_sourceGraph->getOutgoingDestination(sourceVertex, edgeIndex, destVertex))
			{
				m_reachabilityEdgeLookup[sourceVertex].push_back(make_tuple(destVertex, m_totalNumEdges));
				++m_totalNumEdges;

				int edgeNode = m_edgeGraph->getVertexForSourceEdge(sourceVertex, destVertex);
				vxy_assert(edgeNode >= 0);
				VarID edgeVar = m_edgeGraphData->get(edgeNode);

				bool edgeIsClosed = true;
				if (edgeVar.isValid())
				{
					if (definitelyOpenEdge(db, edgeVar))
					{
						edgeIsClosed = false;

						m_minGraph->initEdge(sourceVertex, destVertex);
						m_maxGraph->initEdge(sourceVertex, destVertex);
						m_explanationGraph->initEdge(sourceVertex, destVertex);
					}
					else if (possiblyOpenEdge(db, edgeVar))
					{
						edgeIsClosed = false;

						if (m_edgeWatchHandles.find(edgeVar) == m_edgeWatchHandles.end())
						{
							m_edgeWatchHandles[edgeVar] = db->addVariableWatch(edgeVar, EVariableWatchType::WatchModification, &m_edgeWatcher);
						}
						m_maxGraph->initEdge(sourceVertex, destVertex);
						m_explanationGraph->initEdge(sourceVertex, destVertex);
					}
				}
				else
				{
					edgeIsClosed = false;

					// No variable for this edge, so should always exist
					m_minGraph->initEdge(sourceVertex, destVertex);
					m_maxGraph->initEdge(sourceVertex, destVertex);
					m_explanationGraph->initEdge(sourceVertex, destVertex);
				}

				foundVertexEdges->second[destVertex] = edgeIsClosed ? CLOSED_EDGE_FLOW : OPEN_EDGE_FLOW;

				if (!m_sourceGraph->hasEdge(destVertex, sourceVertex))
				{
					auto foundDestVertexEdges = edgeCapacities.find(destVertex);
					if (foundDestVertexEdges == edgeCapacities.end())
					{
						foundDestVertexEdges = edgeCapacities.insert(destVertex).first;
					}
					foundDestVertexEdges->second[sourceVertex] = 0;
				}
			}
		}
	}

	// Build flow graph edges flat list and lookup map
	m_flowGraphEdges.reserve(m_totalNumEdges);
	m_flowGraphLookup.reserve(m_sourceGraph->getNumVertices());
	for (int sourceVertex = 0; sourceVertex < m_sourceGraph->getNumVertices(); ++sourceVertex)
	{
		m_flowGraphLookup.emplace_back(m_flowGraphEdges.size(), m_flowGraphEdges.size() + edgeCapacities[sourceVertex].size());
		for (auto it = edgeCapacities[sourceVertex].begin(), itEnd = edgeCapacities[sourceVertex].end(); it != itEnd; ++it)
		{
			m_flowGraphEdges.push_back({it->first, -1, it->second});
		}
	}

	// Fill in the ReverseEdgeIndex for each vertex
	for (int sourceVertex = 0; sourceVertex < m_sourceGraph->getNumVertices(); ++sourceVertex)
	{
		for (int i = get<0>(m_flowGraphLookup[sourceVertex]); i < get<1>(m_flowGraphLookup[sourceVertex]); ++i)
		{
			if (m_flowGraphEdges[i].reverseEdgeIndex < 0)
			{
				int destVertex = m_flowGraphEdges[i].endVertex;
				bool foundReverse = false;
				for (int j = get<0>(m_flowGraphLookup[destVertex]); j < get<1>(m_flowGraphLookup[destVertex]); ++j)
				{
					if (m_flowGraphEdges[j].endVertex == sourceVertex)
					{
						m_flowGraphEdges[i].reverseEdgeIndex = j;
						vxy_assert(m_flowGraphEdges[j].reverseEdgeIndex < 0);
						m_flowGraphEdges[j].reverseEdgeIndex = i;

						foundReverse = true;
						break;
					}
				}
				vxy_assert(foundReverse);
			}
		}
	}

	// Register for callback when edges are added/removed from ExplanationGraph, in order to update capacities
	m_explanationGraph->getEdgeChangeListener().add([&](bool edgeWasAdded, int from, int to)
	{
		onExplanationGraphEdgeChange(edgeWasAdded, from, to);
	});

	// Create reachability structures for all variables that are possibly reachability sources
	for (int vertex = 0; vertex < m_sourceGraph->getNumVertices(); ++vertex)
	{
		VarID vertexVar = m_sourceGraphData->get(vertex);
		if (vertexVar.isValid() && possiblyIsSource(db, vertexVar))
		{
			addSource(db, vertexVar);
			m_initialPotentialSources.push_back(vertexVar);
		}
	}

	// Constrain all variables that are definitely reachable by any definite reachability source to reachable
	// Constrain all variables that are not reachable by all potential reachability sources to unreachable
	for (int vertex = 0; vertex < m_sourceGraph->getNumVertices(); ++vertex)
	{
		VarID vertexVar = m_sourceGraphData->get(vertex);
		if (vertexVar.isValid())
		{
			EReachabilityDetermination determination = determineReachability(db, vertex);

			if (determination == EReachabilityDetermination::DefinitelyUnreachable)
			{
				if (!db->constrainToValues(vertexVar, m_notReachableMask, this))
				{
					return false;
				}
			}
			else if (determination == EReachabilityDetermination::DefinitelyReachable)
			{
				if (!db->constrainToValues(vertexVar, m_requireReachableMask, this))
				{
					return false;
				}
			}
		}
	}

	return true;
}

void ITopologySearchConstraint::reset(IVariableDatabase* db)
{
	for (auto it = m_vertexWatchHandles.begin(), itEnd = m_vertexWatchHandles.end(); it != itEnd; ++it)
	{
		db->removeVariableWatch(it->first, it->second, this);
	}
	m_vertexWatchHandles.clear();

	for (auto it = m_edgeWatchHandles.begin(), itEnd = m_edgeWatchHandles.end(); it != itEnd; ++it)
	{
		db->removeVariableWatch(it->first, it->second, &m_edgeWatcher);
	}
	m_edgeWatchHandles.clear();
}

bool ITopologySearchConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& removeWatch)
{
	const ValueSet& newValue = db->getPotentialValues(variable);

	if ((prevValue.anyPossible(m_sourceMask) && !newValue.anyPossible(m_sourceMask)) ||
		(prevValue.anyPossible(m_notReachableMask) && !newValue.anyPossible(m_notReachableMask)))
	{
		if (!contains(m_vertexProcessList.begin(), m_vertexProcessList.end(), variable))
		{
			m_vertexProcessList.push_back(variable);
		}
		db->queueConstraintPropagation(this);
	}
	return true;
}

bool ITopologySearchConstraint::EdgeWatcher::onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& prevValue, bool& removeHandle)
{
	const ValueSet& newValue = db->getPotentialValues(var);
	if ((prevValue.anyPossible(m_parent.m_edgeBlockedMask) && !newValue.anyPossible(m_parent.m_edgeBlockedMask)) ||
		(prevValue.anyPossible(m_parent.m_edgeOpenMask) && !newValue.anyPossible(m_parent.m_edgeOpenMask)))
	{
		m_parent.m_edgeProcessList.push_back(var);
		db->queueConstraintPropagation(&m_parent);
	}
	return true;
}

bool ITopologySearchConstraint::propagate(IVariableDatabase* db)
{
	vxy_assert(!m_edgeChangeFailure);
	ValueGuard<bool> guardEdgeChangeFailure(m_edgeChangeFailure, false);

	// Process edges first, adding/removing edges from the Min/Max graph, respectively
	for (VarID edgeVar : m_edgeProcessList)
	{
		updateGraphsForEdgeChange(db, edgeVar);
		if (m_edgeChangeFailure)
		{
			return false;
		}
	}
	m_edgeProcessList.clear();

	#if REACHABILITY_USE_RAMAL_REPS
	// Batch-update reachability for all edge changes. This will trigger OnReachabilityChanged callbacks.
	{
		vxy_assert(!m_edgeChangeFailure);
		ValueGuard<bool> guardEdgeChange(m_inEdgeChange, true);
		ValueGuard<IVariableDatabase*> guardDb(m_edgeChangeDb, db);

		for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
		{
			it->second.maxReachability->refresh();
			if (m_edgeChangeFailure)
			{
				return false;
			}

			it->second.minReachability->refresh();
			if (m_edgeChangeFailure)
			{
				return false;
			}
		}
	}
	#endif

	vxy_assert(!m_edgeChangeFailure);

	// Now that reachability info is up to date, process vertices
	for (VarID vertexVar : m_vertexProcessList)
	{
		if (!processVertexVariableChange(db, vertexVar))
		{
			return false;
		}
	}
	m_vertexProcessList.clear();

	return true;
}

bool ITopologySearchConstraint::processVertexVariableChange(IVariableDatabase* db, VarID variable)
{
	if (!db->anyPossible(variable, m_sourceMask))
	{
		if (!removeSource(db, variable))
		{
			return false;
		}
	}

	// If this now requires reachability... 
	if (!db->anyPossible(variable, m_notReachableMask))
	{
		int vertex = m_variableToSourceVertexIndex[variable];

		int numReachableSources = 0;
		VarID lastReachableSource = VarID::INVALID;
		for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
		{
			if (it->second.maxReachability->isReachable(vertex) && isValidDistance(db, it->second.maxReachability->getDistance(vertex)))
			{
				++numReachableSources; 
				lastReachableSource = it->first;
				if (numReachableSources > 1)
				{
					break;
				}
			}
		}

		// If not reachable by any source, then fail
		if (numReachableSources == 0)
		{
			bool success = db->constrainToValues(variable, m_notReachableMask, this, [&](auto params) { return explainNoReachability(params); });
			vxy_assert(!success);
			return false;
		}
		// If reachable by a single potential source, that single source must be definite
		else if (numReachableSources == 1)
		{
			if (!db->constrainToValues(lastReachableSource, m_sourceMask, this, [&](auto params) { return explainRequiredSource(params); }))
			{
				return false;
			}
		}
	}

	return true;
}

void ITopologySearchConstraint::addSource(const IVariableDatabase* db, VarID source)
{
	int vertex = m_variableToSourceVertexIndex[source];

	#if REACHABILITY_USE_RAMAL_REPS
	auto minReachable = makeTopology(m_minGraph);
	auto maxReachable = makeTopology(m_maxGraph);
	#else
	auto minReachable = make_shared<ESTreeType>(m_minGraph);
	auto maxReachable = make_shared<ESTreeType>(m_maxGraph);
	#endif

	minReachable->initialize(vertex, &m_reachabilityEdgeLookup, m_totalNumEdges);
	maxReachable->initialize(vertex, &m_reachabilityEdgeLookup, m_totalNumEdges);

	EventListenerHandle minHandle = addMinCallback(*minReachable, db, source);
	EventListenerHandle maxHandle = addMaxCallback(*maxReachable, db, source);

	m_reachabilitySources[source] = { minReachable, maxReachable, minHandle, maxHandle };
	
}

bool ITopologySearchConstraint::removeSource(IVariableDatabase* db, VarID source)
{
	if (m_reachabilitySources.find(source) == m_reachabilitySources.end())
	{
		return true;
	}

	vxy_assert(m_backtrackData.empty() || m_backtrackData.back().level <= db->getDecisionLevel());
	if (m_backtrackData.empty() || m_backtrackData.back().level != db->getDecisionLevel())
	{
		m_backtrackData.push_back({db->getDecisionLevel()});
	}

	vxy_assert(m_backtrackData.back().level == db->getDecisionLevel());
	vxy_sanity(!contains(m_backtrackData.back().reachabilitySourcesRemoved.begin(), m_backtrackData.back().reachabilitySourcesRemoved.end(), source));
	m_backtrackData.back().reachabilitySourcesRemoved.push_back(source);

	ReachabilitySourceData sourceData = m_reachabilitySources[source];
	m_reachabilitySources.erase(source);

	sourceData.minReachability->onReachabilityChanged.remove(sourceData.minRamalHandle);
	sourceData.maxReachability->onReachabilityChanged.remove(sourceData.maxRamalHandle);

	int sourceVertex = m_variableToSourceVertexIndex[source];

	// Look through all vertices that were reachable from this old source. If any are now definitely unreachable,
	// mark them as such.
	bool failure = false;
	auto checkReachability = [&](int vertex, int parent)
	{
		if (sourceData.maxReachability->isReachable(vertex) && isValidDistance(db, sourceData.maxReachability->getDistance(vertex)))
		{
			// This vertex is no longer reachable from the removed source, so might be definitely unreachable now
			VarID vertexVar = m_sourceGraphData->get(vertex);
			if (vertexVar.isValid() && db->anyPossible(vertexVar, m_requireReachableMask))
			{
				EReachabilityDetermination determination = determineReachability(db, vertex);

				if (determination == EReachabilityDetermination::DefinitelyUnreachable)
				{
					//sanityCheckUnreachable(db, vertex);
					if (!db->constrainToValues(vertexVar, m_notReachableMask, this, [&](auto params) { return explainNoReachability(params); }))
					{
						failure = true;
						return ETopologySearchResponse::Abort;
					}
				}
				else if (determination == EReachabilityDetermination::PossiblyReachable && !db->anyPossible(vertexVar, m_notReachableMask))
				{
					// The vertex is marked definitely reachable, but only possibly reachable in the graph.
					// If there is only a single potential source that reaches this vertex, then it must now definitely be a source.
					VarID lastReachableSource = VarID::INVALID;
					int numReachableSources = 0;
					for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
					{
						if (it->second.maxReachability->isReachable(vertex) && isValidDistance(db, it->second.maxReachability->getDistance(vertex)))
						{
							++numReachableSources;
							lastReachableSource = it->first;
							if (numReachableSources > 1)
							{
								break;
							}
						}
					}

					vxy_assert(numReachableSources >= 1);
					if (numReachableSources == 1)
					{
						if (!db->constrainToValues(lastReachableSource, m_sourceMask, this, [&, source](auto params) { return explainRequiredSource(params, source); }))
						{
							failure = true;
							return ETopologySearchResponse::Abort;
						}
					}
				}
			}
			return ETopologySearchResponse::Continue;
		}
		else
		{
			return ETopologySearchResponse::Skip;
		}
	};
	//source was removed. find nodes that relied on this source, check to see if they are reachable by other sources
	m_dfs.search(*m_sourceGraph.get(), sourceVertex, checkReachability);

	return !failure;
}

void ITopologySearchConstraint::updateGraphsForEdgeChange(IVariableDatabase* db, VarID variable)
{
	vxy_assert(!m_inEdgeChange);
	vxy_assert(!m_edgeChangeFailure);
	vxy_assert(m_edgeChangeDb == nullptr);

	ValueGuard<bool> guardEdgeChange(m_inEdgeChange, true);
	ValueGuard<IVariableDatabase*> guardDb(m_edgeChangeDb, db);

	int nodeIndex = m_variableToSourceEdgeIndex[variable];

	//
	// If an edge becomes definitely unblocked, add it to the min graph.
	// If an edge becomes definitely blocked, remove it from the max graph.
	//
	// Sources listen to edge changes and call OnReachabilityChanged for any nodes that become (un)reachable from
	// that source. The variables will attempt to be constrained based on their (un)reachability; if they cannot,
	// then the bEdgeChangeFailure flag is set.
	//

	if (db->anyPossible(variable, m_edgeOpenMask) && !db->anyPossible(variable, m_edgeBlockedMask))
	{
		int from, to;
		bool bidirectional;
		m_edgeGraph->getSourceEdgeForVertex(nodeIndex, from, to, bidirectional);
		if (!m_minGraph->hasEdge(from, to))
		{
			m_minGraph->addEdge(from, to, db->getTimestamp());
			if (bidirectional)
			{
				m_minGraph->addEdge(to, from, db->getTimestamp());
			}
		}
	}
	else if (db->anyPossible(variable, m_edgeBlockedMask) && !db->anyPossible(variable, m_edgeOpenMask))
	{
		int from, to;
		bool bidirectional;
		m_edgeGraph->getSourceEdgeForVertex(nodeIndex, from, to, bidirectional);

		if (m_maxGraph->hasEdge(from, to))
		{
			// Remove from the explanation graph first, so that we can sync to correct time.
			m_explanationGraph->removeEdge(from, to, db->getTimestamp());
			if (bidirectional)
			{
				m_explanationGraph->removeEdge(to, from, db->getTimestamp());
			}

			m_maxGraph->removeEdge(from, to, db->getTimestamp());
			if (bidirectional)
			{
				m_maxGraph->removeEdge(to, from, db->getTimestamp());
			}
		}
	}
}

//determine if it's still within the required range
void ITopologySearchConstraint::onReachabilityChanged(int vertexIndex, VarID sourceVar, bool inMinGraph)
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

	if (inMinGraph)
	{
		// See if this vertex is definitely reachable by any source now
		if (determineReachability(m_edgeChangeDb, vertexIndex) == EReachabilityDetermination::DefinitelyReachable)
		{
			VarID var = m_sourceGraphData->get(vertexIndex);
			if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_requireReachableMask, this))
			{
				m_edgeChangeFailure = true;
			}
		}
	}
	else
	{
		// vertexIndex became unreachable in the max graph
		if (determineReachability(m_edgeChangeDb, vertexIndex) == EReachabilityDetermination::DefinitelyUnreachable)
		{
			VarID var = m_sourceGraphData->get(vertexIndex);
			//sanityCheckUnreachable(m_edgeChangeDb, vertexIndex);

			if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_notReachableMask, this, [&](auto params) { return explainNoReachability(params); }))
			{
				m_edgeChangeFailure = true;
			}
		}
	}
}

void ITopologySearchConstraint::backtrack(const IVariableDatabase* db, SolverDecisionLevel level)
{
	vxy_assert(!m_edgeChangeFailure);
	m_edgeProcessList.clear();
	m_vertexProcessList.clear();

	ValueGuard<bool> backtrackGuard(m_backtracking, true);

	while (!m_backtrackData.empty() && m_backtrackData.back().level > level)
	{
		for (VarID sourceVar : m_backtrackData.back().reachabilitySourcesRemoved)
		{
			addSource(db, sourceVar);
		}
		m_backtrackData.pop_back();
	}

	// Backtrack any edges added/removed after this point
	m_minGraph->backtrackUntil(db->getTimestamp());
	m_maxGraph->backtrackUntil(db->getTimestamp());
	m_explanationGraph->backtrackUntil(db->getTimestamp());

	#if REACHABILITY_USE_RAMAL_REPS
	// Batch-update reachability for all edge changes
	{
		for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
		{
			it->second.maxReachability->refresh(); 
			it->second.minReachability->refresh();
		}
		vxy_assert(!m_edgeChangeFailure);
	}
	#endif
}

ITopologySearchConstraint::EReachabilityDetermination ITopologySearchConstraint::determineReachability(const IVariableDatabase* db, int vertexIndex)
{
	VarID vertexVar = m_sourceGraphData->get(vertexIndex);
	for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
	{
		if (it->first == vertexVar)
		{
			// Don't treat reachability as reflective. If a vertex is marked both needing reachability and is a
			// reachability source, it needs to be reachable from a DIFFERENT source.
			continue;
		}

		if (it->second.minReachability->isReachable(vertexIndex) && isValidDistance(db, it->second.minReachability->getDistance(vertexIndex)))
		{
			if (definitelyIsSource(db, it->first))
			{
				return EReachabilityDetermination::DefinitelyReachable;
			}
			else
			{
				return EReachabilityDetermination::PossiblyReachable;
			}
		}
		else if (it->second.maxReachability->isReachable(vertexIndex) && isValidDistance(db, it->second.maxReachability->getDistance(vertexIndex)))
		{
			return EReachabilityDetermination::PossiblyReachable;
		}
	}

	return EReachabilityDetermination::DefinitelyUnreachable;
}

// Called whenever an edge is added or removed from the explanation graph, including during backtracking
void ITopologySearchConstraint::onExplanationGraphEdgeChange(bool edgeWasAdded, int from, int to)
{
	// Keep the edge capacities in sync.
	for (int i = get<0>(m_flowGraphLookup[from]); i < get<1>(m_flowGraphLookup[from]); ++i)
	{
		if (m_flowGraphEdges[i].endVertex == to)
		{
			m_flowGraphEdges[i].capacity = edgeWasAdded ? OPEN_EDGE_FLOW : CLOSED_EDGE_FLOW;
			return;
		}
	}
	vxy_fail();
}

vector<Literal> ITopologySearchConstraint::explainNoReachability(const NarrowingExplanationParams& params) const
{
	 return IVariableDatabase::defaultExplainer(params);
}

vector<Literal> ITopologySearchConstraint::explainRequiredSource(const NarrowingExplanationParams& params, VarID removedSource)
{
	return IVariableDatabase::defaultExplainer(params);
}


void ITopologySearchConstraint::sanityCheckUnreachable(IVariableDatabase* db, int vertexIndex)
{
	#if SANITY_CHECKS
	// For each source that could possibly exist...
	for (VarID potentialSource : m_initialPotentialSources)
	{
		int sourceVertex = m_variableToSourceVertexIndex[potentialSource];
		// If this is currently a potential source...
		if (db->getPotentialValues(potentialSource).anyPossible(m_sourceMask))
		{
			vxy_assert(!TopologySearchAlgorithm::canReach(m_maxGraph, sourceVertex, vertexIndex));
		}
	}
	#endif
}


vector<VarID> ITopologySearchConstraint::getConstrainingVariables() const
{
	vector<VarID> out;
	for (int i = 0; i < m_sourceGraph->getNumVertices(); ++i)
	{
		VarID var = m_sourceGraphData->get(i);
		if (var.isValid())
		{
			out.push_back(var);
		}
	}

	for (int i = 0; i < m_edgeGraph->getNumVertices(); ++i)
	{
		VarID var = m_edgeGraphData->get(i);
		if (var.isValid())
		{
			out.push_back(var);
		}
	}

	return out;
}

bool ITopologySearchConstraint::checkConflicting(IVariableDatabase* db) const
{
	// TODO
	return false;
}

#undef SANITY_CHECKS
