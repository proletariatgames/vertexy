// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "rules/UnfoundedSetAnalyzer.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

static constexpr bool LOG_SOURCE_CHANGES = false;

UnfoundedSetAnalyzer::UnfoundedSetAnalyzer(ConstraintSolver& solver)
    : m_solver(solver)
{
}

UnfoundedSetAnalyzer::~UnfoundedSetAnalyzer()
{
    for (auto& sink : m_sinks)
    {
        const BodyInfo* bodyInfo = m_solver.getRuleDB().getBody(sink->getBodyId());
        m_solver.getVariableDB()->removeVariableWatch(bodyInfo->lit.variable, sink->getHandle(), sink.get());
        sink.reset();
    }
    m_sinks.clear();
}

bool UnfoundedSetAnalyzer::initialize()
{
    auto db = m_solver.getVariableDB();
    RuleDatabase& ruleDB = m_solver.getRuleDB();

    m_atomData.resize(ruleDB.getNumAtoms());
    m_bodyData.resize(ruleDB.getNumBodies());

    m_unfoundedSet.setIndexSize(ruleDB.getNumAtoms());
    m_remainUnfoundedSet.setIndexSize(ruleDB.getNumAtoms());
    m_needsNewSourceQueue.setIndexSize(ruleDB.getNumAtoms());

    for (int bodyID = 0; bodyID < ruleDB.getNumBodies(); ++bodyID)
    {
        auto bodyInfo = ruleDB.getBody(bodyID);
        vxy_sanity(bodyID == bodyInfo->id);
        initializeBodySupports(bodyInfo);
    }

    emptySourcePropagationQueue();

    // Falsify any atoms that have no external support
    for (int atomIdx = 1; atomIdx < m_atomData.size(); ++atomIdx)
    {
        AtomID atomId(atomIdx);
        auto atomInfo = ruleDB.getAtom(atomId);
        if (atomInfo->scc < 0)
        {
            // TODO: pre-prune rule database to only atoms in SCC
            continue;
        }

        if (!hasValidSource(atomId) &&
            !db->excludeValues(atomInfo->equivalence, nullptr))
        {
            return false;
        }
    }

    // Watch bodies that we need to maintain external support
    for (int bodyIdx = 0; bodyIdx < m_bodyData.size(); ++bodyIdx)
    {
        const BodyInfo* bodyInfo = ruleDB.getBody(bodyIdx);
        if (!db->anyPossible(bodyInfo->lit))
        {
            continue;
        }

        // TODO: prune bodies that cannot support atoms outside of SCCs
        const bool canSupport = containsPredicate(bodyInfo->heads.begin(), bodyInfo->heads.end(),
            [&](const AtomInfo* headInfo)
            {
                return headInfo->scc >= 0;
            }
        );

        // We only need to watch bodies that refer to atoms that have cyclic dependencies.
        if (canSupport)
        {
            m_sinks.push_back(make_unique<Sink>(*this, bodyIdx));
            m_sinks.back()->setHandle(
                db->addVariableValueWatch(bodyInfo->lit.variable, bodyInfo->lit.values, m_sinks.back().get())
            );
        }
    }

    return true;
}

bool UnfoundedSetAnalyzer::Sink::onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle)
{
    m_outer.onBodyFalsified(m_bodyId);
    return true;
}

void UnfoundedSetAnalyzer::onBodyFalsified(int32_t bodyId)
{
    const BodyInfo* bodyInfo = m_solver.getRuleDB().getBody(bodyId);
    vxy_sanity(!m_solver.getVariableDB()->anyPossible(bodyInfo->lit));

    const BodyData& data = m_bodyData[bodyId];
    if (data.numWatching > 0)
    {
        // Add this to the queue, to be processed once propagation has hit fixpoint.
        m_falseBodyQueue.push_back(bodyInfo);
    }
}

void UnfoundedSetAnalyzer::onBacktrack()
{
    // If we're backtracking, all of this things that became false this step have been undone.
    vxy_sanity(!containsPredicate(m_falseBodyQueue.begin(), m_falseBodyQueue.end(),
        [&](auto bodyInfo)
        {
            return !m_solver.getVariableDB()->anyPossible(bodyInfo->lit);
        }
    ));
    m_falseBodyQueue.clear();
}

bool UnfoundedSetAnalyzer::analyze()
{
    //
    // Attempt to repair sources, potentially returning an unfounded set (atoms that have no non-cyclic supports)
    //

    m_unfoundedSet.clear();
    while (findUnfoundedSet(m_unfoundedSet))
    {
        if (!excludeUnfoundedSet(m_unfoundedSet))
        {
            return false;
        }
    }

    return true;
}

bool UnfoundedSetAnalyzer::findUnfoundedSet(AtomLookupSet& outSet)
{
    //
    // Go through all the body literals that became false, and remove them as valid supports from
    // all rule heads relying on them, adding those heads to the needsNewSourceQueue.
    //
    // We need to do this every time findUnfoundedSet is called, because falsifying an
    // unfounded and propagating in the solver may cause other bodies to become false.
    //

    for (auto it = m_falseBodyQueue.begin(), itEnd = m_falseBodyQueue.end(); it != itEnd; ++it)
    {
        const BodyInfo* bodyInfo = *it;
        const BodyData& bodyData = m_bodyData[bodyInfo->id];
        vxy_sanity(!m_solver.getVariableDB()->anyPossible(bodyInfo->lit));
        vxy_assert(bodyData.numWatching > 0);

        vxy_sanity(m_sourcePropagationQueue.empty());
        for (auto it2 = bodyInfo->heads.begin(), it2End = bodyInfo->heads.end(); it2 != it2End; ++it2)
        {
            const AtomInfo* headInfo = *it2;
            AtomData& headData = m_atomData[headInfo->id.value];

            if (headData.source == bodyInfo->id)
            {
                if constexpr (LOG_SOURCE_CHANGES)
                {
                    VERTEXY_LOG("**Atom %s lost source %s", headInfo->name.c_str(), m_solver.getVariableName(bodyInfo->lit.variable).c_str());
                }
                if (headData.sourceIsValid)
                {
                    headData.sourceIsValid = false;
                    m_sourcePropagationQueue.push_back(headInfo);
                }
                m_needsNewSourceQueue.add(headInfo->id);
            }
        }

        // tell all bodies holding the newly false atoms that they have lost a support, which
        // may propagate to heads losing support, and so on.
        emptySourcePropagationQueue();
    }

    m_falseBodyQueue.clear();
    vxy_sanity(m_sourcePropagationQueue.empty());

    //
    // Try to find a new support for everything in the needsNewSourceQueue.
    //
    while (!m_needsNewSourceQueue.empty())
    {
        AtomID atomId = m_needsNewSourceQueue.pop();
        const AtomInfo* atomInfo = m_solver.getRuleDB().getAtom(atomId);
        vxy_assert(atomInfo->scc >= 0);

        if (hasValidSource(atomId))
        {
            // received a source through source propagation
            continue;
        }

        if (!m_solver.getVariableDB()->anyPossible(atomInfo->equivalence))
        {
            continue;
        }

        // attempt to find a new source for this atom.
        // Otherwise, build an unfounded set and return it.
        if (!findNewSourceOrUnfoundedSet(atomInfo, outSet))
        {
            return true;
        }

        vxy_assert(hasValidSource(atomId));
    }

    vxy_assert(m_needsNewSourceQueue.empty());
    return false;
}

bool UnfoundedSetAnalyzer::findNewSourceOrUnfoundedSet(const AtomInfo* lostSourceAtom, AtomLookupSet& outSet)
{
    //
    // Given an atom that has lost its external source support, attempt to find a new source for it.
    // Otherwise, if no source can be found, outSet will be the set of atoms in the same SCC that this node
    // directly/indirectly requires for support, which themselves have no external support.
    //

    vxy_assert(!hasValidSource(lostSourceAtom->id));

    auto db = m_solver.getVariableDB();
    auto& rdb = m_solver.getRuleDB();

    auto& processQueue = outSet;
    processQueue.clear();
    processQueue.add(lostSourceAtom->id);

    m_remainUnfoundedSet.clear();

    bool needsSecondPass = false;
    for (int nextUnfounded = 0; nextUnfounded < processQueue.size(); ++nextUnfounded)
    {
        AtomID atomId = processQueue[nextUnfounded];
        const AtomInfo* headInfo = rdb.getAtom(atomId);
        vxy_assert(headInfo->scc == lostSourceAtom->scc);

        if (hasValidSource(atomId))
        {
            continue;
        }

        // TODO: can avoid looping twice by checking for a new source as well adding to outSet
        if (findNewSource(headInfo))
        {
            vxy_sanity(m_atomData[headInfo->id.value].sourceIsValid);
            if constexpr (LOG_SOURCE_CHANGES)
            {
                VERTEXY_LOG("**Atom %s found new source %s", headInfo->name.c_str(), m_solver.getVariableName(rdb.getBody(m_atomData[headInfo->id.value].source)->lit.variable).c_str());
            }
            // This head still has some (in)direct support outside of its SCC.
            // Propagate this new source assignment, which might add support for other heads in the unfounded set.
            m_sourcePropagationQueue.push_back(headInfo);
            emptySourcePropagationQueue();

            // other heads might've become supported due to propagation, so we need to rebuild the final list after.
            needsSecondPass = true;
            continue;
        }

        // No new source could be found, so this head remains potentially unfounded.
        // (propagation of another head later in the list might make it sourced - see needsSecondPass)
        m_remainUnfoundedSet.add(atomId);

        // For each body of this head in the SCC, add all the body's unsourced lits in our SCC to the processing queue.
        // (U := U ∪ (β+ ∩ (scc(p) ∩ S)) in the paper.)
        for (auto it = headInfo->supports.begin(), itEnd = headInfo->supports.end(); it != itEnd; ++it)
        {
            const BodyInfo* bodyInfo = *it;
            const BodyData& bodyData = m_bodyData[bodyInfo->id];

            // If there was a source body, findNewSource should've found one!
            vxy_sanity((bodyInfo->scc == headInfo->scc && bodyData.numUnsourcedLits > 0) || !db->anyPossible(bodyInfo->lit));

            if (!db->anyPossible(bodyInfo->lit))
            {
                continue;
            }

            // Get the unsourced atoms that form the body and add them to the unfounded set.
            auto& bodyLits = bodyInfo->body.values;
            for (auto itv = bodyLits.begin(), itvEnd = bodyLits.end(); itv != itvEnd; ++itv)
            {
                if (!itv->sign())
                {
                    // TODO: remove this check once database is pre-pruned
                    continue;
                }

                const AtomInfo* bodyLitInfo = rdb.getAtom(itv->id());
                if (bodyLitInfo->scc != bodyInfo->scc)
                {
                    continue;
                }

                if (!hasValidSource(itv->id()) && db->anyPossible(bodyLitInfo->equivalence))
                {
                    processQueue.add(itv->id());
                }
            }
        }
    }

    swap(outSet, m_remainUnfoundedSet);
    if (needsSecondPass)
    {
        // we sourced at least one item, which might've caused items processed earlier in the list to become
        // sourced as well. So we need to do a final pass to find all truly unsourced atoms.
        outSet.removeIf([&](AtomID atom)
        {
            return hasValidSource(atom);
        });
    }

    return outSet.empty();
}

bool UnfoundedSetAnalyzer::findNewSource(const AtomInfo* headInfo)
{
    auto db = m_solver.getVariableDB();
    vxy_assert(!hasValidSource(headInfo->id));

    // This head no longer has its non-cyclic support.
    // Get the bodies that support it, and see if any can act as a new support.
    for (auto it = headInfo->supports.begin(), itEnd = headInfo->supports.end(); it != itEnd; ++it)
    {
        const BodyInfo* bodyInfo = *it;
        const BodyData& bodyData = m_bodyData[bodyInfo->id];

        // Can support if this body is in a different SCC, or the same SCC but all its literals are sourced
        // by bodies outside the SCC.
        if (bodyInfo->scc != headInfo->scc || bodyData.numUnsourcedLits == 0)
        {
            if (db->anyPossible(bodyInfo->lit))
            {
                // Ok, this can act as a new source!
                setSource(headInfo, bodyInfo->id);
                return true;
            }
        }
    }

    vxy_assert(!hasValidSource(headInfo->id));
    return false;
}

bool UnfoundedSetAnalyzer::excludeUnfoundedSet(AtomLookupSet& set)
{
    auto& db = *m_solver.getVariableDB();
    auto& rdb = m_solver.getRuleDB();

    AssertionBuilder clause;
    while (!set.empty())
    {
        AtomID atomToFalsify = set.back();
        const AtomInfo* atomInfo = rdb.getAtom(atomToFalsify);
        if (db.anyPossible(atomInfo->equivalence))
        {
            if (clause.empty())
            {
                clause = getExternalBodies(set);
            }

            if (!createNogoodForAtom(atomToFalsify, set, clause))
            {
                return false;
            }

            if (!m_solver.propagateVariables())
            {
                return false;
            }
        }

        set.pop();
    }

    return true;
}

bool UnfoundedSetAnalyzer::createNogoodForAtom(AtomID atomToFalsify, const AtomLookupSet& unfoundedSet, const AssertionBuilder& clause)
{
    auto& rdb = m_solver.getRuleDB();

    const AtomInfo* atomInfo = rdb.getAtom(atomToFalsify);
    vector<Literal> assertionLiterals = clause.getAssertion(atomInfo->equivalence);

    // TODO: Graph relations
    auto learned = m_solver.learn(assertionLiterals, nullptr);

    // Need to set this in case the new constraint fails!
    m_solver.m_lastTriggeredSink = learned;

    if (!learned->initialize(m_solver.getVariableDB()))
    {
        return false;
    }

    if (!learned->makeUnit(m_solver.getVariableDB(), 0))
    {
        return false;
    }

    vxy_sanity(!m_solver.getVariableDB()->anyPossible(atomInfo->equivalence));
    return true;
}

UnfoundedSetAnalyzer::AssertionBuilder UnfoundedSetAnalyzer::getExternalBodies(const AtomLookupSet& unfoundedSet)
{
    vxy_assert(!unfoundedSet.empty());

    auto& db = *m_solver.getVariableDB();
    auto& rdb = m_solver.getRuleDB();

    AssertionBuilder clauseBuilder;

    // For each atom we're going to falsify...
    int scc = rdb.getAtom(unfoundedSet[0])->scc;
    for (int i = 0; i < unfoundedSet.size(); ++i)
    {
        const AtomInfo* atomInfo = rdb.getAtom(unfoundedSet[i]);
        vxy_assert(atomInfo->scc == scc);
        vxy_sanity(!hasValidSource(atomInfo->id));

        if (!db.anyPossible(atomInfo->equivalence))
        {
            // atom is already false, so we're not propagating it.
            continue;
        }

        // go through each possible external support for the atom that we're falsifying, and add it to the reason we're false.
        for (auto it = atomInfo->supports.begin(), itEnd = atomInfo->supports.end(); it != itEnd; ++it)
        {
            const BodyInfo* bodyInfo = *it;
            vxy_sanity(bodyInfo->scc != scc || m_bodyData[bodyInfo->id].numUnsourcedLits > 0 || !db.anyPossible(bodyInfo->lit));

            bool bExternal = true;
            if (bodyInfo->scc == scc)
            {
                // Check if all positive atoms in this body are outside of the set.
                // We only need to do this for bodies in same SCC; otherwise it's external by definition.
                for (auto itBody = bodyInfo->body.values.begin(), itBodyEnd = bodyInfo->body.values.end(); itBody != itBodyEnd; ++itBody)
                {
                    if (itBody->sign() && unfoundedSet.contains(itBody->id()))
                    {
                        bExternal = false;
                        break;
                    }
                }
            }

            if (bExternal)
            {
                clauseBuilder.add(bodyInfo->lit, getAssertingTime(bodyInfo->lit));
            }
        }
    }

    return clauseBuilder;
}

SolverTimestamp UnfoundedSetAnalyzer::getAssertingTime(const Literal& lit) const
{
    auto& db = *m_solver.getVariableDB();
    auto& stack = db.getAssignmentStack().getStack();

    SolverTimestamp time = db.getLastModificationTimestamp(lit.variable);
    while (time >= 0)
    {
        vxy_sanity(stack[time].variable == lit.variable);
        if (stack[time].previousValue.anyPossible(lit.values))
        {
            break;
        }
        time = stack[time].previousVariableAssignment;
    }

    return time;
}

void UnfoundedSetAnalyzer::initializeBodySupports(const BodyInfo* bodyInfo)
{
    auto db = m_solver.getVariableDB();
    auto& rdb = m_solver.getRuleDB();

    // initialize to the number of positive atoms in the body in the same SCC.
    // this count will be reduced for each positive atom that is externally supported.
    m_bodyData[bodyInfo->id].numUnsourcedLits = count_if(bodyInfo->body.values.begin(), bodyInfo->body.values.end(),
        [&](AtomLiteral bodyLit)
        {
            auto litAtom = rdb.getAtom(bodyLit.id());
            return litAtom->scc == bodyInfo->scc && bodyLit.sign();
        }
    );

    // if the body atom is already false, we can't ever act as a support.
    if (!db->anyPossible(bodyInfo->lit))
    {
        return;
    }

    // Add us as a source support for every head in a different SCC than us.
    for (auto it = bodyInfo->heads.begin(), itEnd = bodyInfo->heads.end(); it != itEnd; ++it)
    {
        AtomInfo* headInfo = *it;
        if (headInfo->scc != bodyInfo->scc && db->anyPossible(headInfo->equivalence))
        {
            setSource(headInfo, bodyInfo->id);
            m_sourcePropagationQueue.push_back(headInfo);
        }
    }
}

void UnfoundedSetAnalyzer::setSource(const AtomInfo* atomInfo, int32_t bodyId)
{
    vxy_assert(!hasValidSource(atomInfo->id));
    vxy_sanity(m_solver.getVariableDB()->anyPossible(atomInfo->equivalence));

    AtomData& atomData = m_atomData[atomInfo->id.value];

    if (atomData.source >= 0)
    {
        m_bodyData[atomData.source].numWatching--;
    }
    atomData.source = bodyId;
    atomData.sourceIsValid = true;
    m_bodyData[atomData.source].numWatching++;
}

void UnfoundedSetAnalyzer::emptySourcePropagationQueue()
{
    while (!m_sourcePropagationQueue.empty())
    {
        const AtomInfo* atomInfo = m_sourcePropagationQueue.back();
        m_sourcePropagationQueue.pop_back();

        const AtomData& atomData = m_atomData[atomInfo->id.value];
        if (atomData.sourceIsValid)
        {
            propagateSourceAssignment(atomInfo->id);
        }
        else
        {
            propagateSourceRemoval(atomInfo->id);
        }
    }
}

void UnfoundedSetAnalyzer::propagateSourceAssignment(AtomID id)
{
    auto db = m_solver.getVariableDB();

    const AtomInfo* atomInfo = m_solver.getRuleDB().getAtom(id);
    // for each body that includes this atom...
    for (auto it = atomInfo->positiveDependencies.begin(), itEnd = atomInfo->positiveDependencies.end(); it != itEnd; ++it)
    {
        const BodyInfo* bodyInfo = *it;
        if (bodyInfo->scc != atomInfo->scc)
        {
            continue;
        }

        BodyData& bodyData = m_bodyData[bodyInfo->id];

        // deduct the number of the body's lits that are unsourced
        vxy_sanity(bodyData.numUnsourcedLits > 0);
        bodyData.numUnsourcedLits--;

        // if all our lits are sourced, then we can act as a support for any heads referring to us.
        // then propagate to any bodies that head atom is a part of.
        if (bodyData.numUnsourcedLits == 0 && m_solver.getVariableDB()->anyPossible(bodyInfo->lit))
        {
            for (auto it2 = bodyInfo->heads.begin(), it2End = bodyInfo->heads.end(); it2 != it2End; ++it2)
            {
                const AtomInfo* headAtomInfo = *it2;
                if (!hasValidSource(headAtomInfo->id) && db->anyPossible(headAtomInfo->equivalence))
                {
                    if constexpr (LOG_SOURCE_CHANGES)
                    {
                        VERTEXY_LOG("**Atom %s gained source %s", headAtomInfo->name.c_str(), m_solver.getVariableName(bodyInfo->lit.variable).c_str());
                    }
                    setSource(headAtomInfo, bodyInfo->id);
                    m_sourcePropagationQueue.push_back(headAtomInfo);
                }
            }
        }
    }
}

void UnfoundedSetAnalyzer::propagateSourceRemoval(AtomID id)
{
    const AtomInfo* atomInfo = m_solver.getRuleDB().getAtom(id);
    // for each body that includes this atom...
    for (auto it = atomInfo->positiveDependencies.begin(), itEnd = atomInfo->positiveDependencies.end(); it != itEnd; ++it)
    {
        const BodyInfo* bodyInfo = *it;
        if (bodyInfo->scc != atomInfo->scc)
        {
            continue;
        }

        BodyData& bodyData = m_bodyData[bodyInfo->id];

        // increase the number of the body's lits that are unsourced
        bodyData.numUnsourcedLits++;
        if (bodyData.numUnsourcedLits == 1 && bodyData.numWatching > 0)
        {
            // we just became sourced->unsourced. tell our heads that they are no longer supported,
            // then propagate that to any bodies that head atom is part of.
            for (auto it2 = bodyInfo->heads.begin(), it2End = bodyInfo->heads.end(); it2 != it2End; ++it2)
            {
                const AtomInfo* headAtomInfo = *it2;
                AtomData& atomData = m_atomData[headAtomInfo->id.value];
                if (atomData.source == bodyInfo->id && atomData.sourceIsValid)
                {
                    if constexpr (LOG_SOURCE_CHANGES)
                    {
                        VERTEXY_LOG("**Atom %s lost source %s", headAtomInfo->name.c_str(), m_solver.getVariableName(bodyInfo->lit.variable).c_str());
                    }
                    atomData.sourceIsValid = false;
                    m_sourcePropagationQueue.push_back(headAtomInfo);
                }
            }
        }
    }
}

bool UnfoundedSetAnalyzer::hasValidSource(AtomID id) const
{
    const AtomInfo* headAtomInfo = m_solver.getRuleDB().getAtom(id);
    return m_atomData[headAtomInfo->id.value].sourceIsValid;
}

vector<Literal> UnfoundedSetAnalyzer::AssertionBuilder::getAssertion(const Literal& assertingLiteral) const
{
    vector<Literal> out;
    out.reserve(m_entries.size());

    out.push_back(assertingLiteral.inverted());
    SolverTimestamp uipTime = -1;

    for (auto it = m_entries.begin(), itEnd = m_entries.end(); it != itEnd; ++it)
    {
        const tuple<Literal, SolverTimestamp>& entry = *it;

        int foundIdx = indexOfPredicate(out.begin(), out.end(), [&](auto& existing)
        {
           return existing.variable == get<Literal>(entry).variable;
        });

        if (foundIdx < 0)
        {
            foundIdx = out.size();
            out.push_back(get<Literal>(entry));
        }
        else if (foundIdx == 0)
        {
            out[0].values.include(get<Literal>(entry).values.excluding(assertingLiteral.values));
        }
        else
        {
            out[foundIdx].values.include(get<Literal>(entry).values);
        }
        vxy_sanity(!out[foundIdx].values.isZero());
        vxy_sanity(out[foundIdx].values.indexOf(false) >= 0);

        if (get<SolverTimestamp>(entry) > uipTime)
        {
            // put UIP literal in second position
            // (this is assumed for clause propagation)
            uipTime = get<SolverTimestamp>(entry);
            swap(out[foundIdx], out[1]);
        }
    }

    return out;
}
