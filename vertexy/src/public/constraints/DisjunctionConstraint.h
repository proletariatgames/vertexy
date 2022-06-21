// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "IBacktrackingSolverConstraint.h"
#include "IConstraint.h"
#include "variable/CommittableVariableDatabase.h"

namespace Vertexy
{

/** enforce that Head is one of HeadValues if and only if InnerCons is satisfied. */
class DisjunctionConstraint : public IBacktrackingSolverConstraint, public ICommittableVariableDatabaseOwner
{
public:
	DisjunctionConstraint(const ConstraintFactoryParams& params, IConstraint* innerConsA, IConstraint* innerConsB);

	struct DisjunctionFactory
	{
		static DisjunctionConstraint* construct(const ConstraintFactoryParams& params, IConstraint* innerConsA, IConstraint* innerConsB);
	};
	using Factory = DisjunctionFactory;

	// IConstraint
	virtual EConstraintType getConstraintType() const override { return EConstraintType::Disjunction; }
	virtual vector<VarID> getConstrainingVariables() const override;
	virtual bool initialize(IVariableDatabase* db) override;
	virtual bool onVariableNarrowed(IVariableDatabase* db, VarID variable, const ValueSet& previousValue, bool& removeWatch) override;
	virtual bool propagate(IVariableDatabase* db) override;
	virtual void reset(IVariableDatabase* db) override;
	virtual bool checkConflicting(IVariableDatabase* db) const override;
	virtual void explain(const NarrowingExplanationParams& params, vector<Literal>& outExplanation) const override;
	virtual void backtrack(const IVariableDatabase* db, SolverDecisionLevel level) override;

	// ICommittableVariableDatabaseOwner
	virtual void committableDatabaseQueueRequest(const CommittableVariableDatabase& db, IConstraint* cons) override;
	virtual WatcherHandle committableDatabaseAddWatchRequest(const CommittableVariableDatabase& db, VarID varID, EVariableWatchType watchType, IVariableWatchSink* sink) override;
	virtual WatcherHandle committableDatabaseAddValueWatchRequest(const CommittableVariableDatabase& db, VarID varID, const ValueSet& values, IVariableWatchSink* sink) override;
	virtual void committableDatabaseDisableWatchRequest(const CommittableVariableDatabase& db, WatcherHandle handle, VarID variable, IVariableWatchSink* sink) override;
	virtual void committableDatabaseRemoveWatchRequest(const CommittableVariableDatabase& db, VarID varID, WatcherHandle handle, IVariableWatchSink* sink) override;
	virtual ExplainerFunction committableDatabaseWrapExplanation(const CommittableVariableDatabase& db, const ExplainerFunction& innerExpl) override;
	virtual void committableDatabaseContradictionFound(const CommittableVariableDatabase& db, VarID varID, IConstraint* source, const ExplainerFunction& explainer) override;
	virtual void committableDatabaseConstraintSatisfied(const CommittableVariableDatabase& db, IConstraint* constraint) override;

protected:
	inline CommittableVariableDatabase createCommittableDB(IVariableDatabase* db, int innerConsIndex);
	bool forwardVariableNarrowed(IVariableDatabase* db, IVariableWatchSink* innerSink, int innerConsIndex, VarID var, const ValueSet& previousValue, bool& removeHandle);
	void explainInner(const NarrowingExplanationParams& params, int innerConsIndex, const ExplainerFunction& innerExpl, vector<Literal>& outExplanation) const;
	bool markUnsat(const CommittableVariableDatabase& cdb, int innerConsIndex, VarID contradictingVar=VarID::INVALID, const ExplainerFunction& innerExpl = nullptr);

	IConstraint* m_innerCons[2];

	class SinkWrapper : public IVariableWatchSink
	{
	public:
		SinkWrapper(DisjunctionConstraint* owner, IVariableWatchSink* sink, int innerConsIndex)
			: owner(owner)
			, wrappedSink(sink)
			, innerConsIndex(innerConsIndex)
		{
		}

		virtual bool onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle) override
		{
			return owner->forwardVariableNarrowed(db, wrappedSink, innerConsIndex, var, previousValue, removeHandle);
		}

		virtual IConstraint* asConstraint() override { return owner; }

		vector<tuple<WatcherHandle, VarID>> handles;
	private:
		DisjunctionConstraint* owner;
		IVariableWatchSink* wrappedSink;
		int innerConsIndex;
	};

	hash_map<IVariableWatchSink*, unique_ptr<SinkWrapper>> m_sinkWrappers[2];

	struct UnsatInfo
	{
		UnsatInfo() { reset(); }

		bool isUnsat() const { return level >= 0; }
		void markUnsat(SolverDecisionLevel unsatLevel, const vector<Literal>& unsatExplanation)
		{
			vxy_assert(level < 0);
			vxy_assert(unsatLevel >= 0);
			level = unsatLevel;
			explanation = unsatExplanation;
		}

		void reset()
		{
			level = -1;
			explanation.clear();
		}

		SolverDecisionLevel level;
		vector<Literal> explanation;
	};
	UnsatInfo m_unsatInfo[2];
	SolverDecisionLevel m_fullySatLevel[2];
	bool m_constraintQueued[2];
	SolverTimestamp m_lastPropagation[2];
};

} // namespace Vertexy