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
    constexpr static int32_t SENTINEL = -1;

    struct AtomData
    {
        // the solver's variable+value corresponding to this atom
        Literal* lit;
        // the ptr offset of the last body that acted as external support for us.
        int32_t source;
        // whether the above body is still a valid external support.
        uint32_t sourceIsValid : 1;
        // whether we're marked as in the unfounded set
        uint32_t inUnfoundedSet : 1;
        // ID of the strongly-connected component we belong to
        uint32_t scc : 28;
        // offset into data[] where dependencies begin.
        int32_t depsOffset;
        // Data array, including both supports (bodies that can make us true) and *positive* dependencies in the same SCC.
        // The array first specifies all supports, with a -1 sentinel marking the end. Then all dependencies are listed,
        // again with a -1 sentinel marking the end.
        int32_t data[0];
    };

    struct BodyData
    {
        // the variable associated with the body
        VarID variable;
        // the scc for the body
        int32_t scc;
        // the number of atoms that are watching us for support.
        int32_t numWatching;
        // the number of literals in the body that don't currently have external support.
        // if zero, this body can act as external support for any of its heads.
        int32_t numUnsourcedLits;
        // offset into data[] where the body literals begin.
        int32_t valuesOffset;
        // Data array, including both heads that will become true if we are satisfied, as well as the body's *positive* literal atoms in the same SCC.
        // The array first specifies all heads, with a -1 sentinel marking the end. Then all body literals are listed,
        // again with a -1 sentinel marking the end.
        int32_t data[0];
    };

    class Sink : public IVariableWatchSink
    {
    public:
        Sink(UnfoundedSetAnalyzer& outer, BodyData* body) : m_outer(outer), m_handle(INVALID_WATCHER_HANDLE), m_body(body) {}
        virtual bool onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle) override;

        WatcherHandle getHandle() const { return m_handle; }
        void setHandle(WatcherHandle inHandle) { m_handle = inHandle; }

        BodyData* getBody() const { return m_body; }

    protected:
        UnfoundedSetAnalyzer& m_outer;
        WatcherHandle m_handle;
        BodyData* m_body;
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

    template<typename T>
    class NodeIterator
    {
    public:
        NodeIterator(int32_t* start, uint8_t* base) : cur(start), base(base) {}
        NodeIterator& operator++()
        {
            ++cur;
            return *this;
        }
        operator bool() const
        {
            return *cur != SENTINEL;
        }
        T* operator*() const
        {
            vxy_sanity(*cur >= 0);
            return reinterpret_cast<T*>(base + *cur);
        }
    protected:
        int32_t* cur;
        uint8_t* base;
    };

    ConstraintSolver& m_solver;

    void initializeData(vector<int32_t>& outAtomOffsets, vector<int32_t>& outBodyOffsets);
    void initializeBodySupports(BodyData* body);

    void setSource(AtomData* atom, BodyData* body);
    void emptySourcePropagationQueue();

    void onBodyFalsified(BodyData* body);
    bool findNewSource(AtomData* head);

    bool findUnfoundedSet(vector<AtomData*>& outSet);
    bool findNewSourceOrUnfoundedSet(AtomData* lostSourceAtom, vector<AtomData*>& outSet);

    bool excludeUnfoundedSet(vector<AtomData*>& set);
    bool createNogoodForAtom(const AtomData* atomToFalsify, const vector<AtomData*>& unfoundedSet, const AssertionBuilder& clause);

    NodeIterator<BodyData> iterateAtomSupports(AtomData* atom)
    {
        return NodeIterator<BodyData>(atom->data, m_bodyBuffer.data());
    }
    NodeIterator<BodyData> iterateAtomPositiveDependencies(AtomData* atom)
    {
        return NodeIterator<BodyData>(&atom->data[atom->depsOffset], m_bodyBuffer.data());
    }
    NodeIterator<AtomData> iterateBodyHeads(BodyData* body)
    {
        return NodeIterator<AtomData>(body->data, m_atomBuffer.data());
    }
    NodeIterator<AtomData> iterateBodyPositiveLiterals(BodyData* body)
    {
        return NodeIterator<AtomData>(&body->data[body->valuesOffset], m_atomBuffer.data());
    }

    BodyData* getAtomSource(const AtomData* atom)
    {
        return atom->source >= 0 ? reinterpret_cast<BodyData*>(&m_bodyBuffer[atom->source]) : nullptr;
    }

    AssertionBuilder getExternalBodies(const vector<AtomData*>& unfoundedSet);
    SolverTimestamp getAssertingTime(const Literal& lit) const;

    void propagateSourceAssignment(AtomData* atom);
    void propagateSourceRemoval(AtomData* atom);

    vector<uint8_t> m_bodyBuffer;
    vector<uint8_t> m_atomBuffer;
    vector<Literal> m_atomLiterals;

    vector<BodyData*> m_falseBodyQueue;
    vector<AtomData*> m_sourcePropagationQueue;
    vector<unique_ptr<Sink>> m_sinks;
    vector<AtomData*> m_needsNewSourceQueue;
    vector<AtomData*> m_unfoundedSet;
    vector<AtomData*> m_remainUnfoundedSet;
};

} // namespace Vertexy