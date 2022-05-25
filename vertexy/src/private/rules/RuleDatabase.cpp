// Copyright Proletariat, Inc. All Rights Reserved.
#include "rules/RuleDatabase.h"

#include <EASTL/sort.h>

#include "ConstraintSolver.h"

using namespace Vertexy;

static constexpr bool LOG_CONCRETE_BODIES = false;

static const SolverVariableDomain booleanVariableDomain(0, 1);
static const ValueSet TRUE_VALUE = booleanVariableDomain.getBitsetForValue(1);

#define VERTEXY_RULE_NAME_ATOMS 1

RuleDatabase::RuleDatabase(ConstraintSolver& solver)
    : m_solver(solver)
{
    m_atoms.push_back(make_unique<ConcreteAtomInfo>());
}

void RuleDatabase::setConflicted()
{
    m_conflict = true;
}

bool RuleDatabase::finalize()
{
    // if (!propagateFacts())
    // {
    //     return false;
    // }

    auto db = m_solver.getVariableDB();

    //
    // Create all potential abstract head literals. We do this by manually calling getRelation() on each
    // abstract head, which will create the solver literal if it doesn't exist yet.
    //
    for (auto& bodyInfo : m_bodies)
    {
        if (bodyInfo->status != ETruthStatus::Undetermined)
        {
            continue;
        }

        static Literal tempLit;
        for (auto& head : bodyInfo->heads)
        {
            if (isLiteralAssumed(head))
            {
                continue;
            }

            AtomInfo* headAtomInfo = getAtom(head.id());
            if (auto abstractHead = headAtomInfo->asAbstract())
            {
                auto headRel = get<GraphLiteralRelationPtr>(abstractHead->getLiteral(*this, head));
                for (int vertex = 0; vertex < abstractHead->getTopology()->getNumVertices(); ++vertex)
                {
                    headRel->getRelation(vertex, tempLit);
                }
            }
        }
    }
        
    //
    // Go through each body, constrain the body so it is true if all literals in the body are true, and false
    // if any literal in the body is false.
    //
    for (auto& bodyInfo : m_bodies)
    {
        if (bodyInfo->status != ETruthStatus::Undetermined)
        {
            continue;
        }

        m_nogoodBuilder.reserve(bodyInfo->atomLits.size()+1);
        for (auto& bodyLit : bodyInfo->atomLits)
        {
            if (isLiteralAssumed(bodyLit))
            {
                // literal is known, no need to include.
                continue;
            }
            
            auto bodyLitAtom = getAtom(bodyLit.id());
            ALiteral atomLit = bodyLitAtom->getLiteral(*this, bodyLit.inverted());
            m_nogoodBuilder.add(atomLit, bodyLit.sign(), bodyLitAtom->getTopology());
        }

        //
        // For all literals Bv1...BvN in the body:
        // nogood(-B, Bv1, Bv2, ..., BvN) == clause(B, -Bv1, -Bv2, ..., -BvN)
        //
        m_nogoodBuilder.add(bodyInfo->getLiteral(*this, true, false), true, bodyInfo->getTopology());
        m_nogoodBuilder.emit(*this);

        //
        // For each literal Bv in the body:
        // nogood(B, -Bv) == clause(-B, Bv)
        //
        for (auto& bodyLit : bodyInfo->atomLits)
        {
            if (isLiteralAssumed(bodyLit))
            {
                // literal is known, no need to include.
                continue;
            }

            auto bodyLitAtom = getAtom(bodyLit.id());
            ALiteral atomLit = bodyLitAtom->getLiteral(*this, bodyLit);
            m_nogoodBuilder.add(bodyInfo->getLiteral(*this, false, true), true, bodyInfo->getTopology());
            m_nogoodBuilder.add(atomLit, !bodyLit.sign(), bodyLitAtom->getTopology());
            m_nogoodBuilder.emit(*this);
        }
        
        //
        // For each head H attached to this body:
        // nogood(-H, B) == clause(H, -B)
        //
        for (auto ith = bodyInfo->heads.begin(), ithEnd = bodyInfo->heads.end(); ith != ithEnd; ++ith)
        {
            if (isLiteralAssumed(*ith))
            {
                continue;
            }

            AtomInfo* headAtomInfo = getAtom((*ith).id());
            ALiteral headLit = headAtomInfo->getLiteral(*this, *ith);
            
            m_nogoodBuilder.add(bodyInfo->getLiteral(*this, true, true), true, bodyInfo->getTopology());
            m_nogoodBuilder.add(headLit, true, headAtomInfo->getTopology());
            m_nogoodBuilder.emit(*this);
        }

        if (bodyInfo->isNegativeConstraint)
        {
            // Body can't be true. Instead of making a constraint for this, we simply set the
            // values of the variables representing the body to disallow "true".
            if (auto concreteBody = bodyInfo->asConcrete())
            {
                if (!db->excludeValues(concreteBody->equivalence, nullptr))
                {
                    setConflicted();
                    return false;
                }
            }
            else
            {
                auto absBody = bodyInfo->asAbstract();
                for (int vertex = 0; vertex < absBody->topology->getNumVertices(); ++vertex)
                {
                    Literal equivalence;
                    if (!absBody->relation->getRelation(vertex, equivalence))
                    {
                        continue;
                    }

                    if (!db->excludeValues(equivalence, nullptr))
                    {
                        setConflicted();
                        return false;
                    }
                }
            }
        }
    }
    
    //
    // Go through each atom, and constrain it to be false if ALL supporting bodies are false.
    // nogood(H, -B1, -B2, ...) == clause(-H, B1, B2, ...)
    //
    for (auto it = m_atoms.begin()+1, itEnd = m_atoms.end(); it != itEnd; ++it)
    {
        auto atomInfo = it->get();
        if (atomInfo->status != ETruthStatus::Undetermined)
        {
            continue;
        }

        if (isLiteralAssumed(atomInfo->id.neg()) || atomInfo->isExternal)
        {
            continue;
        }

        m_nogoodBuilder.reserve(atomInfo->supports.size()+1);

        if (auto concreteAtom = atomInfo->asConcrete())
        {
            m_nogoodBuilder.add(concreteAtom->getConcreteLiteral(*this, true), true, concreteAtom->getTopology());
            for (auto supportBodyInfo : concreteAtom->supports)
            {
                // we should've been marked trivially true if one of our supports was,
                // or it should've been removed as a support if it is trivially false.
                vxy_assert(supportBodyInfo->status == ETruthStatus::Undetermined);
                // if the body is false, it cannot support us
                m_nogoodBuilder.add(supportBodyInfo->getLiteral(*this, false, false), true, supportBodyInfo->getTopology());
            }
            m_nogoodBuilder.emit(*this);
        }
        else
        {
            // In order to ensure proper coverage, we need to make nogoods for each head literal we've seen.
            // TODO: This can produce many duplicated concrete clauses.
            auto abstractAtom = atomInfo->asAbstract();
            for (auto& absLit : abstractAtom->abstractLiterals)
            {
                if (absLit.second != ETruthStatus::Undetermined)
                {
                    continue;
                }
                
                m_nogoodBuilder.add(make_shared<InvertLiteralGraphRelation>(absLit.first->literalRelation), true, abstractAtom->getTopology());
                for (auto bodyInfo : abstractAtom->supports)
                {
                    vxy_assert(bodyInfo->status != ETruthStatus::False);
                    // Abstract atoms might have still have a body marked trivially true without the atom state
                    // being trivially true.
                    if (bodyInfo->status == ETruthStatus::True)
                    {
                        continue;
                    }
                    
                    if (auto abstractBody = bodyInfo->asAbstract())
                    {
                        auto bodyRel = abstractBody->makeRelationForAbstractHead(*this, absLit.first);                        
                        m_nogoodBuilder.add(bodyRel, false, bodyInfo->getTopology());
                    }
                    else
                    {
                        auto concreteBody = bodyInfo->asConcrete();
                        vxy_assert(concreteBody->equivalence.isValid());
                        m_nogoodBuilder.add(concreteBody->equivalence, false, bodyInfo->getTopology());
                    }
                }
                m_nogoodBuilder.emit(*this);
            }
        }
    }

    if (!m_conflict)
    {
        if (m_hasAbstract)
        {
            makeConcrete();
            propagateFacts();
        }
        computeSCCs();
    }

    return !m_conflict;
}

bool RuleDatabase::isLiteralAssumed(const AtomLiteral& literal) const
{
    auto atom = getAtom(literal.id());
    if ((literal.sign() && atom->status == ETruthStatus::False) ||
        (!literal.sign() && atom->status == ETruthStatus::True))
    {
        vxy_fail(); // we should've failed due to conflict already
        return false;
    }

    if (atom->status != ETruthStatus::Undetermined)
    {
        return true;
    }

    auto concreteAtom = atom->asConcrete();
    if (concreteAtom != nullptr && concreteAtom->equivalence.isValid())
    {
        auto db = m_solver.getVariableDB();
        auto& elit = concreteAtom->equivalence;
        if ((literal.sign() && db->getPotentialValues(elit.variable).isSubsetOf(elit.values)) ||
            (!literal.sign() && !db->getPotentialValues(elit.variable).anyPossible(elit.values)))
        {
            return true;
        }
    }
    // else if (auto abstractAtom = atom->asAbstract();
    //          abstractAtom != nullptr && literal.getRelationInfo() != nullptr)
    // {
    //     auto found = abstractAtom->abstractLiterals.find(literal.getRelationInfo());
    //     if (found != abstractAtom->abstractLiterals.end())
    //     {
    //         if (found->second != ETruthStatus::Undetermined)
    //         {
    //             return true;
    //         }
    //     }
    // }

    return false;
}

void RuleDatabase::addRule(const AtomLiteral& head, const vector<AtomLiteral>& body, const ITopologyPtr& topology)
{
    vxy_assert(!head.isValid() || head.sign());

    bool isFact = body.empty();

    // create the BodyInfo (or return the existing one if this is a duplicate)
    BodyInfo* newBodyInfo;
    if (isFact)
    {
        // Empty input body means this is a fact. Set the body to the fact atom, which is always true.
        bool needAbstract = head.isValid() && getAtom(head.id())->asAbstract() != nullptr;
        newBodyInfo = findOrCreateBodyInfo(vector{getTrueAtom().pos()}, topology, head.getRelationInfo(), needAbstract);
    }
    else
    {
        newBodyInfo = findOrCreateBodyInfo(body, topology, head.getRelationInfo(), false);
    }

    // Link the body to the head relying on it, and the head to the body supporting it.
    if (head.isValid())
    {
        auto headInfo = getAtom(head.id());
        if (!contains(headInfo->supports.begin(), headInfo->supports.end(), newBodyInfo))
        {
            headInfo->supports.push_back(newBodyInfo);
            vxy_sanity(!contains(newBodyInfo->heads.begin(), newBodyInfo->heads.end(), head));
            newBodyInfo->heads.push_back(head);
        }
        else
        {
            vxy_sanity(contains(newBodyInfo->heads.begin(), newBodyInfo->heads.end(), head));
        }

        if (auto absHeadInfo = headInfo->asAbstract())
        {
            vxy_assert(head.getRelationInfo() != nullptr);
            auto truthStatus = isFact ? ETruthStatus::True : ETruthStatus::Undetermined;
            absHeadInfo->abstractLiterals.insert({head.getRelationInfo(), truthStatus});
        }
        else if (isFact)
        {
            setAtomStatus(headInfo->asConcrete(), ETruthStatus::True);
        }
    }
    else
    {
        // this body has no head, so it should never hold true.
        newBodyInfo->isNegativeConstraint = true;
    }

    // Link each atom in the body to the body depending on it.
    for (auto& bodyValue : body)
    {
        auto atomInfo = getAtom(bodyValue.id());
        auto& deps = bodyValue.sign() ? atomInfo->positiveDependencies : atomInfo->negativeDependencies;
        if (!contains(deps.begin(), deps.end(), newBodyInfo))
        {
            deps.push_back(newBodyInfo);
        }
    }
}

RuleDatabase::BodyInfo* RuleDatabase::findBodyInfo(const vector<AtomLiteral>& body, const BodySet& bodySet, const AbstractAtomRelationInfoPtr& headRelationInfo, size_t& outHash, bool checkRelations) const
{
    outHash = BodyHasher::hashBody(body);
    auto range = bodySet.find_range_by_hash(outHash);
    for (auto it = range.first; it != range.second; ++it)
    {
        if (BodyHasher::compareBodies((*it)->atomLits, body, checkRelations))
        {
            if ((*it)->headRelationInfo == nullptr ||
                headRelationInfo == nullptr ||
                headRelationInfo == (*it)->headRelationInfo)
            {
                if (headRelationInfo != nullptr)                {
                    (*it)->headRelationInfo = headRelationInfo;
                }
                return *it;
            }
        }
    }
    return nullptr;
}


RuleDatabase::BodyInfo* RuleDatabase::findOrCreateBodyInfo(const vector<AtomLiteral>& body, const ITopologyPtr& topology, const AbstractAtomRelationInfoPtr& headRelationInfo, bool forceAbstract)
{
    vxy_assert(!body.empty());

    size_t hash;
    if (auto found = findBodyInfo(body, m_bodySet, headRelationInfo, hash))
    {
        return found;
    }

    bool hasAbstract = forceAbstract || containsPredicate(body.begin(), body.end(), [&](const AtomLiteral& bodyLit)
    {
       return getAtom(bodyLit.id())->asAbstract() != nullptr;
    });

    unique_ptr<BodyInfo> newBodyInfo;
    if (hasAbstract)
    {
        vxy_assert_msg(topology != nullptr, "must supply a topology for an abstract rule");
        newBodyInfo = make_unique<AbstractBodyInfo>(m_bodies.size(), body, topology);

        m_hasAbstract = true;
    }
    else
    {
        newBodyInfo = make_unique<ConcreteBodyInfo>(m_bodies.size(), body);
    }
    newBodyInfo->numUndeterminedTails = body.size();
    newBodyInfo->headRelationInfo = headRelationInfo;    

    m_bodySet.insert(hash, nullptr, newBodyInfo.get());
    m_bodies.push_back(move(newBodyInfo));

    return m_bodies.back().get();
}

AtomID RuleDatabase::getTrueAtom()
{
    if (m_trueAtom.isValid())
    {
        return m_trueAtom;
    }
    m_trueAtom = createAtom(TEXT("<true-fact>"), true);

    setAtomStatus(getAtom(m_trueAtom)->asConcrete(), ETruthStatus::True);
    return m_trueAtom;
}

AtomID RuleDatabase::createAtom(const wchar_t* name, bool external)
{
    AtomID newAtom(m_atoms.size());

    m_atoms.push_back(make_unique<ConcreteAtomInfo>(newAtom));
    m_atoms.back()->isExternal = external;
#if VERTEXY_RULE_NAME_ATOMS
    if (name == nullptr)
    {
        m_atoms.back()->name.sprintf(TEXT("atom%d"), newAtom.value);
    }
    else
    {
        m_atoms.back()->name = name;
    }
#endif
    return newAtom;
}

AtomID RuleDatabase::createBoundAtom(const Literal& lit, const wchar_t* name, bool external)
{
#if VERTEXY_RULE_NAME_ATOMS
    wstring sname;
    if (name == nullptr)
    {
        sname = {wstring::CtorSprintf(), TEXT("atom%d(%s=%s)"), m_atoms.size(), m_solver.getVariableName(lit.variable).c_str(), lit.values.toString().c_str()};
        name = sname.c_str();
    }
#endif

    AtomID newAtom = createAtom(name, external);

    auto newAtomInfo = m_atoms[newAtom.value]->asConcrete();
    vxy_assert_msg(newAtomInfo != nullptr, "expected concrete atom");

    newAtomInfo->equivalence = lit;

    return newAtom;
}

AtomID RuleDatabase::createAbstractAtom(const ITopologyPtr& topology, const wchar_t* name, bool external)
{
    AtomID newAtom(m_atoms.size());

    m_hasAbstract = true;
    
    m_atoms.push_back(make_unique<AbstractAtomInfo>(newAtom, topology));
    m_atoms.back()->isExternal = external;
#if VERTEXY_RULE_NAME_ATOMS
    if (name == nullptr)
    {
        m_atoms.back()->name.sprintf(TEXT("atom%d"), newAtom.value);
    }
    else
    {
        m_atoms.back()->name = name;
    }
#endif

    return newAtom;
}

void RuleDatabase::computeSCCs()
{
    m_isTight = true;

    int nextSCC = 0;
    auto foundScc = [&](int level, auto it)
    {
        AtomID lastAtom;
        int lastBody = -1;

        int num = 0;
        for (; it; ++it, ++num)
        {
            int node = *it;
            if (node < m_atoms.size()-1)
            {
                lastAtom = AtomID((*it) + 1);
                getAtom(lastAtom)->asConcrete()->scc = nextSCC;
            }
            else
            {
                lastBody = (*it) - (m_atoms.size()-1);
                m_bodies[lastBody]->asConcrete()->scc = nextSCC;
            }
        }

        vxy_sanity(num > 0);
        if (num == 1)
        {
            // trivially connected component
            // mark as not belonging to any scc
            vxy_sanity(!lastAtom.isValid() || lastBody < 0);
            if (lastAtom.isValid())
            {
                getAtom(lastAtom)->asConcrete()->scc = -1;
            }
            else
            {
                m_bodies[lastBody]->asConcrete()->scc = -1;
            }
        }
        else
        {
            // there is a loop in the positive dependency graph, so problem is non-tight.
            m_isTight = false;
            ++nextSCC;
        }
    };

    m_tarjan.findStronglyConnectedComponents(m_atoms.size()-1 + m_bodies.size(),
        [&](int node, auto visitor) { return tarjanVisit(node, visitor); },
        foundScc
    );
}

template <typename T>
void RuleDatabase::tarjanVisit(int node, T&& visitor)
{
    if (node < m_atoms.size()-1)
    {
        AtomID atom(node+1);
        const AtomInfo* atomInfo = getAtom(atom);
        // for each body where this atom occurs (as positive)...
        for (auto refBodyInfo : atomInfo->positiveDependencies)
        {
            const auto& depBodyLits = refBodyInfo->atomLits;
            vxy_sanity(find(depBodyLits.begin(), depBodyLits.end(), AtomLiteral(atom, true)) != depBodyLits.end());

            visitor(m_atoms.size()-1 + refBodyInfo->id);
        }
    }
    else
    {
        auto refBodyInfo = m_bodies[node - (m_atoms.size()-1)].get();
        // visit each head that this body is supporting.
        for (auto& head : refBodyInfo->heads)
        {
            visitor(head.id().value-1);
        }
    }
}

bool RuleDatabase::setAtomStatus(ConcreteAtomInfo* atom, ETruthStatus status)
{
    if (status == ETruthStatus::Undetermined)
    {
        return true;
    }
    
    if (atom->status != status)
    {
        if (atom->status == ETruthStatus::Undetermined)
        {
            atom->status = status;

            if (!atom->synchronize(*this))
            {
                setConflicted();
                return false;
            }            
        }
        else
        {
            setConflicted();
            return false;
        }

        if (!atom->enqueued)
        {
            atom->enqueued = true;
            m_atomsToPropagate.push_back(atom);
        }
    }
    return true;
}

bool RuleDatabase::setBodyStatus(ConcreteBodyInfo* body, ETruthStatus status)
{
    if (status == ETruthStatus::Undetermined)
    {
        return true;
    }
    
    if (body->status != status)
    {
        if (body->status == ETruthStatus::Undetermined)
        {
            body->status = status;

            if (body->equivalence.isValid())
            {
                auto db = m_solver.getVariableDB();
                if (body->status == ETruthStatus::True && !db->constrainToValues(body->equivalence, nullptr))
                {
                    return false;
                }
                else if (body->status == ETruthStatus::False && !db->excludeValues(body->equivalence, nullptr))
                {
                    return false;
                }
            }
        }
        else
        {
            setConflicted();
            return false;
        }

        if (!body->enqueued)
        {
            body->enqueued = true;
            m_bodiesToPropagate.push_back(body);
        }
    }
    return true;
}

bool RuleDatabase::propagateFacts()
{
    // mark any atoms that have no supports as false.
    for (auto& atom : m_atoms)
    {
        vxy_assert(atom->asConcrete() != nullptr);
        if (atom->status != ETruthStatus::Undetermined)
        {
            if (!atom->enqueued)
            {
                atom->enqueued = true;
                m_atomsToPropagate.push_back(atom->asConcrete());
            }
        }
        else if (!atom->isExternal &&
                atom->supports.empty() &&
                !setAtomStatus(atom->asConcrete(), ETruthStatus::False))
        {
            return false;
        }
    }

    // mark any bodies that are definitely true or false
    for (auto& body : m_bodies)
    {
        vxy_assert(body->asConcrete() != nullptr);
        
        bool fact = true;
        for (auto& lit : body->atomLits)
        {
            auto litAtom = getAtom(lit.id());
            if (litAtom->status == ETruthStatus::Undetermined)
            {
                fact = false;
                break;
            }

            if ((lit.sign() && litAtom->status == ETruthStatus::False) ||
                (!lit.sign() && litAtom->status == ETruthStatus::True))
            {
                setBodyStatus(body->asConcrete(), ETruthStatus::False);
                fact = false;
                break;
            }
        }

        if (fact)
        {
            setBodyStatus(body->asConcrete(), ETruthStatus::True);
        }
    }

    // propagate until we reach fixpoint.
    while (!m_atomsToPropagate.empty() || !m_bodiesToPropagate.empty())
    {
        if (!emptyAtomQueue())
        {
            return false;
        }

        if (!emptyBodyQueue())
        {
            return false;
        }
    }

    return true;
}

bool RuleDatabase::emptyAtomQueue()
{
    while (!m_atomsToPropagate.empty())
    {
        auto atom = m_atomsToPropagate.back();
        m_atomsToPropagate.pop_back();

        vxy_assert(atom->enqueued);
        atom->enqueued = false;
        
        auto positiveSide = (atom->status == ETruthStatus::True) ? &atom->positiveDependencies : &atom->negativeDependencies;
        auto negativeSide = (atom->status == ETruthStatus::True) ? &atom->negativeDependencies : &atom->positiveDependencies;

        // For each body this atom is in positively, reduce that bodies' number of undeterminedTails.
        // If all the body's tails (i.e. atoms that make up the body) are determined, we can mark the body as true.
        for (auto depBody : *positiveSide)
        {
            vxy_assert(depBody->numUndeterminedTails > 0);
            depBody->numUndeterminedTails--;
            if (depBody->numUndeterminedTails == 0)
            {
                if (!setBodyStatus(depBody->asConcrete(), ETruthStatus::True))
                {
                    return false;
                }
            }
        }

        // for each body this atom is in negatively, falsify the body
        for (auto depBody : *negativeSide)
        {
            vxy_assert(depBody->numUndeterminedTails > 0);
            if (!setBodyStatus(depBody->asConcrete(), ETruthStatus::False))
            {
                return false;
            }
        }
    }

    return true;
}

bool RuleDatabase::emptyBodyQueue()
{
    while (!m_bodiesToPropagate.empty())
    {
        auto body = m_bodiesToPropagate.back();
        m_bodiesToPropagate.pop_back();

        vxy_assert(body->enqueued);
        body->enqueued = false;

        vxy_assert(body->status != ETruthStatus::Undetermined);

        if (body->status == ETruthStatus::True)
        {
            // mark all heads of this body as true
            for (auto it = body->heads.begin(), itEnd = body->heads.end(); it != itEnd; ++it)
            {
                vxy_assert(it->sign());
                if (!setAtomStatus(getAtom(it->id())->asConcrete(), ETruthStatus::True))
                {
                    return false;
                }
            }
        }
        else
        {
            // Remove this body from the list of each head's supports.
            // If an atom no longer has any supports, it can be falsified.
            for (auto it = body->heads.begin(), itEnd = body->heads.end(); it != itEnd; ++it)
            {
                vxy_assert(it->sign());
                auto atom = getAtom(it->id())->asConcrete();
                vxy_assert(contains(atom->supports.begin(), atom->supports.end(), body));
                atom->supports.erase_first_unsorted(body);
                if (atom->supports.empty())
                {
                    if (!setAtomStatus(atom, ETruthStatus::False))
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

const SolverVariableDomain& RuleDatabase::getDomain(VarID varID) const
{
    return m_solver.getDomain(varID);
}

bool RuleDatabase::isConcreteLiteral(const ALiteral& alit)
{
    return visit([](auto&& typedLit)
    {
        using Type = decay_t<decltype(typedLit)>;
        return is_same_v<Type, Literal>;
    }, alit);
}

void RuleDatabase::makeConcrete()
{
    for (auto atom : m_atomsToPropagate) { atom->enqueued = false; }
    m_atomsToPropagate.clear();
    for (auto body : m_bodiesToPropagate) { body->enqueued = false; }
    m_bodiesToPropagate.clear();
    
    GroundingData groundingData(m_atoms.size(), m_bodies.size());

    //
    // First ground all heads
    //

    for (auto& atom : m_atoms)
    {
        if (atom->asConcrete() != nullptr && atom->status != ETruthStatus::Undetermined)
        {
            groundAtomToConcrete(AtomLiteral(atom->id, true), groundingData);
        }
        else if (auto absAtom = atom->asAbstract())
        {
            for (auto& litEntry : absAtom->abstractLiterals)
            {
                if (litEntry.second == ETruthStatus::True)
                {
                    groundAtomToConcrete(AtomLiteral(atom->id, true, litEntry.first), groundingData);
                }
            }
        }
    }
    
    for (auto& body : m_bodies)
    {
        auto bodyInfo = body.get();
        for (auto& atomLit : bodyInfo->heads)
        {
            groundAtomToConcrete(atomLit, groundingData);
        }
    }

    //
    // Now create the bodies and connect the heads/values.
    //
    
    for (auto& body : m_bodies)
    {
        groundBodyToConcrete(*body, groundingData);
    }

    //
    // replace the partially-abstract atoms with the grounded (undecided) atoms
    //

    m_atoms = move(groundingData.newAtoms);
    m_bodies = move(groundingData.newBodies);

    auto litToString = [&](const AtomLiteral& lit)
    {
        wstring out;
        if (!lit.sign()) out += TEXT("~");

        auto atomInfo = getAtom(lit.id())->asConcrete();
        if (atomInfo->equivalence.isValid())
        {
             out += TEXT("(") +
                m_solver.getVariableName(atomInfo->equivalence.variable) +
                TEXT("=") +
                atomInfo->equivalence.values.toString() +
                TEXT(")");
        }
        else
        {
            out += atomInfo->name;
        }
        return out;
    };
    
    auto bodyToString = [&](const ConcreteBodyInfo* body)
    {
        wstring out;
        bool first = true; 
        for (auto& lit : body->atomLits)
        {
            if (!first)
            {
                out += TEXT(", ");
            }
            first = false;
            out += litToString(lit);
        }
        return out;
    };

    if (LOG_CONCRETE_BODIES)
    {
        VERTEXY_LOG("Concrete bodies:");
        for (auto& body : m_bodies)
        {
            if (body->heads.empty())
            {
                VERTEXY_LOG(" << %s", bodyToString(body->asConcrete()).c_str());
            }
            else
            {
                for (auto& head : body->heads)
                {
                    VERTEXY_LOG("%s << %s", litToString(head).c_str(), bodyToString(body->asConcrete()).c_str());
                }
            }
        }
    }
}

vector<AtomLiteral> RuleDatabase::groundLiteralsToConcrete(const vector<AtomLiteral>& oldLits, GroundingData& groundingData, bool& outSomeFailed, int vertex)
{
    outSomeFailed = false;
    
    vector<AtomLiteral> newLits;
    for (auto& oldLit : oldLits)
    {
        if (m_trueAtom.isValid() && oldLit.id() == m_trueAtom)
        {
            if (!oldLit.sign())
            {
                outSomeFailed = true;
            }
            continue;
        }
        
        auto oldAtomInfo = getAtom(oldLit.id());
        if (oldAtomInfo->asConcrete() != nullptr)
        {
            AtomID newID = groundingData.concreteAtomMappings[oldLit.id().value];
            vxy_assert(newID.isValid());
            
            newLits.push_back(AtomLiteral(newID, oldLit.sign()));
        }
        else
        {
            auto& relationInfo = oldLit.getRelationInfo();
            auto makeConcreteForVertex = [&](ITopology::VertexID v, AbstractAtomInfo* oldAbsInfo, const AtomLiteral& abstractLit)
            {
                auto rel = get<GraphLiteralRelationPtr>(oldAbsInfo->getLiteral(*this, oldLit));
                
                if (oldAbsInfo->isExternal)
                {
                    int value;
                    Literal concreteLit;
                    if (!rel->getRelation(v, concreteLit))
                    {
                        return !abstractLit.sign();
                    }
                    
                    vxy_verify(concreteLit.values.isSingleton(value));
                    return value != 0;
                }

                vector<int> args;
                args.reserve(relationInfo->argumentRelations.size());
                for (auto& arg : relationInfo->argumentRelations)
                {
                    int resolved;
                    if (!arg->getRelation(v, resolved))
                    {
                        break;
                    }
                    args.push_back(resolved);
                }

                if (args.size() != relationInfo->argumentRelations.size())
                {
                    // No relation
                    return !abstractLit.sign();
                }
                
                auto& mappings = groundingData.abstractAtomMappings[abstractLit.id().value]; 
                auto found = mappings.find(args);
                if (found == mappings.end())
                {
                    // No head defining this atom
                    return !abstractLit.sign();
                }

                auto foundAtom = groundingData.newAtoms[found->second.value].get();
                if (foundAtom->status != ETruthStatus::Undetermined)
                {
                    return  (foundAtom->status == ETruthStatus::True && abstractLit.sign()) ||
                            (foundAtom->status == ETruthStatus::False && !abstractLit.sign());
                }

                newLits.push_back(AtomLiteral(found->second, abstractLit.sign(), abstractLit.getRelationInfo()));
                return true;
            };
            
            auto oldAbsInfo = oldAtomInfo->asAbstract();
            if (vertex < 0)
            {
                for (int curVertex = 0; curVertex < oldAbsInfo->topology->getNumVertices(); ++curVertex)
                {
                    if (!makeConcreteForVertex(curVertex, oldAbsInfo, oldLit))
                    {
                        outSomeFailed = true;
                    }
                }
            }
            else
            {
                if (!makeConcreteForVertex(vertex, oldAbsInfo, oldLit))
                {
                    outSomeFailed = true;
                }
            }
        }
    }
    
    return newLits;
}

void RuleDatabase::groundBodyToConcrete(const BodyInfo& oldBody, GroundingData& groundingData)
{
    auto& bodyMapping = groundingData.bodyMappings[oldBody.id];
    if (auto oldConcreteBody = oldBody.asConcrete())
    {
        bool someHeadsFailed;
        vector<AtomLiteral> newHeads = groundLiteralsToConcrete(oldConcreteBody->heads, groundingData, someHeadsFailed);
        if (newHeads.empty() && !oldConcreteBody->heads.empty())
        {
            return;
        }

        bool someValsFailed;
        vector<AtomLiteral> newLits = groundLiteralsToConcrete(oldConcreteBody->atomLits, groundingData, someValsFailed);
        vxy_assert(!someValsFailed);

        if (newHeads.empty() && newLits.empty())
        {
            return;
        }

        // Just clone the body
        auto newBody = make_unique<ConcreteBodyInfo>();
        newBody->id = groundingData.newBodies.size();
        newBody->isNegativeConstraint = oldConcreteBody->isNegativeConstraint;
        newBody->equivalence = oldConcreteBody->equivalence;
        newBody->atomLits = move(newLits);
        newBody->heads = move(newHeads);
        newBody->status = oldBody.status;
        setBodyStatus(newBody.get(), oldBody.status);

        hookupGroundedDependencies(newBody.get(), groundingData);

        bodyMapping.push_back(groundingData.newBodies.size());
        groundingData.newBodySet.insert(BodyHasher::hashBody(newBody->atomLits), nullptr, newBody.get());
        groundingData.newBodies.push_back(move(newBody));
    }
    else
    {
        // Abstract body needs to be instantiated for every vertex of the topology.
        auto oldAbstractBody = oldBody.asAbstract();
        bodyMapping.resize(oldAbstractBody->topology->getNumVertices(), -1);
        
        for (int vertex = 0; vertex < oldAbstractBody->topology->getNumVertices(); ++vertex)
        {
            auto rel = get<GraphLiteralRelationPtr>(oldAbstractBody->getLiteral(*this, false, false));

            Literal lit;
            if (!rel->getRelation(vertex, lit))
            {
                continue;
            }
            
            bool someHeadsFailed;
            vector<AtomLiteral> newHeads = groundLiteralsToConcrete(oldAbstractBody->heads, groundingData, someHeadsFailed, vertex);
            if (newHeads.empty() && !oldAbstractBody->heads.empty())
            {
                continue;
            }
            
            bool someValsFailed;
            vector<AtomLiteral> newValues = groundLiteralsToConcrete(oldAbstractBody->atomLits, groundingData, someValsFailed, vertex);
            if (someValsFailed)
            {
                continue;
            }

            if (newValues.empty())
            {
                for (auto& headLit : newHeads)
                {
                    auto headInfo = groundingData.newAtoms[headLit.id().value]->asConcrete();
                    setAtomStatus(headInfo, ETruthStatus::True);
                }
                continue;
            }

            size_t hash;
            if (auto existingBody = findBodyInfo(newValues, groundingData.newBodySet, nullptr, hash, false))
            {
                for (auto& head : newHeads)
                {
                    if (!contains(existingBody->heads.begin(), existingBody->heads.end(), head))
                    {
                        groundingData.newAtoms[head.id().value]->supports.push_back(existingBody);
                        existingBody->heads.push_back(head);
                    }
                }

                // Make these two bodies equivalent to each other.
                m_solver.makeConstraint<ClauseConstraint>(vector{existingBody->asConcrete()->equivalence, lit.inverted()}, false);
                m_solver.makeConstraint<ClauseConstraint>(vector{existingBody->asConcrete()->equivalence.inverted(), lit}, false);
            }
            else
            {
                auto newBody = make_unique<ConcreteBodyInfo>();
                newBody->id = groundingData.newBodies.size();
                newBody->equivalence = lit;
                newBody->isNegativeConstraint = oldAbstractBody->isNegativeConstraint;
                newBody->atomLits = move(newValues);
                newBody->heads = move(newHeads);
                newBody->status = oldAbstractBody->status;
                setBodyStatus(newBody.get(), oldAbstractBody->status);

                hookupGroundedDependencies(newBody.get(), groundingData);

                bodyMapping[vertex] = groundingData.newBodies.size();
                groundingData.newBodySet.insert(hash, nullptr, newBody.get());
                groundingData.newBodies.push_back(move(newBody));
            }
        }
    }
}

void RuleDatabase::hookupGroundedDependencies(ConcreteBodyInfo* newBodyInfo, GroundingData& groundingData)
{
    for (auto& headLit : newBodyInfo->heads)
    {
        vxy_assert(headLit.sign());
        auto headInfo = groundingData.newAtoms[headLit.id().value].get();
        headInfo->supports.push_back(newBodyInfo);
    }

    for (auto& bodyLit : newBodyInfo->atomLits)
    {
        auto litInfo = groundingData.newAtoms[bodyLit.id().value].get();
        if (bodyLit.sign())
        {
            litInfo->positiveDependencies.push_back(newBodyInfo);
        }
        else
        {
            litInfo->negativeDependencies.push_back(newBodyInfo);
        }
    }
    newBodyInfo->numUndeterminedTails = newBodyInfo->atomLits.size();
}

void RuleDatabase::groundAtomToConcrete(const AtomLiteral& oldAtom, GroundingData& groundingData)
{
    vxy_assert(oldAtom.sign());
    
    const AtomInfo* oldAtomInfo = getAtom(oldAtom.id());
    if (oldAtomInfo->status != ETruthStatus::Undetermined)
    {
        return;
    }
    
    if (auto oldConcreteAtom = oldAtomInfo->asConcrete())
    {
        // Concrete atom. Just create a new copy, if we haven't made it yet already.
        if (!groundingData.concreteAtomMappings[oldAtomInfo->id.value].isValid())
        {
            auto newAtom = make_unique<ConcreteAtomInfo>(AtomID(groundingData.newAtoms.size()));
            newAtom->name = oldConcreteAtom->name;
            newAtom->equivalence = oldConcreteAtom->equivalence;
            newAtom->isExternal = oldConcreteAtom->isExternal;
            setAtomStatus(newAtom.get(), oldConcreteAtom->status);

            groundingData.concreteAtomMappings[oldAtomInfo->id.value] = AtomID(groundingData.newAtoms.size());
            groundingData.newAtoms.push_back(move(newAtom));
        }
    }
    else
    {
        auto oldAbstractAtom = oldAtomInfo->asAbstract();
        auto& relationInfo = oldAtom.getRelationInfo();
        auto litEntry = oldAbstractAtom->abstractLiterals.find(oldAtom.getRelationInfo());

        vector<int> args;

        auto& vertexMap = groundingData.abstractAtomMappings[oldAbstractAtom->id.value];
        for (int vertex = 0; vertex < oldAbstractAtom->topology->getNumVertices(); ++vertex)
        {
            args.clear();
            args.reserve(relationInfo->argumentRelations.size());
            for (auto& argRel : relationInfo->argumentRelations)
            {
                int resolved;
                if (!argRel->getRelation(vertex, resolved))
                {
                    break;
                }
                args.push_back(resolved);
            }

            if (args.size() != relationInfo->argumentRelations.size())
            {
                continue;
            }
            
            if (auto found = vertexMap.find(args);
                found != vertexMap.end())
            {
                auto existing = groundingData.newAtoms[found->second.value]->asConcrete();
                if (existing->status == ETruthStatus::Undetermined)
                {
                    setAtomStatus(existing, litEntry->second);
                }                    
                continue;
            }

            Literal lit;
            if (!oldAtom.getRelationInfo()->literalRelation->getRelation(vertex, lit))
            {
                continue;
            }
            
            auto newAtom = make_unique<ConcreteAtomInfo>(AtomID(groundingData.newAtoms.size()));
            newAtom->name.sprintf(TEXT("(%s=%s)"), m_solver.getVariableName(lit.variable).c_str(), lit.values.toString());
            newAtom->equivalence = lit;
            newAtom->isExternal = oldAtomInfo->isExternal;
            setAtomStatus(newAtom.get(), litEntry->second);

            vertexMap[args] = AtomID(groundingData.newAtoms.size());
            groundingData.newAtoms.push_back(move(newAtom));
        }
    }
}

int32_t RuleDatabase::BodyHasher::hashBody(const vector<AtomLiteral>& body)
{
    int32_t hash = 0;
    for (const auto& it : body)
    {
        hash += eastl::hash<int32_t>()(it.id().value);
    }
    return hash;
}

bool RuleDatabase::BodyHasher::compareBodies(const vector<AtomLiteral>& lbody, const vector<AtomLiteral>& rbody, bool checkRelations)
{
    if (lbody.size() != rbody.size())
    {
        return false;
    }
    
    for (const auto& lbodyLit : lbody)
    {
        auto idx = indexOf(rbody.begin(), rbody.end(), lbodyLit);
        if (idx < 0)
        {
            return false;
        }
        // AtomLiteral.operator== doesn't check relationInfo.
        if (checkRelations && lbodyLit.getRelationInfo() != rbody[idx].getRelationInfo())
        {
            return false;
        }
    }

    return true;
}

RuleDatabase::ALiteral RuleDatabase::ConcreteAtomInfo::getLiteral(RuleDatabase& rdb, const AtomLiteral& atomLit)
{
    return getConcreteLiteral(rdb, !atomLit.sign());
}

Literal RuleDatabase::ConcreteAtomInfo::getConcreteLiteral(RuleDatabase& rdb, bool inverted)
{
    if (!equivalence.variable.isValid())
    {
        VarID var = rdb.m_solver.makeVariable(name, booleanVariableDomain);
        equivalence = Literal(var, TRUE_VALUE);
    }
    return inverted ? equivalence.inverted() : equivalence;
}

bool RuleDatabase::ConcreteAtomInfo::synchronize(RuleDatabase& rdb) const
{
    vxy_assert(status != ETruthStatus::Undetermined);
    if (!equivalence.variable.isValid())
    {
        // no variable created yet
        return true;
    }

    if (status == ETruthStatus::True)
    {
        if (!rdb.m_solver.getVariableDB()->constrainToValues(equivalence, nullptr))
        {
            rdb.setConflicted();
            return false;
        }
    }
    else if (status == ETruthStatus::False)
    {
        if (!rdb.m_solver.getVariableDB()->excludeValues(equivalence, nullptr))
        {
            rdb.setConflicted();
            return false;
        }
    }
    return true;
}

RuleDatabase::ALiteral RuleDatabase::AbstractAtomInfo::getLiteral(RuleDatabase& rdb, const AtomLiteral& atomLit)
{
    return atomLit.sign() ? atomLit.getRelationInfo()->literalRelation : atomLit.getRelationInfo()->getInverseRelation();
}

bool RuleDatabase::AbstractAtomInfo::synchronize(RuleDatabase& rdb) const
{
    // vxy_fail_msg("NYI");
    return true;
}

bool RuleDatabase::AbstractAtomInfo::synchronizeLiteral(RuleDatabase& rdb, const AbstractAtomRelationInfoPtr& litRelation, ETruthStatus status)
{
    vxy_assert(status != ETruthStatus::Undetermined);
    for (int vertex = 0; vertex < topology->getNumVertices(); ++vertex)
    {
        Literal lit;
        if (!litRelation->literalRelation->getRelation(vertex, lit))
        {
            continue;
        }

        if (status == ETruthStatus::True)
        {
            if (!rdb.m_solver.getVariableDB()->constrainToValues(lit, nullptr))
            {
                rdb.setConflicted();
                return false;
            }
        }
        else
        {
            if (!rdb.m_solver.getVariableDB()->excludeValues(lit, nullptr))
            {
                rdb.setConflicted();
                return false;
            }
        }
    }

    return true;
}

RuleDatabase::ALiteral RuleDatabase::ConcreteBodyInfo::getLiteral(RuleDatabase& rdb, bool allowCreation, bool inverted) const
{
    if (!equivalence.variable.isValid())
    {
        vxy_assert(allowCreation);
        
        wstring name = TEXT("body[[");
        bool first = true;
        for (auto& bodyLit : atomLits)
        {
            if (!first)
            {
                name.append(TEXT(", "));
            }
            first = false;

            auto atomInfo = rdb.getAtom(bodyLit.id());
            if (!bodyLit.sign())
            {
                name.append(TEXT("~"));
            }
            name.append(atomInfo->name);
        }
        name.append(TEXT("]]"));

        equivalence.variable = rdb.m_solver.makeBoolean(name);
        equivalence.values = TRUE_VALUE;
    }
    return inverted ? equivalence.inverted() : equivalence;
}

RuleDatabase::ALiteral RuleDatabase::AbstractBodyInfo::getLiteral(RuleDatabase& rdb, bool allowCreation, bool inverted) const
{
    if (bodyMapper == nullptr)
    {
        AbstractAtomRelationInfoPtr headRelationInfo = heads.empty() ? nullptr : heads[0].getRelationInfo();
        vxy_sanity(!containsPredicate(heads.begin(), heads.end(), [&](auto&& head) { return head.getRelationInfo() != headRelationInfo; } ));

        bodyMapper = make_shared<AbstractBodyMapper>(rdb, this, headRelationInfo);
    }

    if (allowCreation)
    {
        GraphLiteralRelationPtr rel = make_shared<BodyInstantiatorRelation>(bodyMapper, true);
        if (inverted)
        {
            rel = make_shared<InvertLiteralGraphRelation>(rel);
        }
        return rel;
    }
    
    if (relation == nullptr)
    {
        relation = make_shared<BodyInstantiatorRelation>(bodyMapper, false);
    }

    if (inverted)
    {
        if (invRelation == nullptr)
        {
            invRelation = make_shared<InvertLiteralGraphRelation>(relation);
        }
        return invRelation;
    }
    else
    {
        return relation;
    }
}

GraphLiteralRelationPtr RuleDatabase::AbstractBodyInfo::makeRelationForAbstractHead(RuleDatabase& rdb, const AbstractAtomRelationInfoPtr& headRelInfo)
{
    vxy_assert(bodyMapper != nullptr);
    vxy_assert(headRelInfo != nullptr);
    return make_shared<BoundBodyInstantiatorRelation>(bodyMapper, headRelInfo->argumentRelations);
}

/**
 *
 *
 * NoGoodBuilder
 *
 *
 */

void RuleDatabase::NogoodBuilder::add(const ALiteral& litToAdd, bool required, const ITopologyPtr& topology)
{
    auto concreteMatch = [&](const ALiteral& lit, VarID matchVar)
    {
        if (auto concreteLit = get_if<Literal>(&lit))
        {
            return concreteLit->variable == matchVar;
        }
        return false;
    };

    // If adding a concrete literal, check if an existing concrete literal already exists.
    // If so, merge the input values into it. Otherwise, we'll add the literal to our list.
    if (auto concreteLit = get_if<Literal>(&litToAdd))
    {
        required = true;
        
        auto found = find_if(m_literals.begin(), m_literals.end(), [&](auto&& checkLit)
        {
            return concreteMatch(checkLit.first, concreteLit->variable);
        });

        if (found != m_literals.end())
        {
            size_t offset = found - m_literals.begin();
            if (m_topologies[offset] != nullptr && m_topologies[offset] != topology)
            {
                m_topologies[offset] = nullptr;
            }
            get<Literal>(found->first).values.include(concreteLit->values);
            return;
        }
    }

    m_literals.push_back(make_pair(litToAdd, required));
    m_topologies.push_back(topology);
}

void RuleDatabase::NogoodBuilder::emit(RuleDatabase& rdb)
{
    ConstraintSolver& solver = rdb.getSolver();
    
    if (m_literals.empty())
    {
        return;
    }

    auto isAbstract = [&](const pair<ALiteral, bool>& lit)
    {
        return get_if<GraphLiteralRelationPtr>(&lit.first) != nullptr;
    };

    if (!containsPredicate(m_literals.begin(), m_literals.end(), isAbstract))
    {
        vector<Literal> concreteLits;
        for (auto& alitEntry : m_literals)
        {
            concreteLits.push_back(get<Literal>(alitEntry.first));
        }
        solver.makeConstraint<ClauseConstraint>(concreteLits);
    }
    else
    {
        ITopologyPtr topology = m_topologies[0];

        GraphCulledVector<GraphLiteralRelationPtr> abstractLits;
        for (int i = 0; i < m_literals.size(); ++i)
        {
            if (topology == nullptr)
            {
                topology = m_topologies[i];
            }
            else if (m_topologies[i] != nullptr && m_topologies[i] != topology)
            {
                topology = nullptr;
            }

            auto& alitEntry = m_literals[i];
            if (auto concreteLit = get_if<Literal>(&alitEntry.first))
            {
                abstractLits.push_back(make_pair(make_shared<ConstantGraphRelation<Literal>>(*concreteLit), true));
            }
            else
            {
                abstractLits.push_back(make_pair(get<GraphLiteralRelationPtr>(alitEntry.first), alitEntry.second));   
            }
        }

        if (topology != nullptr)
        {
            solver.makeGraphConstraint<ClauseConstraint>(topology, abstractLits);    
        }
        else
        {
            // Graph constraint, but conflicting topologies.
            // Theoretically this can be supported, but I don't think it's possible to create right now.
            vxy_fail();
        }
    }

    m_literals.clear();
    m_topologies.clear();
}

AbstractBodyMapper::AbstractBodyMapper(RuleDatabase& rdb, const RuleDatabase::AbstractBodyInfo* bodyInfo, const AbstractAtomRelationInfoPtr& headRelationInfo)
    : m_rdb(rdb)
    , m_headRelationInfo(headRelationInfo)
    , m_bodyInfo(bodyInfo)
{
}

bool AbstractBodyMapper::getForHead(const vector<int>& concreteHeadArgs, Literal& outLiteral)
{
    size_t argHash = RuleDatabase::ArgumentHasher()(concreteHeadArgs);
    auto found = m_bindMap.find_by_hash(concreteHeadArgs, argHash);
    if (found == m_bindMap.end())
    {
        return false;
    }
    
    outLiteral = found->second;
    return true;
}

Literal AbstractBodyMapper::makeForArgs(const vector<int>& args, size_t argHash)
{
    vxy_sanity(m_bindMap.find_by_hash(args, argHash) == m_bindMap.end());

    VarID varID = m_rdb.getSolver().makeBoolean(makeVarName(args));
    
    Literal lit(varID, SolverVariableDomain(0,1).getBitsetForValue(1));
    auto it = m_bindMap.insert(argHash, nullptr, {args, move(lit)}).first;
    return it->second;
}

bool AbstractBodyMapper::getForVertex(ITopology::VertexID vertex, bool allowCreation, Literal& outLit)
{
    vector<int>* args;
    if (m_headRelationInfo == nullptr)
    {
        // We won't have a head literal if this is a disallow statement.
        // In this case, we'll create a variable for any vertex in which the body's terms' relationships hold. 
        static vector<int> tempArray(1);
        tempArray[0] = vertex;
        args = &tempArray;
    }
    else
    {        
        // Take the head we're bound to and make it concrete.
        m_concrete.resize(m_headRelationInfo->argumentRelations.size());
        for (int i = 0; i < m_concrete.size(); ++i)
        {
            if (!m_headRelationInfo->argumentRelations[i]->getRelation(vertex, m_concrete[i]))
            {
                return false;
            }
        }

        args = &m_concrete;
    }

    size_t argHash = RuleDatabase::ArgumentHasher()(*args);
    auto found = m_bindMap.find_by_hash(*args, argHash);
    if (found != m_bindMap.end())
    {
        outLit = found->second;
        return true;
    }

    if (!allowCreation)
    {
        return false;
    }
    
    vxy_assert(m_headRelationInfo != nullptr || m_bodyInfo->isNegativeConstraint);
 
    outLit = makeForArgs(*args, argHash);
    return true;
}

wstring AbstractBodyMapper::makeVarName(const vector<int>& concreteHeadArgs) const
{
    wstring name = TEXT("graphBody[");
    for (int i = 0; i < concreteHeadArgs.size(); ++i)
    {
        if (i > 0) name += TEXT(", ");
        name += eastl::to_wstring(concreteHeadArgs[i]);
    }
    
    name += TEXT("] [[(");
        
    bool first = true;
    for (auto& head : m_bodyInfo->heads)
    {
        if (!first)
        {
            name.append(TEXT(", "));
        }
        first = false;

        name += litToString(head);
    }
    name.append(TEXT(") <<= "));

    first = true;
    for (auto& bodyLit : m_bodyInfo->atomLits)
    {
        if (!first)
        {
            name.append(TEXT(", "));
        }
        first = false;

        name += litToString(bodyLit);
    }
    name.append(TEXT("]]"));
    return name;
}

wstring AbstractBodyMapper::litToString(const AtomLiteral& lit) const
{
    wstring out;
       
    auto atomInfo = m_rdb.getAtom(lit.id());
    if (auto absAtomInfo = atomInfo->asAbstract())
    {
        auto litRel = get<GraphLiteralRelationPtr>(absAtomInfo->getLiteral(m_rdb, lit));
        out += litRel->toString();
    }
    else
    {
        if (!lit.sign())
        {
            out += TEXT("~");
        }
        out += atomInfo->name;
    }
    return out;
}

BoundBodyInstantiatorRelation::BoundBodyInstantiatorRelation(const shared_ptr<AbstractBodyMapper>& mapper, const vector<GraphVertexRelationPtr>& headRelations)
    : m_mapper(mapper)
    , m_headRelations(headRelations)
{
    m_name = TEXT("BodyMapper(");
    for (int i = 0; i < m_headRelations.size(); ++i)
    {
        if (i > 0) m_name += TEXT(", ");
        m_name += m_headRelations[i]->toString();
    }
    m_name += TEXT(")");
}

bool BoundBodyInstantiatorRelation::getRelation(VertexID sourceVertex, Literal& out) const
{
    m_concrete.resize(m_headRelations.size());
    for (int i = 0; i < m_concrete.size(); ++i)
    {            
        if (!m_headRelations[i]->getRelation(sourceVertex, m_concrete[i]))
        {
            return false;
        }
    }

    return m_mapper->getForHead(m_concrete, out);
}

size_t BoundBodyInstantiatorRelation::hash() const
{
    int hash = 0;
    for (auto& rel : m_headRelations)
    {
        hash = combineHashes(hash, rel->hash());
    }
    return hash;
}

bool BoundBodyInstantiatorRelation::equals(const IGraphRelation<Literal>& rhs) const
{
    // Only one of this created per body, so no need for deep equality inspection
    return &rhs == this;
}

wstring BoundBodyInstantiatorRelation::toString() const
{
    return m_name;
}

BodyInstantiatorRelation::BodyInstantiatorRelation(const shared_ptr<AbstractBodyMapper>& mapper, bool allowCreation)
    : m_mapper(mapper)
    , m_allowCreation(allowCreation)
{
}

bool BodyInstantiatorRelation::getRelation(VertexID sourceVertex, Literal& out) const
{
    return m_mapper->getForVertex(sourceVertex, m_allowCreation, out);
}

size_t BodyInstantiatorRelation::hash() const
{
    return 0;
}
