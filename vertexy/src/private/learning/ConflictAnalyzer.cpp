// Copyright Proletariat, Inc. All Rights Reserved.
#include "learning/ConflictAnalyzer.h"
#include "ConstraintSolver.h"
#include "variable/HistoricalVariableDatabase.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

constexpr int REDUNDANCY_CHECKING_LEVEL = 0;
constexpr bool LOG_CONFLICTS = false;

ConflictAnalyzer::ConflictAnalyzer(ConstraintSolver& inSolver)
	: m_solver(inSolver)
	, m_numTopLevelNodes(0)
{
}

SolverDecisionLevel ConflictAnalyzer::analyzeConflict(SolverTimestamp conflictTs, IConstraint* conflictingConstraint, VarID contradictingVariable, ClauseConstraint*& outLearned)
{
	if (m_variableClauseIndices.empty())
	{
		m_variableClauseIndices.resize(m_solver.getVariableDB()->getNumVariables() + 1, -1);
	}

	vxy_assert(m_solver.hasFinishedInitialArcConsistency());

	if (m_solver.getCurrentDecisionLevel() == 0)
	{
		return -1;
	}

	//
	// Ask the constraint that failed for an explanation. If there was a variable that was contradicting
	// (i.e. no potential values remaining), then ask for an explanation for that. Otherwise, ask the
	// constraint for a general explanation for its failure.
	//

	static vector<Literal> explanation;
	if (!contradictingVariable.isValid())
	{
		HistoricalVariableDatabase hdb(&m_solver.m_variableDB, conflictTs);
		NarrowingExplanationParams params(&m_solver, &hdb, conflictingConstraint, VarID::INVALID, {}, conflictTs);
		conflictingConstraint->explain(params, explanation);
	}
	else
	{
		// VERTEXY_LOG("** Contradiction on %s", Solver.VariableDB.GetVariableName(ContradictingVariable).c_str());
		vxy_sanity(m_solver.m_variableDB.isInContradiction(contradictingVariable));
		m_solver.getExplanationForModification(m_solver.m_variableDB.getLastModificationTimestamp(contradictingVariable), explanation);
	}
	if constexpr (LOG_CONFLICTS)
	{
		VERTEXY_LOG("Initial conflict explanation: %s", m_solver.literalArrayToString(explanation).c_str());
	}

	// Some explanations can return empty values, which are useless (they won't ever be a support)
	// Remove them.
	for (int i = explanation.size() - 1; i >= 0; --i)
	{
		if (explanation[i].values.isZero())
		{
			explanation.erase_unsorted(&explanation[i]);
		}
	}
	vxy_sanity(!explanation.empty());

	//
	// Look through the implication graph to find a full explanation for the conflict. This will
	// also determine how far we need to backtrack in order to avoid the conflict.
	//

	SolverDecisionLevel backtrackLevel = searchImplicationGraph(explanation, conflictingConstraint, conflictTs);

	// If this triggers, some constraint is returning a bad explanation...
	vxy_sanity(!containsPredicate(explanation.begin(), explanation.end(), [&](auto& lit) { return !lit.values.contains(false); }));

	//
	// If conflicting constraint is a learned constraint in the temporary pool, up its activity and see if it deserves
	// to be placed into the permanent pool.
	//

	if (auto learnedCons = conflictingConstraint->asClauseConstraint(); learnedCons && learnedCons->isLearned())
	{
		m_solver.markConstraintActivity(*learnedCons);
	}

	//
	// Record the new constraint
	//

	outLearned = m_solver.learn(explanation, m_resolvedRelationInfo.get());

	return backtrackLevel;
}

SolverDecisionLevel ConflictAnalyzer::searchImplicationGraph(vector<Literal>& inOutExplanation, const IConstraint* initialConflict, SolverTimestamp conflictTime)
{
	const SolverDecisionLevel decisionLevelAtConflict = m_solver.getDecisionLevelForTimestamp(conflictTime);
	vxy_assert(m_solver.getCurrentDecisionLevel() == decisionLevelAtConflict);

	//
	// Grab the variables in the conflicting constraint's explanation, along with their last modification time.
	// As part of the loop, determine how many variables were modified at the current decision level, and
	// how many variables were assigned as part of a decision during search.
	//

	m_nodes.clear();
	m_nodes.reserve(inOutExplanation.size());
	m_graph = initialConflict->getGraph();
	m_graphFilter = initialConflict->getGraphRelationInfo() != nullptr ? initialConflict->getGraphRelationInfo()->getFilter() : nullptr;
	m_anchorGraphVertex = initialConflict->getGraphRelationInfo() != nullptr ? initialConflict->getGraphRelationInfo()->getSourceGraphVertex() : -1;

	ConstraintGraphRelationInfo initialConflictRelationInfo;
	if (!initialConflict->getGraphRelations(inOutExplanation, initialConflictRelationInfo))
	{
		initialConflictRelationInfo.invalidate();
		m_graph = nullptr;
	}

	m_topLevel = -1;
	for (const Literal& lit : inOutExplanation)
	{
		m_variableClauseIndices[lit.variable.raw()] = m_nodes.size();

		SolverTimestamp time = m_solver.m_variableDB.getLastModificationTimestamp(lit.variable);

		m_nodes.push_back({lit.variable, time, m_solver.getDecisionLevelForTimestamp(time)});
		applyGraphRelation(m_nodes.back(), initialConflictRelationInfo, lit.values, EGraphRelationType::Initialize);

		m_topLevel = max(m_nodes.back().level, m_topLevel);
	}

	refreshTopLevelNodeCount();

	//
	// Move the modification timestamp of each variable in the explanation backwards to the moment they
	// became conflicting. This is useful because the arbitrary order of propagation may mean that
	// conflict happened earlier, but propagation just hadn't happened yet, so the conflict wasn't discovered
	// til later (in the same decision level).
	//

	vxy_assert(!m_nodes.empty());
	for (int i = 0; i < m_nodes.size(); ++i)
	{
		vxy_assert(m_nodes[i].var == inOutExplanation[i].variable);
		relax(m_nodes[i], inOutExplanation[i].values);
	}

	auto& db = m_solver.m_variableDB;
	auto& assignmentStack = db.getAssignmentStack().getStack();

	const SolverTimestamp mostRecentDecisionAssignment = m_solver.getTimestampForCurrentDecisionLevel() + 1;
	vxy_sanity(assignmentStack[mostRecentDecisionAssignment].constraint == nullptr);

	//
	// Look backwards through the implication graph, starting with the most recent variable change. We have built
	// a full explanation of the conflict once there is only one node at the most recent decision level, and we
	// have at least one variable that was assigned as part of a decision.
	//

	int mostRecentNodeIndex = findMostRecentNodeIndex();
	static ConstraintGraphRelationInfo relationInfo;

	static vector<Literal> explToResolve;
	while (!m_nodes.empty() && (m_numTopLevelNodes > 1 || m_nodes[mostRecentNodeIndex].time > mostRecentDecisionAssignment))
	{
		const VarID pivotVar = m_nodes[mostRecentNodeIndex].var;
		const SolverTimestamp lastModificationTime = m_nodes[mostRecentNodeIndex].time;
		vxy_assert(m_nodes[mostRecentNodeIndex].var == inOutExplanation[mostRecentNodeIndex].variable);
		vxy_assert(lastModificationTime == findLatestFalseTime(m_nodes[mostRecentNodeIndex].var, inOutExplanation[mostRecentNodeIndex].values, lastModificationTime));

		if (lastModificationTime < 0)
		{
			// Hit beginning of time, unsolvable.
			break;
		}

		const AssignmentStack::Modification& mod = assignmentStack[lastModificationTime];
		vxy_assert(mod.variable == pivotVar);

		IConstraint* antecedent = mod.constraint;
		if (antecedent == nullptr)
		{
			// hit a decision, just back up to that.
			break;
		}

		//
		// If this is a temporary learned constraint, bump its activity since it's part of a conflict.
		// Also, recompute the LBD score and move it into the permanent pool if LBD is low enough.
		//

		if (auto learnedCons = antecedent->asClauseConstraint(); learnedCons && learnedCons->isLearned())
		{
			m_solver.markConstraintActivity(*learnedCons);
		}

		//
		// Ask the constraint why it made this modification
		//

		m_solver.getExplanationForModification(lastModificationTime, explToResolve);
		//VERTEXY_LOG("Antecedent constraint explanation on pivot %s at %d: %s", *db.GetVariableName(PivotVar).ToString(), LastModificationTime, *Solver.LiteralArrayToString(ExplToResolve));

		//
		// Resolve this constraint's explanation with the explanation we've built so far, using logical inference
		// to add/remove terms.
		//

		if (!antecedent->getGraphRelations(explToResolve, relationInfo))
		{
			relationInfo.invalidate();
		}

		resolve(explToResolve, relationInfo, inOutExplanation, pivotVar, lastModificationTime);
		//VERTEXY_LOG("Resolved to %s", *Solver.LiteralArrayToString(InOutExplanation));

		// Find the new most-recent node
		// TODO: We should be able to keep track of this instead of searching every iteration
		mostRecentNodeIndex = findMostRecentNodeIndex();
	}

	// Clear out VariableClauseIndices, so it is fresh for next time.
	// NOTE! We don't want to return earlier than here!
	for (auto& literal : inOutExplanation)
	{
		m_variableClauseIndices[literal.variable.raw()] = -1;
	}

	vxy_assert(m_nodes.size() == inOutExplanation.size());
	vxy_assert(m_nodes[mostRecentNodeIndex].var == inOutExplanation[mostRecentNodeIndex].variable);
	vxy_sanity(m_nodes[mostRecentNodeIndex].time == findLatestFalseTime(m_nodes[mostRecentNodeIndex].var, inOutExplanation[mostRecentNodeIndex].values, m_nodes[mostRecentNodeIndex].time));

	if (m_nodes.empty() || m_nodes[mostRecentNodeIndex].time < 0)
	{
		// No solution
		return -1;
	}
	else
	{
		// The most recent node index now reflects our Unique Implication Point (UIP)
		SolverTimestamp uipTime = m_nodes[mostRecentNodeIndex].time;

		// Ensure asserting literal is first in the list
		// Step() will always propagate first literal immediately after backtrack
		if (mostRecentNodeIndex != 0)
		{
			swap(m_nodes[mostRecentNodeIndex], m_nodes[0]);
			swap(inOutExplanation[mostRecentNodeIndex], inOutExplanation[0]);
		}


		//
		// Find the graph, if any, this learned constraint can be part of. Each variable needs to be in the
		// same graph, or has a relation to the graph.
		//

		m_resolvedRelationInfo.reset();
		if (m_graph != nullptr)
		{
			vxy_sanity(initialConflict->getGraphRelationInfo() && initialConflict->getGraphRelationInfo()->getGraph() == m_graph);
			const bool isPromotable = !containsPredicate(m_nodes.begin() + 1, m_nodes.end(), [&](auto& node)
			{
				return !node.hasGraphRelation();
			});
			if (isPromotable)
			{
				m_resolvedRelationInfo = move(make_unique<ConstraintGraphRelationInfo>(m_graph, m_anchorGraphVertex, m_graphFilter));
				for (int i = 0; i < m_nodes.size(); ++i)
				{
					const ImplicationNode& node = m_nodes[i];

					if (auto varRel = get_if<GraphVariableRelationPtr>(&node.relation))
					{
						m_resolvedRelationInfo->addVariableRelation(node.var, *varRel);						
					}
					else
					{
						vxy_assert(node.var == inOutExplanation[i].variable);
						m_resolvedRelationInfo->addLiteralRelation(inOutExplanation[i], get<GraphLiteralRelationPtr>(node.relation));
					}

					#if VERTEXY_SANITY_CHECKS
					if (!isClauseRelation(node.relation))
					{
						auto resolvedRel = get<GraphVariableRelationPtr>(node.relation);
						VarID resolvedVar;
						vxy_sanity(resolvedRel->getRelation(m_anchorGraphVertex, resolvedVar) != false);
						vxy_sanity(resolvedVar == node.var);
					}
					else
					{
						auto resolvedRel = get<GraphLiteralRelationPtr>(node.relation);
						Literal resolvedLit;
						vxy_sanity(resolvedRel->getRelation(m_anchorGraphVertex, resolvedLit) != false);
						vxy_sanity(resolvedLit == inOutExplanation[i]);
					}
					#endif
				}
			}
		}

		//
		// Remove any redundant variables
		//
		if constexpr (REDUNDANCY_CHECKING_LEVEL > 0)
		{
			uint32_t levelMask = 0;
			for (int i = 1; i < m_nodes.size(); ++i)
			{
				levelMask |= getLevelBit(m_nodes[i].level);
			}

			int newSize = 1;
			for (int i = 1; i < m_nodes.size(); ++i)
			{
				if (m_nodes[i].time < 0 || assignmentStack[m_nodes[i].time].constraint == nullptr || !checkRedundant(inOutExplanation, i, levelMask))
				{
					inOutExplanation[newSize] = inOutExplanation[i];

					m_nodes[newSize] = m_nodes[i];
					++newSize;
				}
			}
			if (newSize < inOutExplanation.size())
			{
				// VERTEXY_LOG("Reduced clause size from %d to %d", InOutExplanation.Num(), NewSize);
				// VERTEXY_LOG("Old: %s", *Solver.LiteralArrayToString(InOutExplanation));

				inOutExplanation.resize(newSize);
				m_nodes.resize(newSize);

				// VERTEXY_LOG("New: %s", *Solver.LiteralArrayToString(InOutExplanation));
			}
		}

		// Take note of constraint/variable activity for heuristics
		markActivity(inOutExplanation, uipTime);

		//
		// Calculate decision level we should backtrack to.
		//

		if (m_nodes.size() == 1)
		{
			// TODO: single literal clauses have no way to propagate currently
			return 0;
			//FSolverDecisionLevel DL = Solver.GetDecisionLevelForVariable(Nodes[0].Key);
			//return DL > 0 ? DL-1 : 0;
		}
		else
		{
			// TODO: I don't think relaxation is required here and below: should be relaxed already
			vxy_assert(m_nodes[1].var == inOutExplanation[1].variable);
			m_nodes[1].time = findLatestFalseTime(m_nodes[1].var, inOutExplanation[1].values, m_nodes[1].time);
			m_nodes[1].level = m_solver.getDecisionLevelForTimestamp(m_nodes[1].time);

			//
			// Find the latest node before the UIP
			//

			SolverTimestamp beforeMostRecentIndex = 1;
			SolverDecisionLevel bestLevel = m_nodes[1].level;
			for (int i = 2; i < m_nodes.size(); ++i)
			{
				vxy_sanity(inOutExplanation[i].variable == m_nodes[i].var);
				if (m_nodes[i].time > m_nodes[beforeMostRecentIndex].time)
				{
					int newTime = findLatestFalseTime(m_nodes[i].var, inOutExplanation[i].values, m_nodes[i].time);
					if (newTime != m_nodes[i].time)
					{
						m_nodes[i].time = newTime;
						m_nodes[i].level = m_solver.getDecisionLevelForTimestamp(m_nodes[i].time);
					}

					SolverDecisionLevel level = m_nodes[i].level;
					if (level > bestLevel)
					{
						bestLevel = level;
						beforeMostRecentIndex = i;
					}
				}
			}

			//
			// The most recent literal before the UIP needs to be in the second slot so it is properly watched
			//

			if (beforeMostRecentIndex != 1)
			{
				swap(m_nodes[1], m_nodes[beforeMostRecentIndex]);
				swap(inOutExplanation[1], inOutExplanation[beforeMostRecentIndex]);
			}

			vxy_assert(bestLevel < m_solver.getDecisionLevelForTimestamp(uipTime));
			return bestLevel;
		}
	}
}

int ConflictAnalyzer::findMostRecentNodeIndex() const
{
	int mostRecentNodeIndex = 0;
	for (int i = 1; i < m_nodes.size(); ++i)
	{
		vxy_sanity(m_solver.getDecisionLevelForTimestamp(m_nodes[i].time) <= m_topLevel);
		if (m_nodes[i].time > m_nodes[mostRecentNodeIndex].time)
		{
			mostRecentNodeIndex = i;
		}
	}

	return mostRecentNodeIndex;
}

void ConflictAnalyzer::resolve(const vector<Literal>& newClauses, const ConstraintGraphRelationInfo& relationInfo, vector<Literal>& outClauses, VarID pivotVar, SolverTimestamp newClauseTimestamp)
{
	auto& stack = m_solver.m_variableDB.getAssignmentStack().getStack();

	int pivotVarIndex = getNodeIndexForVar(pivotVar);
	SolverTimestamp pivotModTime = m_nodes[pivotVarIndex].time;

	if (relationInfo.getFilter() != nullptr)
	{
		if (m_graphFilter == nullptr)
		{
			m_graphFilter = relationInfo.getFilter();
		}
		else
		{
			m_graphFilter = TManyToOneGraphRelation<bool>::combine(m_graphFilter, relationInfo.getFilter());
		}
	}
	
	//
	// Insert all clauses into the disjunction, except for the pivot variable
	//

	int newClausePivotIndex = -1;
	for (int i = 0; i < newClauses.size(); ++i)
	{
		if (newClauses[i].variable == pivotVar)
		{
			newClausePivotIndex = i;
			continue;
		}

		if (!newClauses[i].values.isZero())
		{
			insertClause(newClauses[i], relationInfo, outClauses, m_solver.m_variableDB.getModificationTimePriorTo(newClauses[i].variable, newClauseTimestamp));
		}
	}

	//
	// Intersect the pivot variable's potential values with the full explanation
	//

	{
		vxy_assert(newClausePivotIndex >= 0);

		vxy_assert(outClauses[pivotVarIndex].variable == pivotVar);
		Literal& pivotClause = outClauses[pivotVarIndex];

		pivotClause.values.intersect(newClauses[newClausePivotIndex].values);

		SolverDecisionLevel prevDecisionLevel = m_nodes[pivotVarIndex].level;

		vxy_assert(prevDecisionLevel <= m_topLevel);
		if (prevDecisionLevel == m_topLevel)
		{
			--m_numTopLevelNodes;
		}

		if (pivotClause.values.isZero())
		{
			vxy_assert(m_variableClauseIndices[pivotVar.raw()] == pivotVarIndex);
			m_variableClauseIndices[pivotVar.raw()] = -1;

			outClauses.erase_unsorted(&outClauses[pivotVarIndex]);
			if (pivotVarIndex < outClauses.size())
			{
				m_variableClauseIndices[outClauses[pivotVarIndex].variable.raw()] = pivotVarIndex;
			}

			m_nodes.erase_unsorted(&m_nodes[pivotVarIndex]);
		}
		else
		{
			vxy_sanity(stack[pivotModTime].variable == m_nodes[pivotVarIndex].var);
			m_nodes[pivotVarIndex].time = stack[pivotModTime].previousVariableAssignment;

			applyGraphRelation(m_nodes[pivotVarIndex], relationInfo, newClauses[newClausePivotIndex].values, EGraphRelationType::Intersection);

			SolverDecisionLevel newDecisionLevel = m_solver.getDecisionLevelForTimestamp(m_nodes[pivotVarIndex].time);
			vxy_assert(newDecisionLevel <= prevDecisionLevel);
			m_nodes[pivotVarIndex].level = newDecisionLevel;
			if (newDecisionLevel == m_topLevel)
			{
				++m_numTopLevelNodes;
			}

			relax(m_nodes[pivotVarIndex], pivotClause.values);
		}
	}

	// If we ran out of nodes at the top level, find then new top level.
	if (m_numTopLevelNodes == 0)
	{
		refreshTopLevel();
	}
}

void ConflictAnalyzer::insertClause(const Literal& clause, const ConstraintGraphRelationInfo& originRelationInfo, vector<Literal>& outClauses, SolverTimestamp newTimestamp)
{
	int nodeIndex;
	int clauseIndex = m_variableClauseIndices[clause.variable.raw()];
	vxy_sanity(clauseIndex == indexOfPredicate(outClauses.begin(), outClauses.end(), [&](auto& c) { return c.variable == clause.variable; }));

	if (clauseIndex < 0)
	{
		vxy_sanity(getNodeIndexForVar(clause.variable) < 0);
		nodeIndex = m_nodes.size();

		SolverDecisionLevel newLevel = m_solver.getDecisionLevelForTimestamp(newTimestamp);
		vxy_assert(newLevel <= m_topLevel);
		m_nodes.push_back({clause.variable, newTimestamp, newLevel});

		if (newLevel == m_topLevel)
		{
			++m_numTopLevelNodes;
		}

		clauseIndex = outClauses.size();
		outClauses.push_back(clause);
		m_variableClauseIndices[clause.variable.raw()] = clauseIndex;
		applyGraphRelation(m_nodes[nodeIndex], originRelationInfo, clause.values, EGraphRelationType::Initialize);
	}
	else
	{
		vxy_assert(getNodeIndexForVar(clause.variable) == clauseIndex);
		nodeIndex = clauseIndex;

		ImplicationNode& node = m_nodes[nodeIndex];
		outClauses[clauseIndex].values.include(clause.values);
		vxy_sanity(outClauses[clauseIndex].values.contains(false));

		SolverDecisionLevel oldLevel = node.level;
		if (newTimestamp > node.time)
		{
			SolverDecisionLevel newLevel = m_solver.getDecisionLevelForTimestamp(newTimestamp);
			node.level = newLevel;

			if (oldLevel == m_topLevel)
			{
				--m_numTopLevelNodes;
			}
			if (newLevel == m_topLevel)
			{
				++m_numTopLevelNodes;
			}

			node.time = newTimestamp;
		}
		applyGraphRelation(m_nodes[nodeIndex], originRelationInfo, clause.values, EGraphRelationType::Union);
	}

	relax(m_nodes[nodeIndex], outClauses[clauseIndex].values);
}

template <typename T>
shared_ptr<const IGraphRelation<T>> ConflictAnalyzer::createOffsetGraphRelation(int graphNode, const shared_ptr<const IGraphRelation<T>>& inRel)
{
	vxy_assert(m_graph != nullptr);
	if (graphNode != m_anchorGraphVertex)
	{
		TopologyLink link;
		if (!m_graph->getTopologyLink(m_anchorGraphVertex, graphNode, link))
		{
			return nullptr;
		}

		if (auto existingLinkRel = dynamic_cast<const TTopologyLinkGraphRelation<T>*>(inRel.get()))
		{
			TopologyLink combinedLink = link.combine(existingLinkRel->getLink());
			if (combinedLink.isEquivalent(TopologyLink::SELF, *m_graph))
			{
				return make_shared<TVertexToDataGraphRelation<T>>(existingLinkRel->getTopo(), existingLinkRel->getData());
			}
			return make_shared<TTopologyLinkGraphRelation<T>>(existingLinkRel->getTopo(), existingLinkRel->getData(), combinedLink);
		}
		else if (auto existingMapping = dynamic_cast<const TMappingGraphRelation<T>*>(inRel.get()))
		{
			if (auto mapperLinkRel = dynamic_cast<const TopologyLinkIndexGraphRelation*>(existingMapping->getFirstRelation().get()))
			{
				TopologyLink combinedLink = link.combine(mapperLinkRel->getLink());
				if (combinedLink.isEquivalent(TopologyLink::SELF, *m_graph))
				{
					return existingMapping->getSecondRelation();
				}
				auto newLinkRel = make_shared<TopologyLinkIndexGraphRelation>(m_graph, combinedLink);
				return newLinkRel->map(existingMapping->getSecondRelation());
			}
		}

		auto linkRel = make_shared<TopologyLinkIndexGraphRelation>(m_graph, link);
		return linkRel->map(inRel);
	}
	else
	{
		return inRel;
	}
}

void ConflictAnalyzer::applyGraphRelation(ImplicationNode& node, const ConstraintGraphRelationInfo& originGraphInfo, const ValueSet& values, EGraphRelationType applicationType)
{
	if (m_anchorGraphVertex < 0)
	{
		return;
	}

	if (m_graph != originGraphInfo.getGraph())
	{
		m_graph = nullptr;
		node.clearGraphRelation();
		return;
	}


	ARelation newRelation;
	GraphLiteralRelationPtr newLiteralRelation;
	GraphVariableRelationPtr newVarRelation;
	
	if (originGraphInfo.getLiteralRelation(Literal(node.var, values), newLiteralRelation))
	{
		newRelation = newLiteralRelation;
	}
	else if (originGraphInfo.getVariableRelation(node.var, newVarRelation))
	{
		newRelation = newVarRelation;
	}
	else
	{
		m_graph = nullptr;
		node.clearGraphRelation();
		return;
	}

	if (!compatibleRelations(node.relation, newRelation))
	{
		m_graph = nullptr;
		node.clearGraphRelation();
		return;
	}
	
	const bool hasExistingRelation = node.hasGraphRelation();

	// Apply the relation
	bool isValid = true;
	
	//
	// Handle Vertex -> Literal relations
	//
	if (newLiteralRelation != nullptr)
	{
		auto offsetRel = createOffsetGraphRelation(originGraphInfo.getSourceGraphVertex(), newLiteralRelation);
		
		Literal relationVals;
		vxy_verify(newLiteralRelation->getRelation(originGraphInfo.getSourceGraphVertex(), relationVals));
		vxy_sanity(relationVals.variable == node.var);
		if (applicationType == EGraphRelationType::Intersection)
		{
			relationVals.values.invert();
			offsetRel = make_shared<InvertLiteralGraphRelation>(offsetRel);
		}

		if (relationVals.values != values)
		{
			// The explanation returned something unexpected. TODO: Maybe can check subset instead?
			isValid = false;
		}
		else if (!hasExistingRelation || !offsetRel->equals(*get<GraphLiteralRelationPtr>(node.relation)))
		{
			if (hasExistingRelation)
			{
				if (applicationType == EGraphRelationType::Intersection)
				{
					auto intersectRel = make_shared<LiteralIntersectionGraphRelation>();
					intersectRel->add(get<GraphLiteralRelationPtr>(node.relation));
					intersectRel->add(offsetRel);
					node.relation = intersectRel;
				}
				else
				{
					auto unionRel = make_shared<LiteralUnionGraphRelation>();
					unionRel->add(get<GraphLiteralRelationPtr>(node.relation));
					unionRel->add(offsetRel);
					node.relation = unionRel;
				}
			}
			else
			{
				node.relation = offsetRel;
			}
		}
	}
	else
	{
		//
		// Handle Vertex -> VarID relations
		//

		auto offsetRel = createOffsetGraphRelation(originGraphInfo.getSourceGraphVertex(), newVarRelation);
		if (!hasExistingRelation || !offsetRel->equals(*get<GraphVariableRelationPtr>(node.relation)))
		{
			if (hasExistingRelation)
			{
				auto existingRel = get<GraphVariableRelationPtr>(node.relation);
				if (node.multiRelation == nullptr)
				{
					auto newMultiRel = make_shared<TManyToOneGraphRelation<VarID>>();
					// Compact chained ManyToOneGraphRelations:
					if (auto existingMultiRel = dynamic_cast<const TManyToOneGraphRelation<VarID>*>(existingRel.get()))
					{
						for (auto& inner : existingMultiRel->getRelations())
						{
							newMultiRel->add(inner);
						}
					}
					else
					{
						newMultiRel->add(existingRel);
					}
					node.relation = newMultiRel;
					node.multiRelation = newMultiRel;
				}

				bool isAlreadyContained = containsPredicate(node.multiRelation->getRelations().begin(), node.multiRelation->getRelations().end(), [&](auto&& inner)
				{
					return inner->equals(*offsetRel);
				});
				if (!isAlreadyContained)
				{
					node.multiRelation->add(offsetRel);
				}
			}
			else
			{
				node.relation = offsetRel;
			}
		}
	}

	if (!isValid)
	{
		m_graph = nullptr;
		node.clearGraphRelation();
	}
}

bool ConflictAnalyzer::compatibleRelations(const ARelation& existingRelation, const ARelation& newRelation)
{
	return visit([&](auto&& typedRel)
	{
		using T1 = decay_t<decltype(typedRel)>;
		if (typedRel == nullptr)
		{
			return true;
		}
		else if (is_same_v<T1, GraphLiteralRelationPtr>)
		{
			return isClauseRelation(newRelation);
		}
		else
		{
			return !isClauseRelation(newRelation);
		}
	}, existingRelation);
}

// Conflict clause minimization: see http://minisat.se/downloads/MiniSat_v1.13_short.pdf
bool ConflictAnalyzer::checkRedundant(const vector<Literal>& explanation, int litIndex, uint32_t levelMask) const
{
	auto& db = m_solver.m_variableDB;
	auto& stack = db.getAssignmentStack().getStack();

	static vector<Literal> reason;
	if constexpr (REDUNDANCY_CHECKING_LEVEL == 1)
	{
		// !!FIXME!! I don't think this is quite right... It's checking for variables but not values.
		// The full version (after this block) correctly looks at values.
		vxy_fail();

		// Simple/cheaper version of redundancy check that just sees if the reason for this literal's propagation
		// is a subset of the constraint we're learning. If so, it's redundant.
		m_solver.getExplanationForModification(m_nodes[litIndex].time, reason);
		for (auto& reasonLit : reason)
		{
			if (db.getModificationTimePriorTo(reasonLit.variable, m_nodes[litIndex].time) >= 0)
			{
				bool wasFound = contains(explanation.begin(), explanation.end(), [&](auto& literal)
				{
					return literal.variable == reasonLit.variable;
				});
				if (!wasFound)
				{
					return false;
				}
			}
		}

		return true;
	}

	m_redundancySeen.clear();
	m_redundancySeen.pad(m_solver.m_variableDB.getNumVariables() + 1, false);
	m_redundancyValues.resize(m_solver.m_variableDB.getNumVariables() + 1);

	for (int i = 1; i < explanation.size(); ++i)
	{
		m_redundancySeen[explanation[i].variable.raw()] = true;
		m_redundancyValues[explanation[i].variable.raw()] = explanation[i].values;
	}

	// Start by checking if this variable is subsumed by a prior constraint in the implication graph.
	// I.e. if the explanation for this literal propagating is a subset of the constraint we're learning, then
	// it is redundant.
	// Otherwise, recurse: if there is some literal in the reason for this literal that doesn't appear in
	// the learned constraint, see if the reason for THAT literal is a subset of the learned constraint. If so,
	// that literal can be ignored.
	m_redundancyStack.clear();
	vxy_assert(m_nodes[litIndex].var == explanation[litIndex].variable);
	m_redundancyStack.push_back(ImplicationNode{explanation[litIndex].variable, m_nodes[litIndex].time, -1});

	static vector<Literal> reasons;
	while (!m_redundancyStack.empty())
	{
		ImplicationNode curNode = m_redundancyStack.back();
		m_redundancyStack.pop_back();

		if (curNode.time >= 0)
		{
			vxy_assert(stack[curNode.time].variable == curNode.var);
			vxy_assert(stack[curNode.time].constraint != nullptr);

			m_solver.getExplanationForModification(curNode.time, reasons);
			for (auto& reason : reasons)
			{
				// Check if we've seen this variable and all its values
				bool alreadySeen = m_redundancySeen[reason.variable.raw()];
				ValueSet& seenValues = m_redundancyValues[reason.variable.raw()];
				if (!alreadySeen || !reason.values.isSubsetOf(seenValues))
				{
					SolverTimestamp reasonTimestamp = db.getModificationTimePriorTo(reason.variable, curNode.time);
					reasonTimestamp = findLatestFalseTime(reason.variable, reason.values, reasonTimestamp);

					if (reasonTimestamp >= 0)
					{
						vxy_assert(stack[reasonTimestamp].variable == reason.variable);

						SolverDecisionLevel reasonLevel = m_solver.getDecisionLevelForTimestamp(reasonTimestamp);
						if (stack[reasonTimestamp].constraint && (getLevelBit(reasonLevel) & levelMask) != 0)
						{
							// Mark the variable and values in this reason as seen
							if (!alreadySeen)
							{
								m_redundancySeen[reason.variable.raw()] = true;
								seenValues = reason.values;
							}
							else
							{
								seenValues.include(reason.values);
							}
							m_redundancyStack.push_back(ImplicationNode{reason.variable, reasonTimestamp, -1});
						}
						else
						{
							return false;
						}
					}
				}
			}
		}
	}

	return true;
}

bool ConflictAnalyzer::relax(ImplicationNode& node, const ValueSet& assertingValue)
{
	int origTime = node.time;
	if (node.time < 0)
	{
		return false;
	}

	if (node.level == m_topLevel)
	{
		--m_numTopLevelNodes;
	}

	node.time = findLatestFalseTime(node.var, assertingValue, node.time);
	vxy_assert(node.time <= origTime);
	if (node.time != origTime)
	{
		node.level = m_solver.getDecisionLevelForTimestamp(node.time);
	}

	if (node.level == m_topLevel)
	{
		++m_numTopLevelNodes;
	}

	if (m_numTopLevelNodes == 0)
	{
		refreshTopLevel();
	}

	return node.time != origTime;
}

// Finds the timestamp for a variable at which AssertingValue becomes impossible
SolverTimestamp ConflictAnalyzer::findLatestFalseTime(VarID var, const ValueSet& assertingValue, SolverTimestamp latestTime) const
{
	auto& stack = m_solver.m_variableDB.getAssignmentStack().getStack();
	while (latestTime >= 0)
	{
		vxy_assert(stack[latestTime].variable == var);
		if (stack[latestTime].previousValue.anyPossible(assertingValue))
		{
			break;
		}

		latestTime = stack[latestTime].previousVariableAssignment;
	}
	return latestTime;
}

int ConflictAnalyzer::getNodeIndexForVar(VarID var) const
{
	return indexOfPredicate(m_nodes.begin(), m_nodes.end(), [&](const ImplicationNode& n) { return n.var == var; });
}

void ConflictAnalyzer::refreshTopLevel()
{
	int prevTopLevel = m_topLevel;
	m_topLevel = -1;
	for (auto& node : m_nodes)
	{
		m_topLevel = max(m_topLevel, node.level);
	}

	vxy_assert(m_topLevel <= prevTopLevel);
	refreshTopLevelNodeCount();
}

void ConflictAnalyzer::refreshTopLevelNodeCount()
{
	m_numTopLevelNodes = 0;
	for (auto& node : m_nodes)
	{
		vxy_assert(node.level <= m_topLevel);
		if (node.level == m_topLevel)
		{
			++m_numTopLevelNodes;
		}
	}
}

void ConflictAnalyzer::markActivity(const vector<Literal>& resolvedExplanation, SolverTimestamp uipTime)
{
	auto& db = m_solver.m_variableDB;
	auto& assignmentStack = db.getAssignmentStack().getStack();

	//
	// Mark conflict activity for all variables/values in the learned clause
	//

	auto& heuristics = m_solver.getDecisionHeuristics();
	const bool wantsReasonActivity = containsPredicate(heuristics.begin(), heuristics.end(), [&](auto heuristic)
	{
		return heuristic->wantsReasonActivity();
	});

	static hash_set<VarID> seenSet;
	seenSet.clear();

	for (int i = 0; i < resolvedExplanation.size(); ++i)
	{
		auto& literal = resolvedExplanation[i];
		if (m_nodes[i].time >= 0 && m_nodes[i].level > 0)
		{
			for (auto& heuristic : m_solver.getDecisionHeuristics())
			{
				heuristic->onVariableConflictActivity(literal.variable, literal.values, db.getValueBefore(literal.variable, uipTime));
			}
		}
		if (wantsReasonActivity) { seenSet.insert(literal.variable); }
	}

	//
	// Separately mark any variables involved in the reason for literals in the explanation, but don't appear in the
	// conflict.
	//

	if (wantsReasonActivity)
	{
		static vector<Literal> reasons;
		for (auto& node : m_nodes)
		{
			SolverTimestamp explanationTime = node.time;
			if (explanationTime < 0)
			{
				continue;
			}

			if (assignmentStack[explanationTime].constraint == nullptr)
			{
				continue;
			}

			m_solver.getExplanationForModification(explanationTime, reasons);
			for (auto& lit : reasons)
			{
				if (seenSet.find(lit.variable) != seenSet.end())
				{
					SolverTimestamp valuePreviousTime;
					const ValueSet& reasonValue = db.getValueBefore(lit.variable, explanationTime, &valuePreviousTime);
					if (valuePreviousTime >= 0)
					{
						const ValueSet& prevReasonValue = valuePreviousTime >= 0 ? assignmentStack[valuePreviousTime].previousValue : db.getInitialValues(lit.variable);
						for (auto& heuristic : m_solver.getDecisionHeuristics())
						{
							heuristic->onVariableReasonActivity(lit.variable, reasonValue, prevReasonValue);
						}
					}
					seenSet.insert(lit.variable);
				}
			}
		}
	}
}
