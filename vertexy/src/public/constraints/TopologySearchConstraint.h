// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "ConstraintTypes.h"
#include "IBacktrackingSolverConstraint.h"
#include "ISolverConstraint.h"
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

/** Base class for ReachabilityConstraint and ShortestPathConstraint
 */
class ITopologySearchConstraint : public IBacktrackingSolverConstraint
{
public:
	ITopologySearchConstraint(const ConstraintFactoryParams& params,
		const shared_ptr<TTopologyVertexData<VarID>>& sourceGraphData,
		const ValueSet& sourceMask,
		const ValueSet& requireReachableMask,
		const shared_ptr<TTopologyVertexData<VarID>>& edgeGraphData,
		const ValueSet& edgeBlockedMask
	);

	virtual vector<VarID> getConstrainingVariables() const override;
	virtual bool initialize(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;
	virtual bool getGraphRelations(const vector<Literal>& literals, ConstraintGraphRelationInfo& outRelations) const override;

protected:
	//for now don't worry about explainers
	vector<Literal> explainNoReachability(const NarrowingExplanationParams& params) const;
	vector<Literal> explainRequiredSource(const NarrowingExplanationParams& params, VarID removedSource = VarID::INVALID);

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
	//used by propagate, NEEDS TO CHANGE
	bool processVertexVariableChange(IVariableDatabase* db, VarID variable);
	//used by propagate
	void updateGraphsForEdgeChange(IVariableDatabase* db, VarID variable);
	void onReachabilityChanged(int vertexIndex, VarID sourceVar, bool inMinGraph);
	void sanityCheckUnreachable(IVariableDatabase* db, int vertexIndex);

	void onExplanationGraphEdgeChange(bool edgeWasAdded, int from, int to);

	void addSource(VarID source);
	//used by processVertexVariableChange
	bool removeSource(IVariableDatabase* db, VarID source);

	using ESTreeType = ESTree<BacktrackingDigraphTopology>;
	using RamalRepsType = RamalReps<BacktrackingDigraphTopology>;

	//virtual
	virtual bool isValidDistance(const IVariableDatabase* db, int dist) const = 0;
	virtual shared_ptr<RamalRepsType> makeTopology(const shared_ptr<BacktrackingDigraphTopology>& graph) const = 0;
	virtual EventListenerHandle addMinCallback(RamalRepsType& minReachable, const IVariableDatabase* db, VarID source) = 0;
	virtual EventListenerHandle addMaxCallback(RamalRepsType& maxReachable, const IVariableDatabase* db, VarID source) = 0;

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
		EdgeWatcher(ITopologySearchConstraint& parent)
			: m_parent(parent)
		{
		}

		virtual ISolverConstraint* asConstraint() override { return &m_parent; }
		virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;

	protected:
		ITopologySearchConstraint& m_parent;
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

	struct ReachabilitySourceData
	{
		#if REACHABILITY_USE_RAMAL_REPS
		shared_ptr<RamalRepsType> minReachability;
		shared_ptr<RamalRepsType> maxReachability;
		#else
		shared_ptr<ESTreeType> minReachability;
		shared_ptr<ESTreeType> maxReachability;
		#endif
		EventListenerHandle minRamalHandle = INVALID_EVENT_LISTENER_HANDLE;
		EventListenerHandle maxRamalHandle = INVALID_EVENT_LISTENER_HANDLE;
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