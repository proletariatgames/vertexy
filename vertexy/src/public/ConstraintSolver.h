// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include <EASTL/hash_map.h>
#include <random> // no EASTL implementation available

#include "ConstraintSolverStats.h"

#include "ConstraintTypes.h"
#include "SignedClause.h"
#include "constraints/ConstraintFactoryParams.h"
#include "variable/SolverVariableDatabase.h"
#include "variable/SolverVariableDomain.h"
#include "decision/CoarseLRBHeuristic.h"
#include "decision/VSIDSHeuristic.h"
#include "restart/LubyRestartPolicy.h"
#include "restart/NoRestartPolicy.h"
#include "restart/LBDRestartPolicy.h"
#include "constraints/ConstraintOperator.h"
#include "constraints/IBacktrackingSolverConstraint.h"
#include "constraints/IConstraint.h"
#include "learning/ConflictAnalyzer.h"
#include "topology/GraphArgumentTransformer.h"
#include "topology/TopologyVertexData.h"
#include "variable/IVariablePropagator.h"

#include <EASTL/deque.h>
#include <EASTL/bonus/lru_cache.h>

#include "program/Program.h"
#include "rules/RuleDatabase.h"

namespace Vertexy
{
class IRestartPolicy;

enum class EConstraintSolverResult : uint8_t
{
	// We have not yet started solving anything.
	Uninitialized,
	// We have not yet finished solving for all variables in the system.
	Unsolved,
	// We have arrived at a full solution for the system, with all variables having a value.
	Solved,
	// We have fully searched the search tree without finding a solution, i.e. no solution exists.
	Unsatisfiable
};

struct SolvedVariableRecord
{
	wstring name;
	int value;
};

/** For hashing learned constraints */
struct ConstraintHashFuncs
{
	/**
	 * @return True if the keys match.
	 */
	bool operator()(const ClauseConstraint* consA, const ClauseConstraint* consB) const
	{
		if (consA->getNumLiterals() != consB->getNumLiterals())
		{
			return false;
		}

		for (int i = 0; i < consA->getNumLiterals(); ++i)
		{
			auto& lit = consA->getLiteral(i);

			bool bFound = false;
			for (int j = 0; j < consB->getNumLiterals(); ++j)
			{
				if (consB->getLiteral(j) == lit)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				return false;
			}
		}

		return true;
	}

	/** Calculates a hash index for a key. */
	uint32_t operator()(const ClauseConstraint* cons) const
	{
		eastl::hash<VarID> varHasher;
		eastl::hash<ValueSet> valHasher;

		uint32_t hash = 0;
		for (int i = 0; i < cons->getNumLiterals(); ++i)
		{
			hash += varHasher(cons->getLiteral(i).variable);
			hash += valHasher(cons->getLiteral(i).values);
		}
		return hash;
	}
};

/** Constraint solver implementation */
class ConstraintSolver : public IVariableDomainProvider
{
	friend class SolverVariableDatabase;
	friend class ConstraintFactoryParams;
	friend class ConflictAnalyzer;
	friend class UnfoundedSetAnalyzer;
	friend class SolverDecisionLog;

	using BaseHeuristicType = CoarseLRBHeuristic;
	using RestartPolicyType = LubyRestartPolicy;

	static ConstraintSolver* s_currentSolver; // for debugging

public:
	using RandomStreamType = std::mt19937;

	// Constructor: if RandomSeed is 0, a random value will be chosen as the seed.
	ConstraintSolver(const wstring& name = TEXT("[unnamed]"), int randomSeed = 0, const shared_ptr<ISolverDecisionHeuristic>& baseHeuristic = nullptr);
	virtual ~ConstraintSolver();

	//
	// Solving API
	//

	const wstring& getName() const { return m_name; }

	void setOutputLog(const shared_ptr<SolverDecisionLog>& log)
	{
		m_outputLog = log;
	}

	const shared_ptr<SolverDecisionLog>& getOutputLog()
	{
		return m_outputLog;
	}

	const ConstraintSolverStats& getStats() const { return m_stats; }
	void dumpStats(bool verbose = false);

	// Adds a strategy to the top of the solver's strategy stack. Must be done before solving starts.
	// Strategies allow external code to drive the choice and order of what variables/values to decide on.
	// They can defer to later strategies (or the solver's heuristic), allowing the creation of multi-stage
	// solution pipelines.
	void addDecisionHeuristic(const shared_ptr<ISolverDecisionHeuristic>& strategy);

	// (Re)solve the problem. This is the same as calling StartSolving and calling Step until result != Unsolved.
	EConstraintSolverResult solve();

	// (Re)start solving the solution. This could be immediately find a solution (or absence of a solution) due to
	// initial propagation of constraints.
	// Note that all constraints/variables should be created and registered at this point.
	EConstraintSolverResult startSolving();

	// Step forward the solver once. Precondition: StartSolving should have been called already.
	EConstraintSolverResult step();

	// Returns the current decision level of the solver. Each time the solver picks a candidate value, it increments the level.
	// If a contradiction occurs, it will decrement the level, backtracking up the search tree until the conflict is resolved.
	int getCurrentDecisionLevel() const { return m_decisionLevels.size(); }

	// Whether we've finished our initial pass of constraining each variable to the set of allowable values
	// that fit all constraints. After this point, the tree walk is performed.
	bool hasFinishedInitialArcConsistency() const { return m_initialArcConsistencyEstablished; }

	// Returns the current status (last return value of Step)
	EConstraintSolverResult getCurrentStatus() const { return m_currentStatus; }

	template<typename T>
	void addProgram(tuple<UProgramInstance, T>&& instance)
	{
		return addProgram(move(get<UProgramInstance>(instance)));
	}
	void addProgram(UProgramInstance&& instance);

	// Returns whether or not the given variable is solved.
	bool isSolved(VarID varID) const;

	// Returns the solved value for a single variable. Will assert if variable is not solved.
	// Note this returns the TRANSLATED value, not the internal (offset) value
	int getSolvedValue(VarID varID) const;

	// Returns whether a rule atom is currently true.
	bool isAtomTrue(AtomID atomID) const;

	// Returns the solution. Will assert if current state isn't Solved.
	hash_map<VarID, SolvedVariableRecord> getSolution() const;

	void debugSaveSolution(const wchar_t* filename) const;
	void debugAttemptSolution(const wchar_t* filename);

	// The random seed this solver was initialized with. Running with the same seed should reproduce the
	// same solution every time.
	int getSeed() const { return m_initialSeed; }

	// The random number generator. Strategies can use this to ensure deterministic results.
	RandomStreamType& getRandom() { return m_random; }

	// Returns a random float in 0-1 range, inclusive.
	template <typename T=float>
	T randomFloat()
	{
		uint32_t range = m_random.max() - m_random.min();
		T r = T((m_random() - m_random.min()) / T(range));
		vxy_sanity(r >= 0.0);
		vxy_sanity(r <= 1.0);
		return r;
	}

	// Returns a random float in the given range, inclusive.
	template <typename T>
	T randomRangeFloat(T minVal, T maxVal)
	{
		return randomFloat<T>() * (maxVal - minVal) + minVal;
	}

	// Returns a random int in the given range, inclusive.
	int32_t randomRange(int32_t minVal, int32_t maxVal)
	{
		int outRange = maxVal - minVal;
		if (outRange == 0)
		{
			return minVal;
		}
		return int(randomFloat<float>() * outRange + minVal);
	}

	// Get the decision making heuristic
	const vector<shared_ptr<ISolverDecisionHeuristic>>& getDecisionHeuristics() { return m_heuristicStack; }

	// Maps FVarID to the decision level that was variable was chosen on, or 0.
	const vector<uint32_t>& getVariableToDecisionLevelMap() const { return m_variableToDecisionLevel; }

	// Gets the level the variable was chosen for decision, or 0 if not yet chosen.
	SolverDecisionLevel getDecisionLevelForVariable(VarID varID) const
	{
		vxy_assert(varID.isValid());
		return m_variableToDecisionLevel[varID.raw()];
	}

	// Gets the decision level where this timestamp occured.
	SolverDecisionLevel getDecisionLevelForTimestamp(SolverTimestamp time) const;

	// Whether we're in a new descent. This is true after we've restarted, until we hit a conflict.
	bool isInNewDescent() const { return m_newDescentAfterRestart; }

	//
	// Variable API
	//

	// Make a variable with a specified set of potential values. The domain of the variable is implicit based on the
	// potential values passed in.
	VarID makeVariable(const wstring& varName, const vector<int>& potentialValues);

	// Make a variable with an explicit domain, and optional set of potential values within that domain.
	// If the potential values are not specified, then the variable's potential values will initially be the entire domain.
	VarID makeVariable(const wstring& varName, const SolverVariableDomain& domain, const vector<int>& potentialValues = {});

	// Utility function to make a boolean variable
	VarID makeBoolean(const wstring& varName)
	{
		return makeVariable(varName, SolverVariableDomain(0,1));
	}

	// Create a graph of variables for the associated topology
	// Returned reference can be upcast to shared_ptr<ITopologyInstance<FVarID>>
	shared_ptr<TTopologyVertexData<VarID>> makeVariableGraph(const wstring& dataName, const shared_ptr<ITopology>& topology, const SolverVariableDomain& variableDomain, const wstring& namePrefix)
	{
		auto output = make_shared<TTopologyVertexData<VarID>>(topology, VarID::INVALID, dataName);
		fillVariableGraph(output, variableDomain, namePrefix);
		return output;
	}

	// Fill in an already-instantiated graph with variables
	void fillVariableGraph(const shared_ptr<TTopologyVertexData<VarID>>& data, const SolverVariableDomain& variableDomain, const wstring& namePrefix)
	{
		shared_ptr<ITopology> graph = data->getSource();
		for (int i = 0; i < graph->getNumVertices(); ++i)
		{
			wstring varName = namePrefix + graph->vertexIndexToString(i);
			VarID varID = makeVariable(varName, variableDomain);
			data->set(i, varID);

			m_variableToGraphs[varID.raw()].push_back(m_graphs.size());
		}

		if (!contains(m_graphs.begin(), m_graphs.end(), data->getSource()))
		{
			m_graphs.push_back(data->getSource());
		}
	}

	// Initialize a variable's potential values. Can only be called before solving.
	void setInitialValues(VarID varID, const vector<int>& potentialValues);

	// Get the database used to store current variable state and the assignment trail
	SolverVariableDatabase* getVariableDB() { return &m_variableDB; }
	const SolverVariableDatabase* getVariableDB() const { return &m_variableDB; }

	// Get the TRANSLATED (not internal) potential values of a given variable.
	vector<int> getPotentialValues(VarID varID) const;

	// Get the name for a given variable
	const wstring& getVariableName(VarID varID) const;

	// Get the external (TRANSLATED) domain for the variable
	virtual const SolverVariableDomain& getDomain(VarID varID) const override
	{
		vxy_assert(varID.isValid());
		return m_variableDomains[varID.raw()];
	}

	// Add a watcher for when a variable changes. The Sink will be called whenever the variable changes (determined by WatchType).
	// Note that watchers are NOT triggered during backtracking: only when a decision is made or propagated.
	WatcherHandle addVariableWatch(VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink);
	// Add a watcher for when a variable becomes no longer any of the input values.
	// Note this is can be less efficient than AddVariableWatch, but is useful if you only want to watch a small
	// subset of values.
	WatcherHandle addVariableValueWatch(VarID varID, const ValueSet& watchValues, IVariableWatchSink* sink);
	/** Disable a given watch, marking it to be reenabled if we backtrack to this point. */
	void disableWatcherUntilBacktrack(WatcherHandle handle, VarID variable, IVariableWatchSink* sink);
	// Remove a watch, given the handle of the watcher
	void removeVariableWatch(VarID varID, WatcherHandle handle, IVariableWatchSink* sink);

	//
	// Constraint API
	//

	// Helper functions for creating constraints
	class ClauseConstraint& clause(const vector<SignedClause>& clauses);
	class ClauseConstraint& nogood(const vector<SignedClause>& clauses);
	class IffConstraint& iff(const SignedClause& head, const vector<SignedClause>& body);
	class AllDifferentConstraint& allDifferent(const vector<VarID>& variables, bool useWeakPropagation = false);
	class TableConstraint& table(const shared_ptr<struct TableConstraintData>& data, const vector<VarID>& variables);
	class OffsetConstraint& offset(VarID sum, VarID term, int delta);
	class InequalityConstraint& inequality(VarID leftHandSide, EConstraintOperator op, VarID rightHandSide);
	class CardinalityConstraint& cardinality(const vector<VarID>& variables, const hash_map<int, tuple<int, int>>& cardinalitiesForValues);
	class SumConstraint& sum(const VarID sum, const vector<VarID>& vars);
	class DisjunctionConstraint& disjunction(IConstraint* consA, IConstraint* consB);

	//
	// Create a constraint across an entire graph. All vertices in the graph will share the same constraints.
	//
	// To refer to vertices at relative locations within the graph, use FTopologyLink instead of FVarID, or FSignedGraphClause instead of FSignedClause.
	// You can also refer to a variable outside the graph, by passing FVarID/FSignedClause as normal.
	//
	// Example 1: Making an inequality where all vertices in graph should be greater than the vertex "below" them:
	//		Solver.MakeGraphConstraint<FInequalityConstraint>(MyGraph,
	//			FTopologyLink::Self,
	//			EConstraintOperator::GreaterThan,
	//			FTopologyLink(FGridTopology::Directions::Down, 1)
	//		);
	//
	// Example 2: Making an inequality where all vertices in the graph should be greater than another variable's value:
	//		FVarID OtherVar = Solver.MakeVariable(TEXT("OtherVar"), Domain);
	//		Solver.MakeGraphConstraint<FInequalityConstraint>(MyGraph,
	//			FTopologyLink::Self,
	//			EConstraintOperator::GreaterThan,
	//			OtherVar
	//		);
	//
	template <typename ConstraintType, typename... ArgsType>
	GraphConstraintID makeGraphConstraint(const shared_ptr<ITopology>& graph, ArgsType&&... args)
	{
		bool anyMade = false;

		auto graphConstraintData = make_shared<TTopologyVertexData<IConstraint*>>(graph, nullptr);

		for (int i = 0; i < graph->getNumVertices(); ++i)
		{
			IConstraint* cons = maybeInstanceGraphConstraint<ConstraintType>(graph, i, forward<ArgsType>(args)...);
			if (cons != nullptr)
			{
				anyMade = true;
				graphConstraintData->set(i, cons);
			}
		}

		if (!anyMade)
		{
			return GraphConstraintID::INVALID;
		}

		m_graphConstraints.push_back(graphConstraintData);
		return GraphConstraintID(m_graphConstraints.size());
	}

	template <typename ConstraintType, typename Topo, typename... ArgsType>
	GraphConstraintID makeGraphConstraint(const shared_ptr<Topo>& graph, ArgsType&&... args)
	{
		return makeGraphConstraint<ConstraintType>(ITopology::adapt(graph), forward<ArgsType>(args)...);
	}

	// Generic method for constructing a constraint.
	// Usage: Solver.MakeConstraint<FMyConstraintType>(ConstructorParam1, ConstructorParam2, ...);
	template <typename T, typename... ArgsType>
	T& makeConstraint(ArgsType&&... args)
	{
		return *static_cast<T*>(registerConstraint(T::Factory::construct(ConstraintFactoryParams(*this), forward<ArgsType>(args)...)));
	}

	// Access the database for creating ASP-style rules
	RuleDatabase& getRuleDB();

	// Return all the variables that a given constraint refers to
	const vector<VarID>& getVariablesForConstraint(const IConstraint* constraint) const
	{
		return m_constraintArcs[constraint->getID()];
	}

	// Called by constraints to mark themselves for propagation in the constraint propagation queue.
	// This allows constraints to defer their propagation until after all variable propagation has finished,
	// which can be more efficient if the constraint involves a large number of variables.
	void queueConstraintPropagation(const IConstraint* constraint);

	// Used by constraint factories
	inline int getNextConstraintID() const { return m_constraints.size(); }

protected:
	struct QueuedVariablePropagation
	{
		QueuedVariablePropagation(IConstraint* inConstraint, VarID inVariable, SolverTimestamp inTimestamp)
			: constraint(inConstraint)
			, variable(inVariable)
			, timestamp(inTimestamp)
		{
		}

		IConstraint* constraint;
		VarID variable;
		SolverTimestamp timestamp;
	};

	bool finalizeRules();

	bool simplify();

	// Unify all variable domains so that they start at the same base value
	vector<VarID> unifyVariableDomains(const vector<VarID>& vars, int* outNewMinDomain = nullptr);

	// Return a variable representing V in a different (broader) domain. Creates one if it doesn't already exist.
	VarID getOrCreateOffsetVariable(VarID varID, int minDomain, int maxDomain);

	// Called whenever a potential value is removed from a variable. Triggers propagation of this to any
	// constraints that involve this variable.
	void notifyVariableModification(VarID variable, IConstraint* constraint);

	IConstraint* registerConstraint(IConstraint* constraint);

	bool propagate();
	bool propagateVariables();

	bool emptyVariableQueue();
	bool emptyConstraintQueue();

	void backtrackUntilDecision(SolverDecisionLevel decisionLevel, bool isRestart = false);

	ClauseConstraint* learn(const vector<Literal>& learnedClause, const ConstraintGraphRelationInfo* relationInfo);
	bool promoteConstraintToGraph(ClauseConstraint& constraint, int& startVertex);
	bool createLiteralsForGraphPromotion(const ClauseConstraint& promotingCons, int destVertex, ConstraintGraphRelationInfo& outRelInfo, vector<Literal>& outLits) const;

	void markConstraintActivity(ClauseConstraint& constraint, bool recomputeLBD = true);
	void purgeConstraints();

	wstring clauseConstraintToString(const ClauseConstraint& constraint) const;
	wstring literalArrayToString(const vector<Literal>& clauses) const;
	wstring literalToString(const Literal& lit) const;
	wstring valueSetToString(VarID varID, const ValueSet& vals) const;

	/** Called before we start our next solve decision */
	void startNextDecision();

	/** Ask the strategies for the next variable/value we should try next */
	bool getNextDecisionLiteral(VarID& variable, ValueSet& value);

	void findDuplicateClauses();
	void sanityCheckGraphClauses();
	void sanityCheckValid();

	// Given a timestamp, return the reason the variable was changed at that timestamp.
	// Asserts if the timestamp represents a decision point.
	vector<Literal> getExplanationForModification(SolverTimestamp modificationTime) const;
	void sanityCheckExplanation(SolverTimestamp modificationTime, const vector<Literal>& explanation) const;

	inline SolverTimestamp getTimestampForCurrentDecisionLevel() const
	{
		return m_decisionLevels.back().modificationIndex;
	}

	inline SolverTimestamp getTimestampForDecisionLevel(SolverDecisionLevel level) const
	{
		return m_decisionLevels[level - 1].modificationIndex;
	}

	inline bool isVariableInGraph(VarID varID, int graphID) const
	{
		vxy_assert(varID.isValid());
		vxy_assert(graphID >= 0);
		vxy_assert(graphID < m_graphs.size());

		const vector<uint32_t>& varGraphs = m_variableToGraphs[varID.raw()];
		return contains(varGraphs.begin(), varGraphs.end(), graphID);
	}

	// Current status - updated every time Step() is called
	EConstraintSolverResult m_currentStatus = EConstraintSolverResult::Uninitialized;
	// storage for all variables and backtracking data
	SolverVariableDatabase m_variableDB;

	// All constraints that have been learned through conflict analysis that may be purged
	vector<ClauseConstraint*> m_temporaryLearnedConstraints;
	// Learned constraints that will never be purged
	vector<ClauseConstraint*> m_permanentLearnedConstraints;
	// Hashset of constraints - used to prevent duplicates during graph promotion
	hash_set<ClauseConstraint*, ConstraintHashFuncs> m_learnedConstraintSet;
	// Queue of constraints ready to be propagated across graphs, mapped to the next vertex index to be processed.
	hash_map<ClauseConstraint*, int> m_constraintsToPromoteToGraph;

	// State for a given variable+value decision on the search stack
	struct DecisionRecord
	{
		// The timestamp prior to this decision being made
		SolverTimestamp modificationIndex;
		// The choice variable
		VarID variable;
	};

	// The decision stack.
	vector<DecisionRecord> m_decisionLevels;

	// Describes a watch that needs to be restored if/when we backtrack before a decision level
	struct DisabledWatchMarker
	{
		SolverDecisionLevel level;
		VarID var;
		WatcherHandle handle;
		IVariableWatchSink* sink;
	};

	vector<DisabledWatchMarker> m_disabledWatchMarkers;

	// bit for whether a given variable is currently in propagation queue
	ValueSet m_variableQueuedSet;

	// For a given variable + domain, the created offset variable representing Var in that domain
	hash_map<tuple<VarID, int, int>, VarID> m_offsetVariableMap;
	// Map from an offset variable to the source variable and offset from that source
	hash_map<VarID, VarID> m_offsetVariableToSource;

	// All constraints in the system
	vector<unique_ptr<IConstraint>> m_constraints;
	// Whether the constraint at given index is a child constraint (i.e. wrapped by an outer constraint)
	// Child constraints rely on their parents to initialize.
	vector<bool> m_constraintIsChild;
	// Constraints that need to be notified when we backtrack
	vector<IBacktrackingSolverConstraint*> m_backtrackingConstraints;

	// For each constraint (indexed by Constraint->ID), the list of variables involved in the constraint.
	vector<vector<VarID>> m_constraintArcs;
	// domains for variables, for translation
	vector<SolverVariableDomain> m_variableDomains;
	// For each variable, the decision level where it was chosen (or 0 if not yet chosen)
	vector<uint32_t> m_variableToDecisionLevel;
	// Graphs that have been registered with the solver
	vector<shared_ptr<ITopology>> m_graphs;
	// Constraints created by graphs
	vector<shared_ptr<TTopologyVertexData<IConstraint*>>> m_graphConstraints;
	// For each variable, indices of graphs that the variable is associated with
	vector<vector<uint32_t>> m_variableToGraphs;

	// The watcher for each variable
	vector<unique_ptr<IVariablePropagator>> m_variablePropagators;

	// Decision heuristic stack
	vector<shared_ptr<ISolverDecisionHeuristic>> m_heuristicStack;
	bool m_heuristicsInitialized = false;

	// Policy for determining when we restart
	RestartPolicyType m_restartPolicy;
	// Whether we are in a new descent after restarting. Cleared as soon as we hit a conflict.
	bool m_newDescentAfterRestart = false;

	// How often we should log decisions
	int m_decisionLogFrequency;

	// Incrementer for constraint activity
	float m_constraintConflictIncr = 1.0;
	// How many user-supplied constraints were provided
	int m_numUserConstraints = 0;

	// Queue of variable changes that need to be propagated to other constraints
	vector<QueuedVariablePropagation> m_variablePropagationQueue;
	// Prioritized constraint propagation queue. Maps to constraint ID.
	deque<int> m_constraintPropagationQueue;
	// Tracks whether a constraint is currently queued, by constraint ID
	ValueSet m_constraintQueuedSet;

	// Most recent watch sink that was triggered.
	// Reset to null on backtrack.
	IVariableWatchSink* m_lastTriggeredSink = nullptr;
	// Timestamp before we triggered the most recent sink
	SolverTimestamp m_lastTriggeredTs;
	// whether all constraints have set up initial arc-consistency
	bool m_initialArcConsistencyEstablished = false;

	// Random number generator
	int m_initialSeed;
	RandomStreamType m_random;

	vector<UProgramInstance> m_programInsts;
	unique_ptr<RuleDatabase> m_ruleDB;
	vector<variant<bool, Literal>> m_atomValues;

	ConflictAnalyzer m_analyzer;

	unique_ptr<class UnfoundedSetAnalyzer> m_unfoundedSetAnalyzer;

	mutable ConstraintSolverStats m_stats;
	shared_ptr<SolverDecisionLog> m_outputLog;
	wstring m_name;

	/////////////////////////////
	//
	// Handling automatic translation of graph arguments into concrete variables.
	// This allows AddGraphConstraint to take a e.g. shared_ptr<IGraphRelation> argument describing the relative offset
	// of a variable in a topology, and create constraints for each vertex in the topology by resolving the
	// the relation into a concrete VarID per vertex.
	//

	template <typename T, typename... ArgsType>
	T& makeConstraintForGraph(const ConstraintGraphRelationInfo& relationInfo, ArgsType&&... args)
	{
		return *static_cast<T*>(registerConstraint(T::Factory::construct(ConstraintFactoryParams(*this, relationInfo), forward<ArgsType>(args)...)));
	}

	// Record the graph relation for a variable/literal.
	template <typename T, typename R>
	void addRelation(ConstraintGraphRelationInfo& relationInfo, const TransformedGraphArgument<T, R>& arg)
	{
		vxy_assert(arg.relation == nullptr);
	}

	void addRelation(ConstraintGraphRelationInfo& relationInfo, const TransformedGraphArgument<VarID, VarID>& arg)
	{
		if (arg.relation) { relationInfo.addRelation(arg.value, arg.relation); }
	}

	void addRelation(ConstraintGraphRelationInfo& relationInfo, const TransformedGraphArgument<SignedClause, VarID>& arg)
	{
		if (arg.relation) { relationInfo.addRelation(arg.value.variable, arg.relation); }
	}

	void addRelation(ConstraintGraphRelationInfo& relationInfo, const TransformedGraphArgument<SignedClause, SignedClause>& arg)
	{
		if (arg.relation) { relationInfo.addRelation(arg.value.variable, arg.relation); }
	}

	// translate an argument
	template <typename T>
	auto translateGraphConsArgument(const T& arg, ConstraintGraphRelationInfo& relationInfo, bool& success)
	{
		auto translatedArg = GraphArgumentTransformer::transformGraphArgument(relationInfo.sourceGraphVertex, arg);
		if (translatedArg.isValid)
		{
			addRelation(relationInfo, translatedArg);
		}
		else
		{
			success = false;
		}
		return translatedArg.value;
	}

	template<>
	auto translateGraphConsArgument(const GraphConstraintID& id, ConstraintGraphRelationInfo& relationInfo, bool& success)
	{
		IConstraint* cons = nullptr;
		if (id == GraphConstraintID::INVALID)
		{
			success = false;
			return cons;
		}

		auto& graphCons = m_graphConstraints[id.raw()-1];
		cons = graphCons->get(relationInfo.sourceGraphVertex);

		if (cons == nullptr)
		{
			success = false;
		}
		return cons;
	}

	// specialization for arrays
	template <typename T>
	auto translateGraphConsArgument(const vector<T>& argArray, ConstraintGraphRelationInfo& relationInfo, bool& success)
	{
		using ElementType = decltype(translateGraphConsArgument(argArray[0], relationInfo, success));
		vector<ElementType> translatedArray;
		translatedArray.reserve(argArray.size());
		for (auto& arg : argArray)
		{
			auto translatedArg = translateGraphConsArgument(arg, relationInfo, success);
			translatedArray.push_back(translatedArg);
		}
		return translatedArray;
	}

	// Concatenate arguments passed into a tuple<>, so that the constructor can be invoked via apply().
	template <typename... TranslatedParams>
	auto concatGraphConsArgs(const std::tuple<TranslatedParams...>& params, ConstraintGraphRelationInfo& relationInfo, bool& success) { return params; }

	template <typename TranslatedParams, typename NextArg, typename... RemArgs>
	auto concatGraphConsArgs(const TranslatedParams& params, ConstraintGraphRelationInfo& relationInfo, bool& success, const NextArg& arg, RemArgs&&... remArgs)
	{
		return concatGraphConsArgs(
			std::tuple_cat(std::move(params), std::make_tuple(std::move(translateGraphConsArgument(arg, relationInfo, success)))),
			relationInfo, success, std::forward<RemArgs>(remArgs)...
		);
	}

	template <typename T, typename... ArgsType>
	IConstraint* maybeInstanceGraphConstraint(const shared_ptr<ITopology>& graph, uint32_t vertexIndex, ArgsType&&... args)
	{
		ConstraintGraphRelationInfo graphRelationInfo(graph, vertexIndex);

		IConstraint* out = nullptr;

		bool success = true;
		auto translatedArgsTuple = concatGraphConsArgs(std::tuple<>{}, graphRelationInfo, success, forward<ArgsType>(args)...);
		if (success)
		{
			std::apply([&](auto&&... unpackedParams) { out = &makeConstraintForGraph<T>(graphRelationInfo, unpackedParams...); }, translatedArgsTuple);
		}
		return out;
	}
};

} // namespace Vertexy