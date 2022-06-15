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
	if (m_op == EConstraintOperator::LessThan || m_op == EConstraintOperator::LessThanEq)
	{
		this->m_explanationMinGraph = make_shared<BacktrackingDigraphTopology>();
	}
}

void ShortestPathConstraint::onInitialArcConsistency(IVariableDatabase* db)
{
	if (this->m_op == EConstraintOperator::LessThan || this->m_op == EConstraintOperator::LessThanEq)
	{
		m_lastValidTimestamp.clear();
	}
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
				removeTimestampToCommit(db, src.vertex, vertex);
				return false;
			}

			addTimestampToCommit(db, src.vertex, vertex);
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
				removeTimestampToCommit(db, src.vertex, vertex);
				return EValidityDetermination::DefinitelyInvalid;
			}

			addTimestampToCommit(db, src.vertex, vertex);
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
	// if we are here, it means that the path was reachable, but we marked it as invalid because of the distance constraint
	vxy_assert_msg(m_variableToSourceVertexIndex.find(params.propagatedVariable) != m_variableToSourceVertexIndex.end(), "Not a vertex variable?");

	auto db = params.database;
	int conflictVertex = m_variableToSourceVertexIndex.find(params.propagatedVariable)->second;

	vector<Literal> lits;
	lits.push_back(Literal(params.propagatedVariable, m_invalidMask));

	static hash_set<VarID> edgeVarsRecorded;
	edgeVarsRecorded.clear();

	ValueSet visited;
	visited.init(m_initialPotentialSources.size(), false);

	bool isGreaterOp = m_op == EConstraintOperator::GreaterThan || m_op == EConstraintOperator::GreaterThanEq;
	{
		bool hasAnyTimestamp = false;
		auto foundLastTimestamp = m_lastValidTimestamp.find(conflictVertex);
		if (foundLastTimestamp != m_lastValidTimestamp.end())
		{
			for (auto& it : foundLastTimestamp->second)
			{
				if (it.second.hasValue())
				{
					hasAnyTimestamp = true;
					break;
				}
			}
		}

		if (!hasAnyTimestamp)
		{
			// there was never a point where this vertex was valid
			// the best explanation is that this edge is too far away / too close from any sources
			//for (int potentialSrcIndex = 0; potentialSrcIndex < m_initialPotentialSources.size(); ++potentialSrcIndex)
			//{
			//	VarID potentialSource = m_initialPotentialSources[potentialSrcIndex];
			//	if (!db->getPotentialValues(potentialSource).anyPossible(m_sourceMask))
			//	{
			//		lits.push_back(Literal(potentialSource, m_sourceMask));
			//	}
			//	else
			//	{
			//		lits.push_back(Literal(potentialSource, m_sourceMask.inverted()));
			//	}
			//}
			//vxy_assert(false); //check if that's happening
			return lits;
		}
	}

	bool foundAny = false;
	bool hasUnprocessedSources = false;
	// For each source that could possibly exist...
	for (int potentialSrcIndex = 0; potentialSrcIndex < m_initialPotentialSources.size(); ++potentialSrcIndex)
	{
		VarID potentialSource = m_initialPotentialSources[potentialSrcIndex];

		int sourceVertex = m_variableToSourceVertexIndex.find(potentialSource)->second;
		if (sourceVertex == conflictVertex)
		{
			// Reachability sources cannot provide reachability to themselves
			continue;
		}

		// check if it's reachable
		auto reachabilitySource = m_reachabilitySources.find(potentialSource);
		//if (reachabilitySource != m_reachabilitySources.end())
		//{
		//	if (!reachabilitySource->second.maxReachability->isReachable(conflictVertex))
		//	{
		//		// it's not reachable, let's let the reachability explainer kick in
		//		hasUnprocessedSources = true;
		//		continue;
		//	}
		//}

		SolverTimestamp valueToRewind = -1;
		{
			auto& lastValid = m_lastValidTimestamp[conflictVertex].find(sourceVertex);
			if (lastValid == m_lastValidTimestamp[conflictVertex].end() || !lastValid->second.hasValue())
			{
				// there was never a possible path for the source
				// ignore this when running shortest path
				visited[potentialSrcIndex] = true;
				continue;
			}
			valueToRewind = lastValid->second.getTimestamp();
		}
		auto& graph = isGreaterOp ? m_explanationMinGraph : m_explanationMaxGraph;

		if (db->getPotentialValues(potentialSource).anyPossible(m_sourceMask) || reachabilitySource != m_reachabilitySources.end())
		{
			vector<int> path;
			//if (getShortestPathForExplanation(valueToRewind, rewindMinGraph, sourceVertex, conflictVertex, path))
			graph->rewindUntil(valueToRewind);
			auto found = m_shortestPathAlgo.find<BacktrackingDigraphTopology>(*graph, sourceVertex, conflictVertex, path);
			vxy_assert(found);
			graph->fastForward();
			//if ()
			{
				vxy_assert(isValidDistance(db, path.size() - 1));
			}
			foundAny = true;
			visited[potentialSrcIndex] = true;
			// optimization - if the vertices are siblings, and greater than is used, we can safely say that
			// this target is only reachable if this source is not a source
			//if (isGreaterThan && path.size() == 2)
			//{
			//	lits.push_back(Literal(potentialSource, m_sourceMask.inverted().excluding(db->getPotentialValues(potentialSource))));
			//	continue;
			//}
			auto& targetMask = isGreaterOp ? m_edgeBlockedMask : m_edgeOpenMask;
			bool changedEdge = false;
			for (int i = 0; i < (path.size() - 1); ++i)
			{
				// mark all edges that don't currently have the targetMask
				int edge = m_edgeGraph->getVertexForSourceEdge(path[i], path[i + 1]);
				auto edgeVar = m_edgeGraphData->get(edge);
				if (edgeVarsRecorded.find(edgeVar) == edgeVarsRecorded.end())
				{
					if (!db->getValueBefore(edgeVar, params.timestamp).anyPossible(targetMask))
					{
						//VERTEXY_LOG("  %s --> %s", db->getSolver()->getVariableName(edgeVar).c_str(), targetMask.toString().c_str());
						edgeVarsRecorded.insert(edgeVar);
						lits.push_back(Literal(edgeVar, targetMask));
						changedEdge = true;
					}
				}
				else
				{
					changedEdge = true;
				}
			}
			vxy_assert(changedEdge);
		}
		else
		{
			// Not currently a potential source. For now, the conservative explanation is that we'd be able to reach if it
			// was.
			lits.push_back(Literal(potentialSource, m_sourceMask));
			visited[potentialSrcIndex] = true;
			foundAny = true;
		}
	}
	vxy_assert(foundAny);

	if (hasUnprocessedSources)
	{
		// for all other sources, run the reachability explainer
		auto otherLits = explainNoReachability(params, &visited);
		// we can skip the first one since it's always the propagatedVariable
		for (int i = 1; i < otherLits.size(); i++)
		{
			const auto& lit = otherLits[i];
			if (edgeVarsRecorded.find(lit.variable) == edgeVarsRecorded.end())
			{
				// we don't need to add that edge to the edgeVarsRecorded because it was already deduped
				lits.push_back(lit);
			}
		}
	}

	//VERTEXY_LOG("BEGIN explainInvalid");
	//for (int i = 0; i < lits.size(); i++)
	//{
	//	VERTEXY_LOG("  %s --> %s", db->getSolver()->getVariableName(lits[i].variable).c_str(), lits[i].values.toString().c_str());
	//}

	return lits;
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

	m_processingVertices = true;
	m_queuedVertexChanges.insert(vertexIndex);
	auto reachability = determineValidity(m_edgeChangeDb, vertexIndex);

	// See if this vertex is definitely reachable by any source now
	if (reachability == EValidityDetermination::DefinitelyUnreachable)
	{
		// vertexIndex became unreachable in the max graph
		VarID var = m_sourceGraphData->get(vertexIndex);
		//sanityCheckUnreachable(db, vertexIndex);

		if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainNoReachability(params); }))
		{
			m_edgeChangeFailure = true;
		}
	}
	else if (reachability == EValidityDetermination::DefinitelyInvalid)
	{
		// vertexIndex became unreachable in the max graph
		VarID var = m_sourceGraphData->get(vertexIndex);
		//sanityCheckUnreachable(db, vertexIndex);

		if (var.isValid() && !m_edgeChangeDb->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainInvalid(params); }))
		{
			m_edgeChangeFailure = true;
		}
	}
	m_processingVertices = false;
}

void ShortestPathConstraint::backtrack(const IVariableDatabase* db, SolverDecisionLevel level)
{
	auto stamp = db->getTimestamp();
	ITopologySearchConstraint::backtrack(db, level);
	if (this->m_op == EConstraintOperator::LessThan || this->m_op == EConstraintOperator::LessThanEq)
	{
		for (auto& vertexData : m_lastValidTimestamp)
		{
			for (auto& srcData : vertexData.second)
			{
				srcData.second.backtrackUntil(stamp);
			}
		}
	}

	//for (auto& it = m_cachedKeysToTimestamp.rbegin(); it != m_cachedKeysToTimestamp.rend(); it++)
	//{
	//	if (it->first <= stamp)
	//	{
	//		for (auto& sourceAndDest : it->second)
	//		{
	//			m_cachedShortestPaths.erase(sourceAndDest);
	//		}
	//		it = m_cachedKeysToTimestamp.erase(it);
	//		if (it == m_cachedKeysToTimestamp.rend())
	//		{
	//			break;
	//		}
	//	}
	//}
}

void ShortestPathConstraint::commitValidTimestamps(const IVariableDatabase* db)
{
	for (auto& toCommitDest : m_validTimestampsToCommit)
	{
		auto& found = m_lastValidTimestamp.find(toCommitDest.first);
		hash_map<int, TBacktrackableValue<SolverTimestamp>>* validTimestamp;
		if (found == m_lastValidTimestamp.end())
		{
			m_lastValidTimestamp[toCommitDest.first] = hash_map<int, TBacktrackableValue<SolverTimestamp>>();
			validTimestamp = &m_lastValidTimestamp[toCommitDest.first];
		}
		else
		{
			validTimestamp = &found->second;
		}
		for (auto& value : toCommitDest.second)
		{
			// TODO cleanup
			if (validTimestamp->find(value.first) == validTimestamp->end())
			{
				TBacktrackableValue<SolverTimestamp> val;
				val.set(value.second, value.second);
				(*validTimestamp)[value.first] = val;
			}
			else
			{
				(*validTimestamp)[value.first].set(value.second, value.second);
			}
		}
	}
}

void ShortestPathConstraint::onEdgeChangeFailure(const IVariableDatabase* db)
{
	m_queuedVertexChanges.clear();
	m_validTimestampsToCommit.clear();
}

void ShortestPathConstraint::onEdgeChangeSuccess(const IVariableDatabase* db)
{
	commitValidTimestamps(db);
	m_validTimestampsToCommit.clear();
}

void ShortestPathConstraint::addTimestampToCommit(const IVariableDatabase* db, int sourceVertex, int destVertex)
{
	if (m_explainingSourceRequirement || m_processingVertices)
	{
		return;
	}

	auto timestamp = db->getTimestamp();
	if (m_lastValidTimestamp.find(destVertex) != m_lastValidTimestamp.end() &&
		m_lastValidTimestamp[destVertex].find(sourceVertex) != m_lastValidTimestamp[destVertex].end())
	{
		auto& val = m_lastValidTimestamp[destVertex][sourceVertex];
		if (val.hasValue())
		{
			vxy_assert(val.getTimestamp() <= timestamp);
		}
	}

	if (m_validTimestampsToCommit.find(destVertex) == m_validTimestampsToCommit.end())
	{
		m_validTimestampsToCommit[destVertex] = hash_map<int, SolverTimestamp>();
	}

	m_validTimestampsToCommit[destVertex][sourceVertex] = timestamp;
}

void ShortestPathConstraint::removeTimestampToCommit(const IVariableDatabase* db, int sourceVertex, int destVertex)
{
	m_validTimestampsToCommit.erase(sourceVertex);
}

void Vertexy::ShortestPathConstraint::processQueuedVertexChanges(IVariableDatabase* db)
{
	m_validTimestampsToCommit.clear();
	for (auto vertexIndex : m_queuedVertexChanges)
	{
		auto reachability = determineValidity(db, vertexIndex);

		// See if this vertex is definitely reachable by any source now
		if (reachability == EValidityDetermination::DefinitelyUnreachable)
		{
			// vertexIndex became unreachable in the max graph
			VarID var = m_sourceGraphData->get(vertexIndex);
			//sanityCheckUnreachable(db, vertexIndex);

			if (var.isValid() && !db->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainNoReachability(params); }))
			{
				m_edgeChangeFailure = true;
				break;
			}
		}
		else if (reachability == EValidityDetermination::DefinitelyInvalid)
		{
			// vertexIndex became unreachable in the max graph
			VarID var = m_sourceGraphData->get(vertexIndex);
			//sanityCheckUnreachable(db, vertexIndex);

			if (var.isValid() && !db->constrainToValues(var, m_invalidMask, this, [&](auto params) { return explainInvalid(params); }))
			{
				m_edgeChangeFailure = true;
				break;
			}
		}
	}
	m_queuedVertexChanges.clear();
}


#undef SANITY_CHECKS
