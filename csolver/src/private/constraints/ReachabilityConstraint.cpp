// Copyright Proletariat, Inc. All Rights Reserved.
#include "constraints/ReachabilityConstraint.h"

#include "variable/IVariableDatabase.h"
#include "topology/GraphRelations.h"

using namespace csolver;

#define SANITY_CHECKS CS_SANITY_CHECKS

static constexpr int OPEN_EDGE_FLOW = INT_MAX >> 1;
static constexpr int CLOSED_EDGE_FLOW = 1;
static constexpr bool USE_RAMAL_REPS_BATCHING = true;

ReachabilityConstraint* ReachabilityConstraint::ReachabilityFactory::construct(
	const ConstraintFactoryParams& params,
	const shared_ptr<TTopologyVertexData<VarID>>& nodeData,
	const vector<int>& sourceValues,
	const vector<int>& needReachableValues,
	const shared_ptr<TTopologyVertexData<VarID>>& edgeData,
	const vector<int>& edgeBlockedValues)
{
	// Get an example graph variable
	VarID graphVar;
	for (int i = 0; i < nodeData->getSource()->getNumNodes(); ++i)
	{
		if (nodeData->get(i).isValid())
		{
			graphVar = nodeData->get(i);
			break;
		}
	}
	cs_assert(graphVar.isValid());

	// Get an example edge variable
	VarID edgeVar;
	for (int i = 0; i < edgeData->getSource()->getNumNodes(); ++i)
	{
		if (edgeData->get(i).isValid())
		{
			edgeVar = edgeData->get(i);
			break;
		}
	}
	cs_assert(edgeVar.isValid());

	ValueSet sourceMask = params.valuesToInternal(graphVar, sourceValues);
	ValueSet needReachableMask = params.valuesToInternal(graphVar, needReachableValues);
	ValueSet edgeBlockedMask = params.valuesToInternal(edgeVar, edgeBlockedValues);

	return new ReachabilityConstraint(params, nodeData, sourceMask, needReachableMask, edgeData, edgeBlockedMask);
}

ReachabilityConstraint::ReachabilityConstraint(
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

	for (int i = 0; i < m_sourceGraph->getNumNodes(); ++i)
	{
		VarID var = sourceGraphData->get(i);
		if (var.isValid())
		{
			m_variableToSourceNodeIndex[var] = i;
		}
	}

	cs_assert(m_edgeGraph->getSource() == m_sourceGraph);
	for (int i = 0; i < m_edgeGraph->getNumNodes(); ++i)
	{
		VarID var = edgeGraphData->get(i);
		if (var.isValid())
		{
			m_variableToSourceEdgeIndex[var] = i;
		}
	}
}

bool ReachabilityConstraint::getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outRelations) const
{
	// First search through all provided literals to find the minimum graph node.
	// Note: some nodes will be edges, some will be tiles.
	int minGraphNode = m_sourceGraph->getNumNodes();

	vector<int> nodes;
	vector<bool> isEdgeNode;

	nodes.reserve(literals.size());
	isEdgeNode.reserve(literals.size());

	for (auto& lit : literals)
	{
		int graphNode = m_sourceGraphData->indexOf(lit.variable);
		if (graphNode < 0)
		{
			int edgeNode = m_edgeGraphData->indexOf(lit.variable);
			cs_assert(edgeNode >= 0);

			nodes.push_back(edgeNode);
			isEdgeNode.push_back(true);

			int edgeFrom, edgeTo;
			bool bidirectional;
			m_edgeGraph->getSourceEdgeForNode(edgeNode, edgeFrom, edgeTo, bidirectional);

			graphNode = min(edgeFrom, edgeTo);
		}
		else
		{
			nodes.push_back(graphNode);
			isEdgeNode.push_back(false);
		}
		cs_assert(graphNode >= 0);
		minGraphNode = min(graphNode, minGraphNode);
	}

	// We always provide relations in terms of the source graph.
	// Relations are anchored to the minimum node ID found (maps to top-leftmost in a grid graph).
	outRelations.reset(m_sourceGraph, minGraphNode);

	// create the relations!
	outRelations.reserve(literals.size());
	for (int i = 0; i != literals.size(); ++i)
	{
		int node = nodes[i];
		bool isEdge = isEdgeNode[i];

		if (!isEdge)
		{
			if (node != minGraphNode)
			{
				TopologyLink link;
				if (!m_sourceGraph->getTopologyLink(minGraphNode, node, link))
				{
					// no path specified in graph
					// TODO: assert?
					outRelations.clear();
					return false;
				}

				auto linkRel = make_shared<TTopologyLinkGraphRelation<VarID>>(m_sourceGraphData, link);
				outRelations.addRelation(m_sourceGraphData->get(node), linkRel);
			}
			else
			{
				auto selfRel = make_shared<TVertexToDataGraphRelation<VarID>>(m_sourceGraphData);
				outRelations.addRelation(m_sourceGraphData->get(node), selfRel);
			}
		}
		else
		{
			// edge variable: get the source node if the edge
			int edgeOrigin, edgeDestination;
			bool bidirectional;
			m_edgeGraph->getSourceEdgeForNode(node, edgeOrigin, edgeDestination, bidirectional);

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
				cs_assert_msg(false, "Edge node %d has source graph node origin %d, but can't find edgeIndex in source graph!", node, edgeOrigin);
				outRelations.clear();
				return false;
			}

			auto nodeToEdgeNodeRel = make_shared<TVertexEdgeToEdgeGraphVertexGraphRelation<ITopology>>(m_sourceGraph, m_edgeGraph, nodeEdgeIndex);
			auto nodeToEdgeVarRel = nodeToEdgeNodeRel->map(make_shared<TVertexToDataGraphRelation<VarID>>(m_edgeGraphData));

			if (edgeOrigin != minGraphNode)
			{
				TopologyLink link;
				if (!m_sourceGraph->getTopologyLink(minGraphNode, edgeOrigin, link))
				{
					cs_assert_msg(false, "expected link between nodes %d -> %d", minGraphNode, edgeOrigin);
					outRelations.clear();
					return false;
				}

				auto linkRel = make_shared<TopologyLinkIndexGraphRelation>(m_sourceGraph, link);
				outRelations.addRelation(m_edgeGraphData->get(node), linkRel->map(nodeToEdgeVarRel));
			}
			else
			{
				outRelations.addRelation(m_edgeGraphData->get(node), nodeToEdgeVarRel);
			}
		}
	}

	cs_assert(outRelations.relations.size() == literals.size());
	return true;
}

bool ReachabilityConstraint::initialize(IVariableDatabase* db)
{
	// Add all nodes to min/max graphs
	for (int nodeIndex = 0; nodeIndex < m_sourceGraph->getNumNodes(); ++nodeIndex)
	{
		VarID nodeVar = m_sourceGraphData->get(nodeIndex);

		int addedIdx = m_maxGraph->addNode();
		cs_assert(addedIdx == nodeIndex);

		addedIdx = m_minGraph->addNode();
		cs_assert(addedIdx == nodeIndex);

		addedIdx = m_explanationGraph->addNode();
		cs_assert(addedIdx == nodeIndex);

		if (nodeVar.isValid())
		{
			m_nodeWatchHandles[nodeVar] = db->addVariableWatch(nodeVar, EVariableWatchType::WatchModification, this);
		}
	}

	hash_map<int, hash_map<int, int>> edgeCapacities;

	m_totalNumEdges = 0;

	// Add all definitely-open edges to the min graph, and all possibly-open edges to the max graph
	m_reachabilityEdgeLookup.resize(m_sourceGraph->getNumNodes());
	for (int sourceNode = 0; sourceNode < m_sourceGraph->getNumNodes(); ++sourceNode)
	{
		auto foundNodeEdges = edgeCapacities.find(sourceNode);
		if (foundNodeEdges == edgeCapacities.end())
		{
			foundNodeEdges = edgeCapacities.insert(sourceNode).first;
		}

		m_reachabilityEdgeLookup[sourceNode].reserve(m_sourceGraph->getNumOutgoing(sourceNode));
		for (int edgeIndex = 0; edgeIndex < m_sourceGraph->getNumOutgoing(sourceNode); ++edgeIndex)
		{
			int destNode;
			if (m_sourceGraph->getOutgoingDestination(sourceNode, edgeIndex, destNode))
			{
				m_reachabilityEdgeLookup[sourceNode].push_back(make_tuple(destNode, m_totalNumEdges));
				++m_totalNumEdges;

				int edgeNode = m_edgeGraph->getNodeForSourceEdge(sourceNode, destNode);
				cs_assert(edgeNode >= 0);
				VarID edgeVar = m_edgeGraphData->get(edgeNode);

				bool edgeIsClosed = true;
				if (edgeVar.isValid())
				{
					if (definitelyOpenEdge(db, edgeVar))
					{
						edgeIsClosed = false;

						m_minGraph->initEdge(sourceNode, destNode);
						m_maxGraph->initEdge(sourceNode, destNode);
						m_explanationGraph->initEdge(sourceNode, destNode);
					}
					else if (possiblyOpenEdge(db, edgeVar))
					{
						edgeIsClosed = false;

						if (m_edgeWatchHandles.find(edgeVar) == m_edgeWatchHandles.end())
						{
							m_edgeWatchHandles[edgeVar] = db->addVariableWatch(edgeVar, EVariableWatchType::WatchModification, &m_edgeWatcher);
						}
						m_maxGraph->initEdge(sourceNode, destNode);
						m_explanationGraph->initEdge(sourceNode, destNode);
					}
				}
				else
				{
					edgeIsClosed = false;

					// No variable for this edge, so should always exist
					m_minGraph->initEdge(sourceNode, destNode);
					m_maxGraph->initEdge(sourceNode, destNode);
					m_explanationGraph->initEdge(sourceNode, destNode);
				}

				foundNodeEdges->second[destNode] = edgeIsClosed ? CLOSED_EDGE_FLOW : OPEN_EDGE_FLOW;

				if (!m_sourceGraph->hasEdge(destNode, sourceNode))
				{
					auto foundDestNodeEdges = edgeCapacities.find(destNode);
					if (foundDestNodeEdges == edgeCapacities.end())
					{
						foundDestNodeEdges = edgeCapacities.insert(destNode).first;
					}
					foundDestNodeEdges->second[sourceNode] = 0;
				}
			}
		}
	}

	// Build flow graph edges flat list and lookup map
	m_flowGraphEdges.reserve(m_totalNumEdges);
	m_flowGraphLookup.reserve(m_sourceGraph->getNumNodes());
	for (int sourceNode = 0; sourceNode < m_sourceGraph->getNumNodes(); ++sourceNode)
	{
		m_flowGraphLookup.emplace_back(m_flowGraphEdges.size(), m_flowGraphEdges.size() + edgeCapacities[sourceNode].size());
		for (auto it = edgeCapacities[sourceNode].begin(), itEnd = edgeCapacities[sourceNode].end(); it != itEnd; ++it)
		{
			m_flowGraphEdges.push_back({it->first, -1, it->second});
		}
	}

	// Fill in the ReverseEdgeIndex for each node
	for (int sourceNode = 0; sourceNode < m_sourceGraph->getNumNodes(); ++sourceNode)
	{
		for (int i = get<0>(m_flowGraphLookup[sourceNode]); i < get<1>(m_flowGraphLookup[sourceNode]); ++i)
		{
			if (m_flowGraphEdges[i].reverseEdgeIndex < 0)
			{
				int destNode = m_flowGraphEdges[i].endNode;
				bool foundReverse = false;
				for (int j = get<0>(m_flowGraphLookup[destNode]); j < get<1>(m_flowGraphLookup[destNode]); ++j)
				{
					if (m_flowGraphEdges[j].endNode == sourceNode)
					{
						m_flowGraphEdges[i].reverseEdgeIndex = j;
						cs_assert(m_flowGraphEdges[j].reverseEdgeIndex < 0);
						m_flowGraphEdges[j].reverseEdgeIndex = i;

						foundReverse = true;
						break;
					}
				}
				cs_assert(foundReverse);
			}
		}
	}

	// Register for callback when edges are added/removed from ExplanationGraph, in order to update capacities
	m_explanationGraph->getEdgeChangeListener().add([&](bool edgeWasAdded, int from, int to)
	{
		onExplanationGraphEdgeChange(edgeWasAdded, from, to);
	});

	// Create reachability structures for all variables that are possibly reachability sources
	for (int nodeIndex = 0; nodeIndex < m_sourceGraph->getNumNodes(); ++nodeIndex)
	{
		VarID nodeVar = m_sourceGraphData->get(nodeIndex);
		if (nodeVar.isValid() && possiblyIsSource(db, nodeVar))
		{
			addSource(nodeVar);
			m_initialPotentialSources.push_back(nodeVar);
		}
	}

	// Constrain all variables that are definitely reachable by any definite reachability source to reachable
	// Constrain all variables that are not reachable by all potential reachability sources to unreachable
	for (int nodeIndex = 0; nodeIndex < m_sourceGraph->getNumNodes(); ++nodeIndex)
	{
		VarID nodeVar = m_sourceGraphData->get(nodeIndex);
		if (nodeVar.isValid())
		{
			EReachabilityDetermination determination = determineReachability(db, nodeIndex);

			if (determination == EReachabilityDetermination::DefinitelyUnreachable)
			{
				if (!db->constrainToValues(nodeVar, m_notReachableMask, this))
				{
					return false;
				}
			}
			else if (determination == EReachabilityDetermination::DefinitelyReachable)
			{
				if (!db->constrainToValues(nodeVar, m_requireReachableMask, this))
				{
					return false;
				}
			}
		}
	}

	return true;
}

void ReachabilityConstraint::reset(IVariableDatabase* db)
{
	for (auto it = m_nodeWatchHandles.begin(), itEnd = m_nodeWatchHandles.end(); it != itEnd; ++it)
	{
		db->removeVariableWatch(it->first, it->second, this);
	}
	m_nodeWatchHandles.clear();

	for (auto it = m_edgeWatchHandles.begin(), itEnd = m_edgeWatchHandles.end(); it != itEnd; ++it)
	{
		db->removeVariableWatch(it->first, it->second, &m_edgeWatcher);
	}
	m_edgeWatchHandles.clear();
}

bool ReachabilityConstraint::onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& prevValue, bool& removeWatch)
{
	const ValueSet& newValue = db->getPotentialValues(variable);

	if ((prevValue.anyPossible(m_sourceMask) && !newValue.anyPossible(m_sourceMask)) ||
		(prevValue.anyPossible(m_notReachableMask) && !newValue.anyPossible(m_notReachableMask)))
	{
		if (!contains(m_nodeProcessList.begin(), m_nodeProcessList.end(), variable))
		{
			m_nodeProcessList.push_back(variable);
		}
		db->queueConstraintPropagation(this);
	}
	return true;
}

bool ReachabilityConstraint::EdgeWatcher::onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& prevValue, bool& removeHandle)
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

bool ReachabilityConstraint::propagate(IVariableDatabase* db)
{
	cs_assert(!m_edgeChangeFailure);
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
		cs_assert(!m_edgeChangeFailure);
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

	cs_assert(!m_edgeChangeFailure);

	// Now that reachability info is up to date, process nodes
	for (VarID nodeVar : m_nodeProcessList)
	{
		if (!processNodeVariableChange(db, nodeVar))
		{
			return false;
		}
	}
	m_nodeProcessList.clear();

	return true;
}

bool ReachabilityConstraint::processNodeVariableChange(IVariableDatabase* db, VarID variable)
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
		int nodeIndex = m_variableToSourceNodeIndex[variable];

		int numReachableSources = 0;
		VarID lastReachableSource = VarID::INVALID;
		for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
		{
			if (it->second.maxReachability->isReachable(nodeIndex))
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
			cs_assert(!success);
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

void ReachabilityConstraint::addSource(VarID source)
{
	int nodeIndex = m_variableToSourceNodeIndex[source];

	#if REACHABILITY_USE_RAMAL_REPS
	auto minReachable = make_shared<RamalRepsType>(m_minGraph, USE_RAMAL_REPS_BATCHING, true, false);
	auto maxReachable = make_shared<RamalRepsType>(m_maxGraph, USE_RAMAL_REPS_BATCHING, true, false);
	#else
	auto minReachable = make_shared<ESTreeType>(m_minGraph);
	auto maxReachable = make_shared<ESTreeType>(m_maxGraph);
	#endif

	minReachable->initialize(nodeIndex, &m_reachabilityEdgeLookup, m_totalNumEdges);
	maxReachable->initialize(nodeIndex, &m_reachabilityEdgeLookup, m_totalNumEdges);

	// Listen for when reachability changes on the conservative/optimistic graphs
	EventListenerHandle minDelHandle = minReachable->onReachabilityChanged.add([this, source](int changedNode, bool isReachable)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			cs_assert(isReachable);
			onReachabilityChanged(changedNode, source, true);
		}
	});

	EventListenerHandle maxDelHandle = maxReachable->onReachabilityChanged.add([this, source](int changedNode, bool isReachable)
	{
		if (!m_backtracking && !m_explainingSourceRequirement)
		{
			cs_assert(!isReachable);
			onReachabilityChanged(changedNode, source, false);
		}
	});

	m_reachabilitySources[source] = {minReachable, maxReachable, minDelHandle, maxDelHandle};
}

bool ReachabilityConstraint::removeSource(IVariableDatabase* db, VarID source)
{
	if (m_reachabilitySources.find(source) == m_reachabilitySources.end())
	{
		return true;
	}

	cs_assert(m_backtrackData.empty() || m_backtrackData.back().level <= db->getDecisionLevel());
	if (m_backtrackData.empty() || m_backtrackData.back().level != db->getDecisionLevel())
	{
		m_backtrackData.push_back({db->getDecisionLevel()});
	}

	cs_assert(m_backtrackData.back().level == db->getDecisionLevel());
	cs_sanity(!contains(m_backtrackData.back().reachabilitySourcesRemoved.begin(), m_backtrackData.back().reachabilitySourcesRemoved.end(), source));
	m_backtrackData.back().reachabilitySourcesRemoved.push_back(source);

	ReachabilitySourceData sourceData = m_reachabilitySources[source];
	m_reachabilitySources.erase(source);

	sourceData.minReachability->onReachabilityChanged.remove(sourceData.minReachabilityChangedHandle);
	sourceData.maxReachability->onReachabilityChanged.remove(sourceData.maxReachabilityChangedHandle);

	int sourceNode = m_variableToSourceNodeIndex[source];

	// Look through all nodes that were reachable from this old source. If any are now definitely unreachable,
	// mark them as such.
	bool failure = false;
	auto checkReachability = [&](int node, int parent)
	{
		if (sourceData.maxReachability->isReachable(node))
		{
			// This node is no longer reachable from the removed source, so might be definitely unreachable now
			VarID nodeVar = m_sourceGraphData->get(node);
			if (nodeVar.isValid() && db->anyPossible(nodeVar, m_requireReachableMask))
			{
				EReachabilityDetermination determination = determineReachability(db, node);

				if (determination == EReachabilityDetermination::DefinitelyUnreachable)
				{
					sanityCheckUnreachable(db, node);
					if (!db->constrainToValues(nodeVar, m_notReachableMask, this, [&](auto params) { return explainNoReachability(params); }))
					{
						failure = true;
						return ETopologySearchResponse::Abort;
					}
				}
				else if (determination == EReachabilityDetermination::PossiblyReachable && !db->anyPossible(nodeVar, m_notReachableMask))
				{
					// The node is marked definitely reachable, but only possibly reachable in the graph.
					// If there is only a single potential source that reaches this node, then it must now definitely be a source.
					VarID lastReachableSource = VarID::INVALID;
					int numReachableSources = 0;
					for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
					{
						if (it->second.maxReachability->isReachable(node))
						{
							++numReachableSources;
							lastReachableSource = it->first;
							if (numReachableSources > 1)
							{
								break;
							}
						}
					}

					cs_assert(numReachableSources >= 1);
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
	m_dfs.search(*m_sourceGraph.get(), sourceNode, checkReachability);

	return !failure;
}

void ReachabilityConstraint::updateGraphsForEdgeChange(IVariableDatabase* db, VarID variable)
{
	cs_assert(!m_inEdgeChange);
	cs_assert(!m_edgeChangeFailure);
	cs_assert(m_edgeChangeDb == nullptr);

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
		m_edgeGraph->getSourceEdgeForNode(nodeIndex, from, to, bidirectional);
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
		m_edgeGraph->getSourceEdgeForNode(nodeIndex, from, to, bidirectional);

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

void ReachabilityConstraint::onReachabilityChanged(int nodeIndex, VarID sourceVar, bool inMinGraph)
{
	cs_assert(!m_backtracking);
	cs_assert(!m_explainingSourceRequirement);

	cs_assert(m_edgeChangeDb != nullptr);
	cs_assert(m_inEdgeChange);

	if (m_edgeChangeFailure)
	{
		// We already failed - avoid further failures that could confuse the conflict analyzer
		return;
	}

	if (inMinGraph)
	{
		// See if this node is definitely reachable by any source now
		if (determineReachability(m_edgeChangeDb, nodeIndex) == EReachabilityDetermination::DefinitelyReachable)
		{
			VarID var = m_sourceGraphData->get(nodeIndex);
			if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_requireReachableMask, this))
			{
				m_edgeChangeFailure = true;
			}
		}
	}
	else
	{
		// NodeIndex became unreachable in the max graph
		if (determineReachability(m_edgeChangeDb, nodeIndex) == EReachabilityDetermination::DefinitelyUnreachable)
		{
			VarID var = m_sourceGraphData->get(nodeIndex);
			sanityCheckUnreachable(m_edgeChangeDb, nodeIndex);

			if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_notReachableMask, this, [&](auto params) { return explainNoReachability(params); }))
			{
				m_edgeChangeFailure = true;
			}
		}
	}
}

void ReachabilityConstraint::backtrack(const IVariableDatabase* db, SolverDecisionLevel level)
{
	cs_assert(!m_edgeChangeFailure);
	m_edgeProcessList.clear();
	m_nodeProcessList.clear();

	ValueGuard<bool> backtrackGuard(m_backtracking, true);

	while (!m_backtrackData.empty() && m_backtrackData.back().level > level)
	{
		for (VarID sourceVar : m_backtrackData.back().reachabilitySourcesRemoved)
		{
			addSource(sourceVar);
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
		cs_assert(!m_edgeChangeFailure);
	}
	#endif
}

ReachabilityConstraint::EReachabilityDetermination ReachabilityConstraint::determineReachability(const IVariableDatabase* db, int nodeIndex)
{
	VarID nodeVar = m_sourceGraphData->get(nodeIndex);
	for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
	{
		if (it->first == nodeVar)
		{
			// Don't treat reachability as reflective. If a node is marked both needing reachability and is a
			// reachability source, it needs to be reachable from a DIFFERENT source.
			continue;
		}

		if (it->second.minReachability->isReachable(nodeIndex))
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
		else if (it->second.maxReachability->isReachable(nodeIndex))
		{
			return EReachabilityDetermination::PossiblyReachable;
		}
	}

	return EReachabilityDetermination::DefinitelyUnreachable;
}

// Called whenever an edge is added or removed from the explanation graph, including during backtracking
void ReachabilityConstraint::onExplanationGraphEdgeChange(bool edgeWasAdded, int from, int to)
{
	// Keep the edge capacities in sync.
	for (int i = get<0>(m_flowGraphLookup[from]); i < get<1>(m_flowGraphLookup[from]); ++i)
	{
		if (m_flowGraphEdges[i].endNode == to)
		{
			m_flowGraphEdges[i].capacity = edgeWasAdded ? OPEN_EDGE_FLOW : CLOSED_EDGE_FLOW;
			return;
		}
	}
	cs_fail();
}

vector<Literal> ReachabilityConstraint::explainNoReachability(const NarrowingExplanationParams& params) const
{
	// return FSolverVariableDatabase::DefaultExplainer(Params);

	cs_assert_msg(m_variableToSourceNodeIndex.find(params.propagatedVariable) != m_variableToSourceNodeIndex.end(), "Not a node variable?");

	auto db = params.database;
	int conflNode = m_variableToSourceNodeIndex.find(params.propagatedVariable)->second;

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

		int sourceNode = m_variableToSourceNodeIndex.find(potentialSource)->second;
		if (sourceNode == conflNode)
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

			cs_sanity(!TopologySearchAlgorithm::canReach(m_explanationGraph, sourceNode, conflNode));

			// Find the minimum cut in the maximum flow graph. This will correspond to edges that are disabled, because
			// A) we know that SourceNode can't reach ConflNode without going through a disabled edge
			// B) blocked edges are set to a flow capacity of 1, and blocked edges have infinite flow
			vector<tuple<int, int>> cutEdges;
			m_maxFlowAlgo.getMaxFlow(*m_sourceGraph.get(), sourceNode, conflNode, m_flowGraphEdges, m_flowGraphLookup, &cutEdges);
			cs_assert(!cutEdges.empty());

			// Now that we've found the cut, bring the explanation graph back to current state.
			m_explanationGraph->fastForward();

			for (tuple<int, int>& edge : cutEdges)
			{
				int edgeNode = m_edgeGraph->getNodeForSourceEdge(get<0>(edge), get<1>(edge));
				VarID edgeVar = m_edgeGraphData->get(edgeNode);
				if (edgeVarsRecorded.find(edgeVar) == edgeVarsRecorded.end())
				{
					edgeVarsRecorded.insert(edgeVar);

					cs_assert(!db->anyPossible(edgeVar, m_edgeOpenMask));
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

				int node = m_variableToSourceNodeIndex.find(potentialSource)->second;
				if (node == conflNode)
				{
					continue;
				}

				if (db->getPotentialValues(m_initialPotentialSources[j]).anyPossible(m_sourceMask))
				{
					if (!m_maxFlowAlgo.onSinkSide(node, m_flowGraphEdges, m_flowGraphLookup))
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
	cs_assert(!m_explainingSourceRequirement);
	ValueGuard<bool> guard(m_explainingSourceRequirement, true);

	VarID sourceVar = params.propagatedVariable;
	int sourceNode = m_variableToSourceNodeIndex[sourceVar];

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
		if (db->anyPossible(potentialSource, m_sourceMask) && m_reachabilitySources.find(potentialSource) == m_reachabilitySources.end())
		{
			tempSources.push_back(potentialSource);

			int nodeIndex = m_variableToSourceNodeIndex[potentialSource];

			ReachabilitySourceData data;
			#if REACHABILITY_USE_RAMAL_REPS
			data.maxReachability = make_shared<RamalRepsType>(m_maxGraph, false, false, false);
			#else
			Data.maxReachability = make_shared<ESTreeType>(MaxGraph);
			#endif
			data.maxReachability->initialize(nodeIndex, &m_reachabilityEdgeLookup, m_totalNumEdges);

			m_reachabilitySources[potentialSource] = data;
		}
	}

	int removedSourceLitIdx = -1;
	if (removedSource.isValid())
	{
		cs_assert(m_reachabilitySources.find(removedSource) == m_reachabilitySources.end());

		// This became a required source because RemovedSource was removed, and some definitely-reachable nodes were
		// only reachable by this source.

		cs_assert(!db->anyPossible(removedSource, m_sourceMask));
		removedSourceLitIdx = lits.size();
		lits.push_back(Literal(removedSource, m_sourceMask));
	}

	//
	// This became a required source because some variable(s) were marked as required, and we are the only
	// source that can reach them. Find those variables.
	//
	bool foundSupports = false;
	auto& ourReachability = m_reachabilitySources[sourceVar].maxReachability;
	auto searchCallback = [&](int node, int parent, int edgeIndex)
	{
		if (ourReachability->isReachable(node))
		{
			VarID nodeVar = m_sourceGraphData->get(node);
			if (!db->anyPossible(nodeVar, m_notReachableMask))
			{
				bool reachableFromAnotherSource = false;
				for (auto it = m_reachabilitySources.begin(), itEnd = m_reachabilitySources.end(); it != itEnd; ++it)
				{
					if (it->first != sourceVar && it->first != nodeVar && it->second.maxReachability->isReachable(node))
					{
						reachableFromAnotherSource = true;
						break;
					}
				}

				if (!reachableFromAnotherSource)
				{
					cs_assert(m_reachabilitySources[sourceVar].maxReachability->isReachable(node));
					if (nodeVar == removedSource) // make sure we don't add the same literal twice!
					{
						cs_assert(lits[removedSourceLitIdx].variable == nodeVar);
						lits[removedSourceLitIdx].values.include(m_notReachableMask);
					}
					else
					{
						lits.push_back(Literal(nodeVar, m_notReachableMask));
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
	m_dfs.search(*m_sourceGraph.get(), m_variableToSourceNodeIndex[sourceVar], searchCallback);
	cs_assert(foundSupports);

	for (VarID tempSource : tempSources)
	{
		m_reachabilitySources.erase(tempSource);
	}

	m_maxGraph->fastForward();
	return lits;
}


void ReachabilityConstraint::sanityCheckUnreachable(IVariableDatabase* db, int nodeIndex)
{
	#if SANITY_CHECKS
	// For each source that could possibly exist...
	for (VarID potentialSource : m_initialPotentialSources)
	{
		int sourceNode = m_variableToSourceNodeIndex[potentialSource];
		// If this is currently a potential source...
		if (db->getPotentialValues(potentialSource).anyPossible(m_sourceMask))
		{
			cs_assert(!TopologySearchAlgorithm::canReach(m_maxGraph, sourceNode, nodeIndex));
		}
	}
	#endif
}


vector<VarID> ReachabilityConstraint::getConstrainingVariables() const
{
	vector<VarID> out;
	for (int i = 0; i < m_sourceGraph->getNumNodes(); ++i)
	{
		VarID var = m_sourceGraphData->get(i);
		if (var.isValid())
		{
			out.push_back(var);
		}
	}

	for (int i = 0; i < m_edgeGraph->getNumNodes(); ++i)
	{
		VarID var = m_edgeGraphData->get(i);
		if (var.isValid())
		{
			out.push_back(var);
		}
	}

	return out;
}

bool ReachabilityConstraint::checkConflicting(IVariableDatabase* db) const
{
	// TODO
	return false;
}

#undef SANITY_CHECKS
