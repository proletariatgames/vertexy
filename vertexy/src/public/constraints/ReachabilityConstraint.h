// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "IBacktrackingSolverConstraint.h"
#include "IConstraint.h"
#include "SignedClause.h"
#include "ds/ESTree.h"
#include "ds/RamalReps.h"
#include "topology/BacktrackingDigraphTopology.h"
#include "topology/DigraphEdgeTopology.h"
#include "topology/TopologyVertexData.h"
#include "topology/algo/MaxFlowMinCut.h"
#include "variable/IVariableDatabase.h"

#define REACHABILITY_USE_RAMAL_REPS 1

namespace Vertexy
{

/** Constraint to ensure reachability in a graph from a set of sources.
 *  The SourceGraph specifies the full potential connectivity set of all variables.
 *  Any variable that is definitely one of SourceValues is considered a reachability source.
 *  Any variable that is definitely on of NeedReachableValues is considered a destination that must remain reachable from AT LEAST one source.
 */
class ReachabilityConstraint : public IBacktrackingSolverConstraint
{
public:
	ReachabilityConstraint(const ConstraintFactoryParams& params,
		const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
		const ValueSet& sourceMask,
		const ValueSet& requireReachableMask,
		const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
		const ValueSet& edgeBlockedMask
	);

	struct ReachabilityFactory
	{
		static ReachabilityConstraint* construct(
			const ConstraintFactoryParams& params,
			// Graph of vertices where reachability is calculated
			const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
			// Values of vertices in SourceGraph that establish it as a reachability source
			const vector<int>& sourceValues,
			// Values of vertices in SourceGraph that establish it as needing to be reachable from a source
			const vector<int>& needReachableValues,
			// The variables for each edge of source graph
			const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
			// Values of vertices in the edge graph establishing that edge as "off"
			const vector<int>& edgeBlockedValues);
	};

	using Factory = ReachabilityFactory;

	virtual EConstraintType getConstraintType() const override { return EConstraintType::Reachability; }
	virtual vector<VarID> getConstrainingVariables() const override;
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;
	virtual bool getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outRelations) const override;

protected:
	void explainNoReachability(const NarrowingExplanationParams& params, vector<Literal>& outExplanation) const;
	void explainRequiredSource(const NarrowingExplanationParams& params, VarID removedSource, vector<Literal>& outExplanation);

	enum class EReachabilityDetermination : uint8_t
	{
		DefinitelyReachable,
		// Reachable from a definite source
		PossiblyReachable,
		// Reachable from a possible source
		DefinitelyUnreachable,
		// Unreachable from any possible source
	};

	EReachabilityDetermination determineReachability(const IVariableDatabase* db, int vertex);
	bool processVertexVariableChange(IVariableDatabase* db, VarID variable);
	void updateGraphsForEdgeChange(IVariableDatabase* db, VarID variable);
	void onReachabilityChanged(int vertexIndex, VarID sourceVar, bool inMinGraph);
	void sanityCheckUnreachable(IVariableDatabase* db, int vertexIndex);

	void onExplanationGraphEdgeChange(bool edgeWasAdded, int from, int to);

	void addSource(VarID source);
	bool removeSource(IVariableDatabase* db, VarID source);

	inline bool definitelyNeedsToReach(const IVariableDatabase* db, VarID var) const
	{
		return !db->getPotentialValues(var).anyPossible(m_notReachableMask);
	}

	inline bool definitelyIsSource(const IVariableDatabase* db, VarID var) const
	{
		return !db->getPotentialValues(var).anyPossible(m_notSourceMask);
	}

	inline bool definitelyNotSource(const IVariableDatabase* db, VarID var) const
	{
		return !db->getPotentialValues(var).anyPossible(m_sourceMask);
	}

	inline bool possiblyIsSource(const IVariableDatabase* db, VarID var)
	{
		return !definitelyNotSource(db, var);
	}

	inline bool possiblyOpenEdge(const IVariableDatabase* db, VarID var) const
	{
		return db->getPotentialValues(var).anyPossible(m_edgeOpenMask);
	}

	inline bool definitelyOpenEdge(const IVariableDatabase* db, VarID var) const
	{
		return !db->getPotentialValues(var).anyPossible(m_edgeBlockedMask);
	}

	inline bool definitelyClosedEdge(const IVariableDatabase* db, VarID var)
	{
		return !possiblyOpenEdge(db, var);
	}

	class EdgeWatcher : public IVariableWatchSink
	{
	public:
		EdgeWatcher(ReachabilityConstraint& parent)
			: m_parent(parent)
		{
		}

		virtual IConstraint* asConstraint() override { return &m_parent; }
		virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;

	protected:
		ReachabilityConstraint& m_parent;
	};

	EdgeWatcher m_edgeWatcher;

	shared_ptr<TTopologyVertexData<VarID>> m_sourceGraphData;
	shared_ptr<ITopology> m_sourceGraph;
	shared_ptr<TTopologyVertexData<VarID>> m_edgeGraphData;
	shared_ptr<EdgeTopology> m_edgeGraph;

	// Contains edges that DEFINITELY exist. Edges are only added to this graph.
	shared_ptr<BacktrackingDigraphTopology> m_minGraph;
	// Contains edges that POSSIBLY exist. Edges are only removed from this graph.
	shared_ptr<BacktrackingDigraphTopology> m_maxGraph;
	// Synchronized with MaxGraph. Used during explanations where we need to temporarily rewind graph state, but we don't
	// want to propagate to the source reachability trees.
	shared_ptr<BacktrackingDigraphTopology> m_explanationGraph;

	ValueSet m_sourceMask;
	ValueSet m_notSourceMask;

	ValueSet m_requireReachableMask;
	ValueSet m_notReachableMask;

	ValueSet m_edgeBlockedMask;
	ValueSet m_edgeOpenMask;

	hash_map<VarID, WatcherHandle> m_vertexWatchHandles;
	hash_map<VarID, WatcherHandle> m_edgeWatchHandles;

	struct BacktrackData
	{
		SolverDecisionLevel level;
		vector<VarID> reachabilitySourcesRemoved;
	};

	vector<BacktrackData> m_backtrackData;

	using ESTreeType = ESTree<BacktrackingDigraphTopology>;
	using RamalRepsType = RamalReps<BacktrackingDigraphTopology>;

	struct ReachabilitySourceData
	{
		#if REACHABILITY_USE_RAMAL_REPS
		shared_ptr<RamalRepsType> minReachability;
		shared_ptr<RamalRepsType> maxReachability;
		#else
		shared_ptr<ESTreeType> minReachability;
		shared_ptr<ESTreeType> maxReachability;
		#endif
		EventListenerHandle minReachabilityChangedHandle = INVALID_EVENT_LISTENER_HANDLE;
		EventListenerHandle maxReachabilityChangedHandle = INVALID_EVENT_LISTENER_HANDLE;
	};

	vector<VarID> m_vertexProcessList;
	vector<VarID> m_edgeProcessList;

	vector<VarID> m_initialPotentialSources;
	hash_map<VarID, ReachabilitySourceData> m_reachabilitySources;

	hash_map<VarID, int> m_variableToSourceVertexIndex;
	hash_map<VarID, int> m_variableToSourceEdgeIndex;

	IVariableDatabase* m_edgeChangeDb = nullptr;
	bool m_edgeChangeFailure = false;
	bool m_inEdgeChange = false;
	bool m_backtracking = false;
	bool m_explainingSourceRequirement = false;
	int m_totalNumEdges = 0;

	DepthFirstSearchAlgorithm m_dfs;
	mutable TMaxFlowMinCutAlgorithm<int> m_maxFlowAlgo;
	vector<TFlowGraphEdge<int>> m_flowGraphEdges;
	FlowGraphLookupMap m_flowGraphLookup;
	RamalRepsEdgeDefinitions m_reachabilityEdgeLookup;
};

} // namespace Vertexy