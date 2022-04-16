// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include <assert.h>

#include "ConstraintTypes.h"
#include "RuleDatabase.h"
#include "ds/FastLookupSet.h"
#include "rules/RuleTypes.h"
#include "variable/IVariableWatchSink.h"

namespace Vertexy
{

// In charge of tracking rule heads' external (non-cyclic) supports.
// Once a rule head only has cyclic support, we can assign it to false.
//
// This is largely based on the paper "Conflict-driven answer set solving: From theory to practice", Kaufmann et. al.
// https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.303.5&rep=rep1&type=pdf
class UnfoundedSetAnalyzer final
{
public:
    explicit UnfoundedSetAnalyzer(ConstraintSolver& solver);
    ~UnfoundedSetAnalyzer();

    bool initialize();
    bool analyze();
    void onBacktrack();

protected:
    using BodyInfo = RuleDatabase::BodyInfo;
    using AtomInfo = RuleDatabase::AtomInfo;

    struct AtomIDToIndex
    {
        int32_t operator()(AtomID id) { return id.value; }
    };
    using AtomLookupSet = TFastLookupSet<AtomID, true, uint32_t, AtomIDToIndex>;

    struct AtomData
    {
        // the id of the last body that acted as external support for us.
        int32_t source = -1;
        // whether the above body is still a valid external support.
        bool sourceIsValid = false;
    };

    struct BodyData
    {
        // the number of atoms that are watching us for support.
        int numWatching = 0;
        // the number of literals in the body that don't currently have external support.
        // if zero, this body can act as external support for any of its heads.
        int numUnsourcedLits = 0;
    };

    class Sink : public IVariableWatchSink
    {
    public:
        Sink(UnfoundedSetAnalyzer& outer, int32_t bodyId) : m_outer(outer), m_handle(INVALID_WATCHER_HANDLE), m_bodyId(bodyId) {}
        virtual bool onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle) override;

        WatcherHandle getHandle() const { return m_handle; }
        void setHandle(WatcherHandle inHandle) { m_handle = inHandle; }

        int32_t getBodyId() const { return m_bodyId; }

    protected:
        UnfoundedSetAnalyzer& m_outer;
        WatcherHandle m_handle;
        int32_t m_bodyId;
    };

    // Utility to build a clause (vector of literals) where multiple literals might refer to the same variable.
    class AssertionBuilder
    {
    public:
        AssertionBuilder()
        {
        }

        bool empty() const { return m_entries.empty(); }
        void add(const Literal& lit, SolverTimestamp assertingTime)
        {
            m_entries.push_back(make_tuple(lit, assertingTime));
        }

        vector<Literal> getAssertion(const Literal& assertingLiteral) const;

    protected:
        vector<tuple<Literal, SolverTimestamp>> m_entries;
    };

    ConstraintSolver& m_solver;

    void initializeBodySupports(const BodyInfo* bodyInfo);

    void setSource(const AtomInfo* info, int32_t bodyId);
    void emptySourcePropagationQueue();

    void onBodyFalsified(int32_t bodyId);
    bool findNewSource(const AtomInfo* headInfo);

    bool findUnfoundedSet(AtomLookupSet& outSet);
    bool findNewSourceOrUnfoundedSet(const AtomInfo* lostSourceAtom, AtomLookupSet& outSet);

    bool excludeUnfoundedSet(AtomLookupSet& set);
    bool createNogoodForAtom(AtomID atomToFalsify, const AtomLookupSet& unfoundedSet, const AssertionBuilder& clause);
    AssertionBuilder getExternalBodies(const AtomLookupSet& unfoundedSet);
    SolverTimestamp getAssertingTime(const Literal& lit) const;

    inline bool hasValidSource(AtomID id) const;

    void propagateSourceAssignment(AtomID id);
    void propagateSourceRemoval(AtomID id);

    vector<AtomData> m_atomData;
    vector<BodyData> m_bodyData;
    vector<const BodyInfo*> m_falseBodyQueue;
    vector<const AtomInfo*> m_sourcePropagationQueue;
    vector<unique_ptr<Sink>> m_sinks;
    AtomLookupSet m_needsNewSourceQueue;
    AtomLookupSet m_unfoundedSet;
    AtomLookupSet m_remainUnfoundedSet;
};

} // namespace Vertexy