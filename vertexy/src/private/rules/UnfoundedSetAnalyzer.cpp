// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once
#include "rules/UnfoundedSetAnalyzer.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

static const SolverVariableDomain booleanVariableDomain(0, 1);
static const ValueSet FALSE_VALUE = booleanVariableDomain.getBitsetForValue(0);
static const ValueSet TRUE_VALUE = booleanVariableDomain.getBitsetForValue(1);

UnfoundedSetAnalyzer::UnfoundedSetAnalyzer(ConstraintSolver& solver)
    : m_solver(solver)
{
}

UnfoundedSetAnalyzer::~UnfoundedSetAnalyzer()
{
    for (auto& sink : m_sinks)
    {
        m_solver.getVariableDB()->removeVariableWatch(sink->getBody()->variable, sink->getHandle(), sink.get());
        sink.reset();
    }
    m_sinks.clear();
}

bool UnfoundedSetAnalyzer::initialize()
{
    auto db = m_solver.getVariableDB();

    vector<int32_t> atomOffsets, bodyOffsets;
    initializeData(atomOffsets, bodyOffsets);

    // Propagate initial non-cyclical supports to all atoms from external bodies
    for (int i = 0; i < bodyOffsets.size(); ++i)
    {
        auto body = reinterpret_cast<BodyData*>(&m_bodyBuffer[bodyOffsets[i]]);
        initializeBodySupports(body);

        // watch for the body to be falsified
        if (db->anyPossible(body->variable, TRUE_VALUE))
        {
            m_sinks.push_back(make_unique<Sink>(*this, body));
            m_sinks.back()->setHandle(
                db->addVariableValueWatch(body->variable, TRUE_VALUE, m_sinks.back().get())
            );
        }
    }
    // propagate initial body supports to any other bodies/atoms.
    emptySourcePropagationQueue();

    // Falsify any atoms that have no external support
    for (int i = 0; i < atomOffsets.size(); ++i)
    {
        auto atomData = reinterpret_cast<AtomData*>(&m_atomBuffer[atomOffsets[i]]);
        vxy_assert(atomData->scc > 0);

        if (!atomData->sourceIsValid &&
            !db->excludeValues(*atomData->lit, nullptr))
        {
            return false;
        }
    }

    return true;
}

void UnfoundedSetAnalyzer::initializeData(vector<int32_t>& outAtomOffsets, vector<int32_t>& outBodyOffsets)
{
    auto& rdb = m_solver.getRuleDB();

    auto visitBodyHeads = [](const RuleDatabase::BodyInfo* bodyInfo, auto&& callback)
    {
        for (RuleDatabase::AtomInfo* atomInfo : bodyInfo->heads)
        {
            if (atomInfo->isVariable())
            {
                callback(atomInfo);
            }
        }
    };

    auto visitBodyPositiveLits = [&](const RuleDatabase::BodyInfo* bodyInfo, auto&& callback)
    {
        for (AtomLiteral atomLit : bodyInfo->body.values)
        {
            if (atomLit.sign())
            {
                if (RuleDatabase::AtomInfo* atomInfo = rdb.getAtom(atomLit.id()); atomInfo->isVariable() &&
                    atomInfo->scc >= 0 && atomInfo->scc == bodyInfo->scc)
                {
                    callback(atomInfo);
                }
            }
        }
    };

    auto visitAtomSupports = [](const RuleDatabase::AtomInfo* atomInfo, auto&& callback)
    {
        for (RuleDatabase::BodyInfo* bodyInfo : atomInfo->supports)
        {
            if (bodyInfo->isVariable())
            {
                callback(bodyInfo);
            }
        }
    };

    auto visitAtomPositiveDeps = [](const RuleDatabase::AtomInfo* atomInfo, auto&& callback)
    {
        for (RuleDatabase::BodyInfo* bodyInfo : atomInfo->positiveDependencies)
        {
            if (bodyInfo->isVariable() &&
                atomInfo->scc >= 0 && bodyInfo->scc == atomInfo->scc)
            {
                callback(bodyInfo);
            }
        }
    };

    //
    // Determine required size for atom heap.
    //

    outAtomOffsets.clear();

    vector<AtomID> relevantAtoms;
    int32_t atomHeapSize = 0;

    vector<int32_t> atomMapping;
    atomMapping.resize(rdb.getNumAtoms(), -1);

    for (int i = 1; i < rdb.getNumAtoms(); ++i)
    {
        const RuleDatabase::AtomInfo* atomInfo = rdb.getAtom(AtomID(i));
        if (!atomInfo->isVariable() || atomInfo->scc < 0)
        {
            continue;
        }

        int numDeps = 0;
        visitAtomPositiveDeps(atomInfo, [&](auto) { ++numDeps; });

        int numSupports = 0;
        visitAtomSupports(atomInfo, [&](auto) { ++numSupports; });

        atomMapping[i] = relevantAtoms.size();
        relevantAtoms.push_back(AtomID(i));
        outAtomOffsets.push_back(atomHeapSize);

        constexpr int numSentinels = 2;
        atomHeapSize += sizeof(AtomData) + numDeps*sizeof(int32_t) + numSupports*sizeof(int32_t) + numSentinels*sizeof(int32_t);
    }
    m_atomBuffer.resize(atomHeapSize);
    m_atomLiterals.resize(relevantAtoms.size());

    //
    // Determine required size of body heap.
    //

    outBodyOffsets.clear();

    vector<int32_t> relevantBodies;
    int32_t bodyHeapSize = 0;

    vector<int32_t> bodyMapping;
    bodyMapping.resize(rdb.getNumBodies(), -1);

    for (int i = 0; i < rdb.getNumBodies(); ++i)
    {
        const RuleDatabase::BodyInfo* bodyInfo = rdb.getBody(i);
        if (!bodyInfo->isVariable())
        {
            continue;
        }

        const bool canSupport = containsPredicate(bodyInfo->heads.begin(), bodyInfo->heads.end(),
            [&](auto headInfo)
            {
                return headInfo->isVariable() && headInfo->scc >= 0;
            }
        );
        if (!canSupport)
        {
            continue;
        }

        int numHeads = 0;
        visitBodyHeads(bodyInfo, [&](auto) { ++numHeads; });
        int numTails = 0;
        visitBodyPositiveLits(bodyInfo, [&](auto) { ++numTails; });

        bodyMapping[i] = relevantBodies.size();
        relevantBodies.push_back(i);
        outBodyOffsets.push_back(bodyHeapSize);

        constexpr int numSentinels = 2;
        bodyHeapSize += sizeof(BodyData) + numHeads*sizeof(int32_t) + numTails*sizeof(int32_t) + numSentinels*sizeof(int32_t);
    }
    m_bodyBuffer.resize(bodyHeapSize);

    //
    // Create the atoms
    //

    for (int i = 0; i < relevantAtoms.size(); ++i)
    {
        AtomData* newAtom = new (&m_atomBuffer[outAtomOffsets[i]]) AtomData();
        const RuleDatabase::AtomInfo* atomInfo = rdb.getAtom(relevantAtoms[i]);

        m_atomLiterals[i] = atomInfo->equivalence;
        newAtom->lit = &m_atomLiterals[i];
        newAtom->source = -1;
        newAtom->sourceIsValid = false;
        newAtom->inUnfoundedSet = false;
        newAtom->scc = atomInfo->scc+1;

        int offs = 0;
        visitAtomSupports(atomInfo, [&](auto bodyInfo)
        {
            vxy_assert(bodyMapping[bodyInfo->id] >= 0);
            newAtom->data[offs++] = outBodyOffsets[bodyMapping[bodyInfo->id]];
        });

        newAtom->data[offs++] = SENTINEL;
        newAtom->depsOffset = offs;

        visitAtomPositiveDeps(atomInfo, [&](auto bodyInfo)
        {
            vxy_assert(bodyMapping[bodyInfo->id] >= 0);
            newAtom->data[offs++] = outBodyOffsets[bodyMapping[bodyInfo->id]];
        });

        newAtom->data[offs] = SENTINEL;
    }

    //
    // Create the bodies
    //

    for (int i = 0; i < relevantBodies.size(); ++i)
    {
        BodyData* newBody = new (&m_bodyBuffer[outBodyOffsets[i]]) BodyData();
        const RuleDatabase::BodyInfo* bodyInfo = rdb.getBody(relevantBodies[i]);
        newBody->variable = bodyInfo->lit.variable;
        newBody->scc = bodyInfo->scc+1;
        newBody->numWatching = 0;
        vxy_assert(newBody->variable.isValid());
        vxy_sanity(bodyInfo->lit.values == TRUE_VALUE);

        int offs = 0;
        visitBodyHeads(bodyInfo, [&](auto atomInfo)
        {
            vxy_assert(atomMapping[atomInfo->id.value] >= 0);
            newBody->data[offs++] = outAtomOffsets[atomMapping[atomInfo->id.value]];
        });

        newBody->data[offs++] = SENTINEL;
        newBody->valuesOffset = offs;

        int numLits = 0;
        visitBodyPositiveLits(bodyInfo, [&](auto atomInfo)
        {
            vxy_assert(atomMapping[atomInfo->id.value] >= 0);
            newBody->data[offs++] = outAtomOffsets[atomMapping[atomInfo->id.value]];
            ++numLits;
        });
        newBody->numUnsourcedLits = numLits;

        newBody->data[offs] = SENTINEL;
    }
}

bool UnfoundedSetAnalyzer::Sink::onVariableNarrowed(IVariableDatabase* db, VarID var, const ValueSet& previousValue, bool& removeHandle)
{
    m_outer.onBodyFalsified(m_body);
    return true;
}

void UnfoundedSetAnalyzer::onBodyFalsified(BodyData* body)
{
    vxy_sanity(!m_solver.getVariableDB()->anyPossible(body->variable, TRUE_VALUE));

    if (body->numWatching > 0)
    {
        // Add this to the queue, to be processed once propagation has hit fixpoint.
        m_falseBodyQueue.push_back(body);
    }
}

void UnfoundedSetAnalyzer::onBacktrack()
{
    // If we're backtracking, all of this things that became false this step have been undone.
    vxy_sanity(!containsPredicate(m_falseBodyQueue.begin(), m_falseBodyQueue.end(),
        [&](auto bodyInfo)
        {
            return !m_solver.getVariableDB()->anyPossible(bodyInfo->variable, TRUE_VALUE);
        }
    ));
    m_falseBodyQueue.clear();
}

bool UnfoundedSetAnalyzer::analyze()
{
    //
    // Attempt to repair sources, potentially returning an unfounded set (atoms that have no non-cyclic supports)
    //

    vxy_assert(m_unfoundedSet.empty());
    while (findUnfoundedSet(m_unfoundedSet))
    {
        if (!excludeUnfoundedSet(m_unfoundedSet))
        {
            for (auto atom : m_unfoundedSet)
            {
                vxy_assert(atom->inUnfoundedSet);
                atom->inUnfoundedSet = false;
            }
            m_unfoundedSet.clear();

            return false;
        }
    }

    return true;
}

bool UnfoundedSetAnalyzer::findUnfoundedSet(vector<AtomData*>& outSet)
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
        BodyData* bodyData = *it;
        vxy_sanity(!m_solver.getVariableDB()->anyPossible(bodyData->variable, TRUE_VALUE));
        vxy_assert(bodyData->numWatching > 0);

        vxy_sanity(m_sourcePropagationQueue.empty());
        for (auto it2 = iterateBodyHeads(bodyData); it2; ++it2)
        {
            AtomData* headData = *it2;
            if (getAtomSource(headData) == bodyData)
            {
                if (headData->sourceIsValid)
                {
                    headData->sourceIsValid = false;
                    m_sourcePropagationQueue.push_back(headData);
                }
                m_needsNewSourceQueue.push_back(headData);
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
        AtomData* atomData = m_needsNewSourceQueue.back();
        m_needsNewSourceQueue.pop_back();

        vxy_assert(atomData->scc != 0);

        if (atomData->sourceIsValid)
        {
            // received a source through source propagation
            continue;
        }

        if (!m_solver.getVariableDB()->anyPossible(*atomData->lit))
        {
            continue;
        }

        // attempt to find a new source for this atom.
        // Otherwise, build an unfounded set and return it.
        if (!findNewSourceOrUnfoundedSet(atomData, outSet))
        {
            return true;
        }

        vxy_assert(atomData->sourceIsValid);
    }

    vxy_assert(m_needsNewSourceQueue.empty());
    return false;
}

bool UnfoundedSetAnalyzer::findNewSourceOrUnfoundedSet(AtomData* lostSourceAtom, vector<AtomData*>& outSet)
{
    //
    // Given an atom that has lost its external source support, attempt to find a new source for it.
    // Otherwise, if no source can be found, outSet will be the set of atoms in the same SCC that this node
    // directly/indirectly requires for support, which themselves have no external support.
    //

    vxy_assert(!lostSourceAtom->sourceIsValid);

    auto db = m_solver.getVariableDB();

    auto& processQueue = outSet;
    processQueue.clear();

    vxy_assert(!lostSourceAtom->inUnfoundedSet);
    lostSourceAtom->inUnfoundedSet = true;
    processQueue.push_back(lostSourceAtom);

    m_remainUnfoundedSet.clear();

    bool needsSecondPass = false;
    for (int nextUnfounded = 0; nextUnfounded < processQueue.size(); ++nextUnfounded)
    {
        AtomData* headData = processQueue[nextUnfounded];
        vxy_assert(headData->scc == lostSourceAtom->scc);

        if (headData->sourceIsValid)
        {
            headData->inUnfoundedSet = false;

            needsSecondPass = true;
            continue;
        }

        // TODO: can avoid looping twice by checking for a new source as well adding to outSet
        if (findNewSource(headData))
        {
            vxy_sanity(headData->sourceIsValid);
            headData->inUnfoundedSet = false;

            // This head still has some (in)direct support outside of its SCC.
            // Propagate this new source assignment, which might add support for other heads in the unfounded set.
            m_sourcePropagationQueue.push_back(headData);
            emptySourcePropagationQueue();

            // other heads might've become supported due to propagation, so we need to rebuild the final list after.
            needsSecondPass = true;
            continue;
        }

        // No new source could be found, so this head remains potentially unfounded.
        // (propagation of another head later in the list might make it sourced - see needsSecondPass)
        vxy_assert(headData->inUnfoundedSet);
        m_remainUnfoundedSet.push_back(headData);

        // For each body of this head in the SCC, add all the body's unsourced lits in our SCC to the processing queue.
        // (U := U ∪ (β+ ∩ (scc(p) ∩ S)) in the paper.)
        for (auto it = iterateAtomSupports(headData); it; ++it)
        {
            BodyData* bodyData = *it;

            // If there was a source body, findNewSource should've found one!
            vxy_sanity((bodyData->scc == headData->scc && bodyData->numUnsourcedLits > 0) || !db->anyPossible(bodyData->variable, TRUE_VALUE));

            if (!db->anyPossible(bodyData->variable, TRUE_VALUE))
            {
                continue;
            }

            // Get the unsourced atoms that form the body and add them to the unfounded set.
            for (auto itv = iterateBodyPositiveLiterals(bodyData); itv; ++itv)
            {
                AtomData* bodyLitInfo = *itv;
                if (bodyLitInfo->scc != bodyData->scc)
                {
                    continue;
                }

                if (!bodyLitInfo->sourceIsValid && !bodyLitInfo->inUnfoundedSet && db->anyPossible(*bodyLitInfo->lit))
                {
                    bodyLitInfo->inUnfoundedSet = true;
                    processQueue.push_back(bodyLitInfo);
                }
            }
        }
    }

    swap(outSet, m_remainUnfoundedSet);
    if (needsSecondPass)
    {
        // we sourced at least one item, which might've caused items processed earlier in the list to become
        // sourced as well. So we need to do a final pass to find all truly unsourced atoms.
        for (auto it = outSet.begin(); it != outSet.end();)
        {
            auto next = it+1;
            AtomData* atom = *it;
            if (atom->sourceIsValid)
            {
                vxy_assert(atom->inUnfoundedSet);
                atom->inUnfoundedSet = false;
                next = outSet.erase_unsorted(it);
            }
            else
            {
                vxy_assert(atom->inUnfoundedSet);
            }
            it = next;
        }
    }

    return outSet.empty();
}

bool UnfoundedSetAnalyzer::findNewSource(AtomData* head)
{
    auto db = m_solver.getVariableDB();
    vxy_assert(!head->sourceIsValid);

    // This head no longer has its non-cyclic support.
    // Get the bodies that support it, and see if any can act as a new support.
    for (auto it = iterateAtomSupports(head); it; ++it)
    {
        BodyData* bodyData = *it;

        // Can support if this body is in a different SCC, or the same SCC but all its literals are sourced
        // by bodies outside the SCC.
        if (bodyData->scc != head->scc || bodyData->numUnsourcedLits == 0)
        {
            if (db->anyPossible(bodyData->variable, TRUE_VALUE))
            {
                // Ok, this can act as a new source!
                setSource(head, bodyData);
                return true;
            }
        }
    }

    vxy_assert(!head->sourceIsValid);
    return false;
}

bool UnfoundedSetAnalyzer::excludeUnfoundedSet(vector<AtomData*>& set)
{
    auto& db = *m_solver.getVariableDB();

    AssertionBuilder clause;
    while (!set.empty())
    {
        AtomData* atomToFalsify = set.back();
        vxy_assert(atomToFalsify->inUnfoundedSet);

        if (db.anyPossible(*atomToFalsify->lit))
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

        vxy_assert(atomToFalsify->inUnfoundedSet);
        atomToFalsify->inUnfoundedSet = false;
        set.pop_back();
    }

    return true;
}

bool UnfoundedSetAnalyzer::createNogoodForAtom(const AtomData* atomToFalsify, const vector<AtomData*>& unfoundedSet, const AssertionBuilder& clause)
{
    vector<Literal> assertionLiterals = clause.getAssertion(*atomToFalsify->lit);

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

    vxy_sanity(!m_solver.getVariableDB()->anyPossible(*atomToFalsify->lit));
    return true;
}

UnfoundedSetAnalyzer::AssertionBuilder UnfoundedSetAnalyzer::getExternalBodies(const vector<AtomData*>& unfoundedSet)
{
    vxy_assert(!unfoundedSet.empty());

    auto& db = *m_solver.getVariableDB();
    AssertionBuilder clauseBuilder;

    // For each atom we're going to falsify...
    int scc = unfoundedSet[0]->scc;
    for (int i = 0; i < unfoundedSet.size(); ++i)
    {
        AtomData* atomData = unfoundedSet[i];
        vxy_assert(atomData->scc == scc);
        vxy_sanity(!atomData->sourceIsValid);

        if (!db.anyPossible(*atomData->lit))
        {
            // atom is already false, so we're not propagating it.
            continue;
        }

        // go through each possible external support for the atom that we're falsifying, and add it to the reason we're false.
        for (auto it = iterateAtomSupports(atomData); it; ++it)
        {
            BodyData* bodyData = *it;
            vxy_sanity(bodyData->scc != scc || bodyData->numUnsourcedLits > 0 || !db.anyPossible(bodyData->variable, TRUE_VALUE));

            bool bExternal = true;
            if (bodyData->scc == scc)
            {
                // Check if all positive atoms in this body are outside of the set.
                // We only need to do this for bodies in same SCC; otherwise it's external by definition.
                for (auto itBody = iterateBodyPositiveLiterals(bodyData); itBody; ++itBody)
                {
                    AtomData* bodyLit = *itBody;
                    if (bodyLit->inUnfoundedSet)
                    {
                        bExternal = false;
                        break;
                    }
                }
            }

            if (bExternal)
            {
                Literal bodyLit(bodyData->variable, TRUE_VALUE);
                clauseBuilder.add(bodyLit, getAssertingTime(bodyLit));
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

void UnfoundedSetAnalyzer::initializeBodySupports(BodyData* body)
{
    auto db = m_solver.getVariableDB();

    // if the body atom is already false, we can't ever act as a support.
    if (!db->anyPossible(body->variable, TRUE_VALUE))
    {
        return;
    }

    // Add us as a source support for every head in a different SCC than us.
    for (auto it = iterateBodyHeads(body); it; ++it)
    {
        AtomData* head = *it;
        if (head->scc != body->scc && db->anyPossible(*head->lit))
        {
            if (!head->sourceIsValid)
            {
                setSource(head, body);
                m_sourcePropagationQueue.push_back(head);
            }
        }
    }
}

void UnfoundedSetAnalyzer::setSource(AtomData* atom, BodyData* body)
{
    vxy_assert(!atom->sourceIsValid);
    vxy_sanity(m_solver.getVariableDB()->anyPossible(*atom->lit));

    if (atom->source >= 0)
    {
        getAtomSource(atom)->numWatching--;
    }
    atom->source = int32_t(
        reinterpret_cast<const uint8_t*>(body) - m_bodyBuffer.data()
    );
    vxy_sanity(getAtomSource(atom) == body);
    atom->sourceIsValid = true;
    body->numWatching++;
}

void UnfoundedSetAnalyzer::emptySourcePropagationQueue()
{
    while (!m_sourcePropagationQueue.empty())
    {
        AtomData* atom = m_sourcePropagationQueue.back();
        m_sourcePropagationQueue.pop_back();

        if (atom->sourceIsValid)
        {
            propagateSourceAssignment(atom);
        }
        else
        {
            propagateSourceRemoval(atom);
        }
    }
}

void UnfoundedSetAnalyzer::propagateSourceAssignment(AtomData* atom)
{
    auto db = m_solver.getVariableDB();

    // for each body that includes this atom...
    for (auto it = iterateAtomPositiveDependencies(atom); it; ++it)
    {
        BodyData* body = *it;
        vxy_assert(body->scc == atom->scc);

        // deduct the number of the body's lits that are unsourced
        vxy_sanity(body->numUnsourcedLits > 0);
        body->numUnsourcedLits--;

        // if all our lits are sourced, then we can act as a support for any heads referring to us.
        // then propagate to any bodies that head atom is a part of.
        if (body->numUnsourcedLits == 0 && m_solver.getVariableDB()->anyPossible(body->variable, TRUE_VALUE))
        {
            for (auto it2 = iterateBodyHeads(body); it2; ++it2)
            {
                AtomData* headAtom = *it2;
                if (!headAtom->sourceIsValid && db->anyPossible(*headAtom->lit))
                {
                    setSource(headAtom, body);
                    m_sourcePropagationQueue.push_back(headAtom);
                }
            }
        }
    }
}

void UnfoundedSetAnalyzer::propagateSourceRemoval(AtomData* atom)
{
    // for each body that includes this atom...
    for (auto it = iterateAtomPositiveDependencies(atom); it; ++it)
    {
        BodyData* body = *it;
        vxy_assert(body->scc == atom->scc);

        // increase the number of the body's lits that are unsourced
        body->numUnsourcedLits++;
        if (body->numUnsourcedLits == 1 && body->numWatching > 0)
        {
            // we just became sourced->unsourced. tell our heads that they are no longer supported,
            // then propagate that to any bodies that head atom is part of.
            for (auto it2 = iterateBodyHeads(body); it2; ++it2)
            {
                AtomData* head = *it2;
                if (getAtomSource(head) == body && head->sourceIsValid)
                {
                    head->sourceIsValid = false;
                    m_sourcePropagationQueue.push_back(head);
                }
            }
        }
    }
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
