// Copyright Proletariat, Inc. All Rights Reserved.
#include "rules/RuleDatabase.h"

#include <EASTL/sort.h>

#include "ConstraintSolver.h"

using namespace Vertexy;

static constexpr bool LOG_CONCRETE_BODIES = false;
static constexpr bool LOG_ATOM_FACTS = false;

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
    // Ensure no more references to RuleDatabase exist after this function.
    TScopeExitCallback cb([&](){ lockVariableCreation(); });
    
    if (m_hasAbstract)
    {
        makeConcrete();
    }
    
    if (!propagateFacts())
    {
        setConflicted();
        return false;
    }

    auto db = m_solver.getVariableDB();

    //
    // Create variables for all potential bodies/heads. We do this by manually calling getLiteral() on each
    // head, which will create the solver variable if it doesn't exist yet.
    //
    for (auto& bodyInfo : m_bodies)
    {
        if (bodyInfo->asAbstract() != nullptr)
        {
            continue;
        }

        static Literal tempLit;
        for (auto& head : bodyInfo->heads)
        {
            auto headAtomInfo = getAtom(head.id())->asConcrete();
            
            if (headAtomInfo->abstractParent && !headAtomInfo->abstractParent->containsUnknowns(head.getMask()))
            {
                // This is a concrete instantiation of an abstract that is fully known.
                // No constraints will be created for the abstract, so we can safely skip creating a variable.
                continue;
            }

            // Fully-known heads can't always be skipped, because they can be necessary for graph constraint
            // construction. We can skip only if this head is NOT referred to by any bodies that aren't fully known.
            if (!headAtomInfo->containsUnknowns(head.getMask()))
            {
                auto& checkDeps = headAtomInfo->trueFacts.isSingleton()
                    ? headAtomInfo->positiveDependencies
                    : headAtomInfo->negativeDependencies;

                if (!containsPredicate(checkDeps.begin(), checkDeps.end(), [&](auto&& bodyDep){ return !bodyDep.body->isFullyKnown(); }))
                {
                    // All dependencies for the literal are fully known, which means it won't be necessary
                    // for any graph constraints, since those bodies are filtered out of any constraints we create.
                    continue;
                }
            }

            // Force the solver variable to be created.
            headAtomInfo->createLiteral(*this);
        }
    }

    //
    // Go through each body, constrain the body so it is true if all literals in the body are true, and false
    // if any literal in the body is false.
    //
    for (auto& bodyInfo : m_bodies)
    {
        if (bodyInfo->asConcrete() && bodyInfo->asConcrete()->abstractParent)
        {
            continue;
        }
        
        if (bodyInfo->isFullyKnown())
        {
            continue;
        }
       
        // Check if we can just encode this as a simple nogood.
        // Note the heads.empty() check - typically this is the case, but since we share BodyInfos for
        // statements that have identitical bodies, we could be both a negative constraint and have heads.
        // This is handled at the bottom of the outer loop.
        if (bodyInfo->isNegativeConstraint && bodyInfo->heads.empty())
        {
            for (auto& bodyLit : bodyInfo->atomLits)
            {
                if (isLiteralAssumed(bodyLit.id(), bodyLit.sign(), bodyLit.getMask()))
                {
                    // literal is known, no need to include.
                    continue;
                }
        
                auto bodyLitAtom = getAtom(bodyLit.id());
                
                ALiteral atomLit = bodyLitAtom->getLiteral(bodyLit.inverted());
                m_nogoodBuilder.add(atomLit, bodyLit.sign(), bodyLitAtom->getTopology());
            }
            m_nogoodBuilder.emit(*this, bodyInfo->getFilter());
            continue;
        }

        //
        // For all literals Bv1...BvN in the body:
        // nogood(-B, Bv1, Bv2, ..., BvN) == clause(B, -Bv1, -Bv2, ..., -BvN)
        //
        {
            auto filter = bodyInfo->getFilter();

            m_nogoodBuilder.add(bodyInfo->getLiteral(*this, true, false), true, bodyInfo->getTopology());
            for (auto& bodyLit : bodyInfo->atomLits)
            {
                if (isLiteralAssumed(bodyLit.id(), bodyLit.sign(), bodyLit.getMask()))
                {
                    continue;
                }

                auto bodyLitAtom = getAtom(bodyLit.id());
                ALiteral atomLit = bodyLitAtom->getLiteral(bodyLit.inverted());
                m_nogoodBuilder.add(atomLit, bodyLit.sign(), bodyLitAtom->getTopology());
            }

            m_nogoodBuilder.emit(*this, filter);
        }
        
        //
        // For each literal Bv in the body:
        // nogood(B, -Bv) == clause(-B, Bv)
        //
        for (auto& bodyLit : bodyInfo->atomLits)
        {
            if (isLiteralAssumed(bodyLit.id(), bodyLit.sign(), bodyLit.getMask()))
            {
                continue;
            }

            auto bodyLitAtom = getAtom(bodyLit.id());
            m_nogoodBuilder.add(bodyInfo->getLiteral(*this, false, true), true, bodyInfo->getTopology());            
            m_nogoodBuilder.add(bodyLitAtom->getLiteral(bodyLit), true, bodyLitAtom->getTopology());
            m_nogoodBuilder.emit(*this, bodyInfo->getFilter());
        }
        
        //
        // For each head H attached to this body:
        // nogood(-H, B) == clause(H, -B)
        //
        for (auto ith = bodyInfo->heads.begin(), ithEnd = bodyInfo->heads.end(); ith != ithEnd; ++ith)
        {
            if (isLiteralAssumed(ith->id(), ith->sign(), ith->getMask()))
            {
                continue;
            }
            vxy_assert(ith->sign());

            AtomInfo* headAtomInfo = getAtom((*ith).id());
            ALiteral headLit = headAtomInfo->getLiteral(*ith);
            
            m_nogoodBuilder.add(bodyInfo->getLiteral(*this, true, true), true, bodyInfo->getTopology());
            m_nogoodBuilder.add(headLit, true, headAtomInfo->getTopology());

            auto filter = FactGraphFilter::combine(headAtomInfo->getFilter(*ith), bodyInfo->getFilter());
            m_nogoodBuilder.emit(*this, filter);
        }

        // Edge case: a disallowed body that is also connected to a head.
        // E.g. a shared body 
        //    a() <<= foo();
        //    Program::disallow(foo());
        if (bodyInfo->isNegativeConstraint)
        {
            // Body can't be true. Instead of making a constraint for this, we simply set the
            // values of the variables representing the body to disallow "true".
            if (auto concreteBody = bodyInfo->asConcrete())
            {
                vxy_assert(concreteBody->equivalence.isValid());
                if (!db->excludeValues(concreteBody->equivalence, nullptr))
                {
                    setConflicted();
                    return false;
                }
            }
            else
            {
                auto absBody = bodyInfo->asAbstract();
                for (auto& concreteBodyEntry : absBody->concreteBodies)
                {
                    auto concreteInst = concreteBodyEntry.second;
                    if (concreteInst->status != ETruthStatus::Undetermined)
                    {
                        continue;
                    }

                    vxy_assert(concreteInst->equivalence.isValid());
                    if (!db->excludeValues(concreteInst->equivalence, nullptr))
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
        if (atomInfo->asConcrete() && atomInfo->asConcrete()->abstractParent)
        {
            continue;
        }
        
        m_nogoodBuilder.reserve(atomInfo->supports.size()+1);

        if (auto concreteAtom = atomInfo->asConcrete())
        {
            vector<vector<BodyInfo*>> indexLinkages;
            indexLinkages.resize(concreteAtom->domainSize);

            hash_map<ValueSet, vector<BodyInfo*>> bodyOverlaps;

            for (auto& supportBodyInfo : concreteAtom->supports)
            {
                if (supportBodyInfo.body->isFullyKnown() || isLiteralAssumed(atomInfo->id, true, supportBodyInfo.values))
                {
                    continue;
                }
                
                size_t valueHash = eastl::hash<ValueSet>()(supportBodyInfo.values);
                auto found = bodyOverlaps.find_by_hash(supportBodyInfo.values, valueHash);
                if (found == bodyOverlaps.end())
                {
                    found = bodyOverlaps.insert(valueHash, nullptr, {supportBodyInfo.values, vector<BodyInfo*>()}).first;
                }
                found->second.push_back(supportBodyInfo.body);
                
                for (auto itBit = supportBodyInfo.values.beginSetBits(), itEndBit = supportBodyInfo.values.endSetBits(); itBit != itEndBit; ++itBit)
                {
                    indexLinkages[*itBit].push_back(supportBodyInfo.body);
                }
            }

            for (auto& overlapEntry : bodyOverlaps)
            {
                if (!concreteAtom->containsUnknowns(overlapEntry.first))
                {
                    continue;
                }
                
                vector<BodyInfo*> bodiesForMask = overlapEntry.second;
                if (!overlapEntry.first.isSingleton())
                {
                    // include any bodies that partially overlap this mask
                    for (auto itBit = overlapEntry.first.beginSetBits(), itEndBit = overlapEntry.first.endSetBits(); itBit != itEndBit; ++itBit)
                    {
                        auto& linkages = indexLinkages[*itBit];
                        for (auto linkBody : linkages)
                        {
                            if (!linkBody->isFullyKnown() && !contains(bodiesForMask.begin(), bodiesForMask.end(), linkBody))
                            {
                                bodiesForMask.push_back(linkBody);
                            }
                        }
                    }
                }

                for (auto supportBody : bodiesForMask)
                {
                    // we should've been marked trivially true if one of our supports was,
                    // or it should've been removed as a support if it is trivially false.
                    vxy_assert(supportBody->status == ETruthStatus::Undetermined);
                    m_nogoodBuilder.add(supportBody->getLiteral(*this, false, false), false, supportBody->getTopology());
                }

                AtomLiteral headLit(atomInfo->id, false, overlapEntry.first);
                m_nogoodBuilder.add(concreteAtom->getLiteral(headLit), true, concreteAtom->getTopology());
                m_nogoodBuilder.emit(*this, nullptr);                
            }
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

                if (!abstractAtom->containsUnknowns(absLit.first.getMask()))
                {
                    continue;
                }

                auto filter = abstractAtom->getFilter(absLit.first);

                for (auto& bodyInfo : abstractAtom->supports)
                {
                    // Abstract atoms might have still have a body marked trivially true without the atom state
                    // being trivially true.
                    if (bodyInfo.body->status != ETruthStatus::Undetermined)
                    {
                        continue;
                    }

                    if (bodyInfo.body->isFullyKnown())
                    {
                        continue;
                    }

                    if (!bodyInfo.values.anyPossible(absLit.first.getMask()))
                    {
                        continue;
                    }
                    
                    if (auto abstractBody = bodyInfo.body->asAbstract())
                    {
                        auto bodyRel = abstractBody->makeRelationForAbstractHead(*this, absLit.first.getRelationInfo());                        
                        m_nogoodBuilder.add(bodyRel, false, bodyInfo.body->getTopology());
                    }
                    else
                    {
                        auto concreteBody = bodyInfo.body->asConcrete();
                        vxy_assert(concreteBody->equivalence.isValid());
                        m_nogoodBuilder.add(concreteBody->equivalence, false, bodyInfo.body->getTopology());
                    }
                }
                m_nogoodBuilder.add(make_shared<InvertLiteralGraphRelation>(absLit.first.getRelationInfo()->literalRelation), true, abstractAtom->getTopology());
                m_nogoodBuilder.emit(*this, filter);
            }
        }
    }

    if (!m_conflict)
    {
        computeSCCs();
    }

    return !m_conflict;
}

void RuleDatabase::lockVariableCreation()
{
    for (auto& body : m_bodies)
    {
        if (auto absBody = body->asAbstract())
        {
            absBody->lockVariableCreation();
        }
    }

    for (auto& atom : m_atoms)
    {
        if (auto absAtom = atom->asAbstract())
        {
            absAtom->lockVariableCreation();
        }
    }
}

bool RuleDatabase::isLiteralAssumed(AtomID atomID, bool sign, const ValueSet& mask) const
{
    auto atom = getAtom(atomID);
    auto status = atom->getTruthStatus(mask);
    if ((sign && status == ETruthStatus::False) || (!sign && status == ETruthStatus::True))
    {
        vxy_fail(); // we should've failed due to conflict already
        return false;
    }

    if ((sign && status == ETruthStatus::True) || (!sign && status == ETruthStatus::False))
    {
        return true;
    }

    auto concreteAtom = atom->asConcrete();
    if (concreteAtom != nullptr && concreteAtom->equivalence.isValid())
    {
        Literal lit = get<Literal>( concreteAtom->getLiteral(AtomLiteral(atomID, true, mask)) );        
        auto db = m_solver.getVariableDB();
        if ((sign && db->getPotentialValues(lit.variable).isSubsetOf(lit.values)) ||
            (!sign && !db->getPotentialValues(lit.variable).anyPossible(lit.values)))
        {
            return true;
        }
    }

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

        AtomLiteral trueLit(getTrueAtom(), true, ValueSet(1, true));
        newBodyInfo = findOrCreateBodyInfo(vector{trueLit}, topology, head.getRelationInfo(), needAbstract);
    }
    else
    {
        newBodyInfo = findOrCreateBodyInfo(body, topology, head.getRelationInfo(), false);
    }

    // Link the body to the head relying on it, and the head to the body supporting it.
    if (head.isValid())
    {
        linkHeadToBody(head, newBodyInfo);

        auto headInfo = getAtom(head.id());
        if (auto absHeadInfo = headInfo->asAbstract())
        {
            vxy_assert(head.getRelationInfo() != nullptr);
            auto truthStatus = isFact ? ETruthStatus::True : ETruthStatus::Undetermined;
            absHeadInfo->abstractLiterals.insert({head, truthStatus});
        }
        else if (isFact)
        {
            setAtomLiteralStatus(head.id(), head.getMask(), ETruthStatus::True);
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
        addAtomDependency(bodyValue, newBodyInfo);
    }
}

RuleDatabase::BodyInfo* RuleDatabase::findBodyInfo(const vector<AtomLiteral>& body, const AbstractAtomRelationInfoPtr& headRelationInfo, size_t& outHash) const
{
    outHash = BodyHasher::hashBody(body);
    auto range = m_bodySet.find_range_by_hash(outHash);
    for (auto it = range.first; it != range.second; ++it)
    {
        if (headRelationInfo == (*it)->headRelationInfo && BodyHasher::compareBodies((*it)->atomLits, body))
        {
            return *it;
        }
    }
    return nullptr;
}

RuleDatabase::BodyInfo* RuleDatabase::findOrCreateBodyInfo(const vector<AtomLiteral>& body, const ITopologyPtr& topology, const AbstractAtomRelationInfoPtr& headRelationInfo, bool forceAbstract)
{
    vxy_assert(!body.empty());

    size_t hash;
    if (auto found = findBodyInfo(body, headRelationInfo, hash))
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


void RuleDatabase::linkHeadToBody(const AtomLiteral& headLit, BodyInfo* body)
{
    vxy_assert(headLit.sign());
    auto headInfo = getAtom(headLit.id());
    
    auto foundExistingHead = find_if(body->heads.begin(), body->heads.end(), [&](auto&& h) { return h.id() == headLit.id(); });
    if (foundExistingHead != body->heads.end())
    {
        foundExistingHead->includeMask(headLit.getMask());
        
        auto foundExistingSupport = find_if(headInfo->supports.begin(), headInfo->supports.end(), [&](auto&& bodyLink) { return bodyLink.body == body; });
        vxy_assert(foundExistingSupport != headInfo->supports.end());
        foundExistingSupport->values.include(headLit.getMask());
    }
    else
    {
        headInfo->supports.push_back({headLit.getMask(), body});
        body->heads.push_back(headLit);
    }
}

void RuleDatabase::addAtomDependency(const AtomLiteral& bodyLit, BodyInfo* body)
{
    auto atomInfo = getAtom(bodyLit.id());
    auto& deps = bodyLit.sign() ? atomInfo->positiveDependencies : atomInfo->negativeDependencies;

    auto foundExistingDep = find_if(deps.begin(), deps.end(), [&](auto&& bodyLink) { return bodyLink.body == body; });
    if (foundExistingDep != deps.end())
    {
        foundExistingDep->values.include(bodyLit.getMask());
    }
    else
    {
        deps.push_back({bodyLit.getMask(), body});
    }
}

bool RuleDatabase::getLiteralForBody(const AbstractBodyInfo& body, const vector<int>& headArguments, Literal& outLit)
{
    auto it = body.concreteBodies.find(headArguments);
    if (it == body.concreteBodies.end())
    {
        return false;
    }

    ConcreteBodyInfo* concreteBody = it->second;
    
    outLit = concreteBody->equivalence;
    if (!outLit.isValid())
    {
        wstring name = TEXT("graphBody(");
        for (int i = 0; i < headArguments.size(); ++i)
        {
            if (i > 0) name += TEXT(", ");
            // !!FIXME!! This isn't quite right: we're just assuming the first index is a vertex and remaining
            // indices are not.
            name += i == 0 ? body.getTopology()->vertexIndexToString(headArguments[i]) : to_wstring(headArguments[i]);
        }
        name += TEXT(") [[");
        name += literalsToString(concreteBody->atomLits, false);
        name += TEXT("]]");

        VarID newVar = m_solver.makeBoolean(name);
        outLit = Literal(newVar, SolverVariableDomain(0,1).getBitsetForValue(1));

        auto db = m_solver.getVariableDB();
        if ((concreteBody->status == ETruthStatus::True && !db->constrainToValues(outLit, nullptr)) ||
            (concreteBody->status == ETruthStatus::False && !db->excludeValues(outLit, nullptr)))
        {
            setConflicted();
        }

        concreteBody->equivalence = outLit;
    }    
    return true; 
}

bool RuleDatabase::getConcreteArgumentsForRelation(const AbstractAtomRelationInfoPtr& relationInfo, int vertex, vector<int>& outArgs)
{
    outArgs.clear();    
    if (relationInfo->argumentRelations.empty())
    {
        outArgs.push_back(vertex);
    }
    else
    {
        outArgs.reserve(relationInfo->argumentRelations.size());
        for (auto& arg : relationInfo->argumentRelations)
        {
            int resolved;
            if (!arg->getRelation(vertex, resolved))
            {
                break;
            }
            outArgs.push_back(resolved);                
        }

        if (outArgs.size() != relationInfo->argumentRelations.size())
        {
            return false;
        }
    }

    return true;    
}

AtomID RuleDatabase::getTrueAtom()
{
    if (m_trueAtom.isValid())
    {
        return m_trueAtom;
    }
    m_trueAtom = createAtom(TEXT("<true-fact>"), 1, true);

    setAtomLiteralStatus(m_trueAtom, ValueSet(1,true), ETruthStatus::True);
    return m_trueAtom;
}

AtomID RuleDatabase::createAtom(const wchar_t* name, int domainSize, bool external)
{
    AtomID newAtom(m_atoms.size());

    m_atoms.push_back(make_unique<ConcreteAtomInfo>(newAtom, domainSize));
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
    if (external)
    {
        setAtomLiteralStatus(newAtom,ValueSet(domainSize, true), ETruthStatus::True);
    }
    
    return newAtom;
}

AtomID RuleDatabase::createBoundAtom(const AtomBinder& binder, const wchar_t* name, int domainSize, bool external)
{
    AtomID newAtom = createAtom(name, domainSize, external);

    auto newAtomInfo = m_atoms[newAtom.value]->asConcrete();
    vxy_assert_msg(newAtomInfo != nullptr, "expected concrete atom");

    newAtomInfo->binder = binder;
    return newAtom;
}

AtomID RuleDatabase::createAbstractAtom(const ITopologyPtr& topology, const wchar_t* name, int domainSize, bool external)
{
    AtomID newAtom(m_atoms.size());

    m_hasAbstract = true;
    
    m_atoms.push_back(make_unique<AbstractAtomInfo>(newAtom, domainSize, topology));
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
                getAtom(lastAtom)->scc = nextSCC;
            }
            else
            {
                lastBody = (*it) - (m_atoms.size()-1);
                m_bodies[lastBody]->scc = nextSCC;
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
                getAtom(lastAtom)->scc = -1;
            }
            else
            {
                m_bodies[lastBody]->scc = -1;
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
        auto atomInfo = getAtom(atom)->asConcrete();
        if (atomInfo == nullptr || !atomInfo->isChoiceAtom())
        {
            return;
        }
        
        // for each body where this atom occurs (as positive)...
        for (auto& refBodyInfo : atomInfo->positiveDependencies)
        {
            visitor(m_atoms.size()-1 + refBodyInfo.body->id);
        }
    }
    else
    {
        auto refBodyInfo = m_bodies[node - (m_atoms.size()-1)]->asConcrete();
        if (refBodyInfo == nullptr)
        {
            return;    
        }
        
        // visit each head that this body is supporting.
        for (auto& head : refBodyInfo->heads)
        {
            if (!getAtom(head.id())->containsUnknowns(head.getMask()))
            {
                continue;
            }
            visitor(head.id().value-1);
        }
    }
}

bool RuleDatabase::setAtomLiteralStatus(AtomID atomID, const ValueSet& mask, ETruthStatus status)
{
    if (status == ETruthStatus::Undetermined)
    {
        return true;
    }

    auto atom = getAtom(atomID)->asConcrete();
    vxy_assert(atom != nullptr);
   
    auto& setBits = status == ETruthStatus::True ? atom->trueFacts : atom->falseFacts;
    auto& oppBits = status == ETruthStatus::True ? atom->falseFacts : atom->trueFacts;

    // Handling masks:
    // foo.mask(0);  <<-- sets 1st bit as true, 2nd bit as false.
    // foo.mask(0|1); <<-- overlaps false bits. Remove false bits (2nd) from mask. Remainder overlaps true bits (1st): OK.
    // foo.mask(1); <<- overlaps false bits. Remove false bits (2nd) from mask. Remainder doesn't overlap true bits (1st): conflict.

    ValueSet finalMask = mask;
    if (finalMask.anyPossible(oppBits))
    {
        finalMask.exclude(oppBits);
        if (!finalMask.anyPossible(setBits))
        {
            setConflicted();
            return false;
        }
    }
    
    finalMask.exclude(setBits);
    if (finalMask.isZero())
    {
        return true;
    }
    
    setBits.include(finalMask);
    oppBits.include(finalMask.inverted());

    if constexpr (LOG_ATOM_FACTS)
    {
        VERTEXY_LOG("Atom %d %s%s -> %s",
            atom->id.value,
            atom->name.c_str(),
            finalMask.toString().c_str(),
            status == ETruthStatus::True ? TEXT("TRUE") : TEXT("FALSE")
        );
    }
    
    if (!atom->synchronize(*this))
    {
        setConflicted();
        return false;
    }

    if (!atom->enqueued)
    {
        atom->enqueued = true;
        m_atomsToPropagate.push_back(atom);
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
            body->numUndeterminedTails = -1;

            if constexpr (LOG_ATOM_FACTS)
            {
                VERTEXY_LOG("Body %d %s -> %s",
                    body->id,
                    literalsToString(body->atomLits, false).c_str(),
                    status == ETruthStatus::True ? TEXT("TRUE") : TEXT("FALSE")
                );
            }
            
            if (body->equivalence.isValid())
            {
                auto db = m_solver.getVariableDB();
                if ((status == ETruthStatus::True && !db->constrainToValues(body->equivalence, nullptr)) ||
                    (status == ETruthStatus::False && !db->excludeValues(body->equivalence, nullptr)))
                {
                    setConflicted();
                    return false;
                }
            }

            if (body->abstractParent != nullptr)
            {
                vxy_assert(body->abstractParent->numUnknownConcretes > 0);
                body->abstractParent->numUnknownConcretes--;
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
        auto concreteAtom = atom->asConcrete();
        if (concreteAtom == nullptr)
        {
            continue;
        }
        
        if (!concreteAtom->trueFacts.isZero() || !concreteAtom->falseFacts.isZero())
        {
            if (!concreteAtom->enqueued)
            {
                concreteAtom->enqueued = true;
                m_atomsToPropagate.push_back(concreteAtom);
            }
        }
        else if (!concreteAtom->isExternal)
        {
            // Find any indices in this atom that are unsupported.
            ValueSet supportedIndices(concreteAtom->domainSize, false);
            for (auto& supportLink : concreteAtom->supports)
            {
                supportedIndices.include(supportLink.values);
                if (!supportedIndices.contains(false))
                {
                    break;
                }
            }

            if (supportedIndices.contains(false))
            {
                if (!setAtomLiteralStatus(concreteAtom->id, supportedIndices.inverted(), ETruthStatus::False))
                {
                    return false;
                }
            }
        }
    }

    // mark any bodies that are definitely true or false
    for (auto& body : m_bodies)
    {
        if (!body->asConcrete())
        {
            continue;
        }
        
        bool fact = true;
        for (auto& lit : body->atomLits)
        {
            auto litAtom = getAtom(lit.id())->asConcrete();
            auto status = litAtom->getTruthStatus(lit.getMask());
            if ((lit.sign() && status == ETruthStatus::False) || (!lit.sign() && status == ETruthStatus::True))
            {
                setBodyStatus(body->asConcrete(), ETruthStatus::False);
                fact = false;
                break;
            }
            else if (status == ETruthStatus::Undetermined)
            {
                fact = false;
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
    
    //
    // Debug output:
    //
    
    if (LOG_CONCRETE_BODIES)
    {
        VERTEXY_LOG("Concrete bodies:");
        for (auto& body : m_bodies)
        {
            auto concreteBody = body->asConcrete();
            if (concreteBody == nullptr || concreteBody->status == ETruthStatus::False)
            {
                continue;
            }
            
            if (concreteBody->isNegativeConstraint)
            {
                VERTEXY_LOG("%d: disallow(%s)", concreteBody->id, literalsToString(concreteBody->atomLits).c_str());
            }

            for (auto& head : body->heads)
            {
                VERTEXY_LOG("%d: %s << %s", concreteBody->id, literalToString(head).c_str(), literalsToString(concreteBody->atomLits).c_str());
            }
        }
    }
    
    return true;
}

wstring RuleDatabase::literalToString(const AtomLiteral& lit) const
{
    wstring out;
    if (!lit.sign()) out += TEXT("~");
    auto atom = getAtom(lit.id());
    out += atom->name;
    if (atom->domainSize > 1)
    {
        out += lit.getMask().toString();
    }
    return out;
}

wstring RuleDatabase::literalsToString(const vector<AtomLiteral>& lits, bool cullKnown) const
{
    wstring out;
    bool first = true; 
    for (auto& lit : lits)
    {
        auto atomInfo = getAtom(lit.id());
        if (lit.getMask().isSubsetOf(atomInfo->trueFacts))
        
        if (auto concreteAtom = getAtom(lit.id())->asConcrete())
        {
            auto status = concreteAtom->getTruthStatus(lit.getMask());
            if (status != ETruthStatus::Undetermined && cullKnown)
            {
                continue;
            }
        }
        if (!first)
        {
            out += TEXT(", ");
        }
        first = false;
        out += literalToString(lit);
    }
    return out;
}

bool RuleDatabase::emptyAtomQueue()
{
    while (!m_atomsToPropagate.empty())
    {
        auto atom = m_atomsToPropagate.back();
        m_atomsToPropagate.pop_back();

        vxy_assert(atom->enqueued);
        atom->enqueued = false;

        for (auto& posDepLink : atom->positiveDependencies)
        {
            auto status = atom->getTruthStatus(posDepLink.values);
            
            if (status == ETruthStatus::False)
            {
                if (!setBodyStatus(posDepLink.body->asConcrete(), ETruthStatus::False))
                {
                    return false;
                }
            }
            else if (status == ETruthStatus::True && posDepLink.body->numUndeterminedTails >= 0)
            {
                vxy_assert(posDepLink.body->numUndeterminedTails > 0);
                posDepLink.body->numUndeterminedTails--;
                if (posDepLink.body->numUndeterminedTails == 0)
                {
                    if (!setBodyStatus(posDepLink.body->asConcrete(), ETruthStatus::True))
                    {
                        return false;
                    }
                }
            }
        }

        for (auto& negDepLink : atom->negativeDependencies)
        {
            auto status = atom->getTruthStatus(negDepLink.values);
                
            if (status == ETruthStatus::True)
            {
                if (!setBodyStatus(negDepLink.body->asConcrete(), ETruthStatus::False))
                {
                    return false;
                }
            }
            else if (status == ETruthStatus::False && negDepLink.body->numUndeterminedTails >= 0)
            {
                vxy_assert(negDepLink.body->numUndeterminedTails > 0);
                negDepLink.body->numUndeterminedTails--;
                if (negDepLink.body->numUndeterminedTails == 0)
                {
                    if (!setBodyStatus(negDepLink.body->asConcrete(), ETruthStatus::True))
                    {
                        return false;
                    }
                }
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
                if (!setAtomLiteralStatus(it->id(), it->getMask(), ETruthStatus::True))
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
                vxy_assert(containsPredicate(atom->supports.begin(), atom->supports.end(), [&](auto&& link) { return link.body == body; }));

                auto foundBody = find_if(atom->supports.begin(), atom->supports.end(), [&](auto&& link) { return link.body == body; });
                atom->supports.erase_unsorted(foundBody);

                ValueSet remainingSupportedIndices(atom->domainSize, false);
                for (auto& link : atom->supports)
                {
                    remainingSupportedIndices.include(link.values);
                    if (!remainingSupportedIndices.contains(false))
                    {
                        break;
                    }
                }

                if (remainingSupportedIndices.contains(false))
                {                    
                    if (!setAtomLiteralStatus(it->id(), remainingSupportedIndices.inverted(), ETruthStatus::False))
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
    GroundingData groundingData(m_atoms.size(), m_bodies.size());
   
    //
    // First ground all heads
    //
    
    vector<AbstractBodyInfo*> absBodies;
    for (auto& body : m_bodies)
    {
        auto bodyInfo = body.get();
        if (auto absBody = bodyInfo->asAbstract())
        {
            absBodies.push_back(absBody);
        }
        
        for (auto& atomLit : bodyInfo->heads)
        {
            groundAtomToConcrete(atomLit, groundingData);
        }
    }

    //
    // Now create the bodies and connect the heads/values.
    //

    for (auto& body : absBodies)
    {
        groundBodyToConcrete(*body, groundingData);
    }
}

vector<AtomLiteral> RuleDatabase::groundLiteralsToConcrete(int vertex, const vector<AtomLiteral>& oldLits, GroundingData& groundingData, bool& outSomeFailed)
{
    outSomeFailed = false;
    
    vector<AtomLiteral> newLits;
    vector<int> args;

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
        if (auto concreteAtomInfo = oldAtomInfo->asConcrete())
        {
            if (concreteAtomInfo->isExternal)
            {
                vxy_assert(concreteAtomInfo->getTruthStatus(oldLit.getMask()) == ETruthStatus::True);
                if (!oldLit.sign())
                {
                    outSomeFailed = true;
                }
            }
            else
            {
                newLits.push_back(oldLit);
            }
        }
        else
        {
            auto oldAbsInfo = oldAtomInfo->asAbstract();

            auto& relationInfo = oldLit.getRelationInfo();
            if (!getConcreteArgumentsForRelation(relationInfo, vertex, args))
            {
                if (oldLit.sign())
                {
                    outSomeFailed = true;
                }
                continue;
            }

            if (oldAbsInfo->isExternal)
            {
                auto rel = get<GraphLiteralRelationPtr>(oldAbsInfo->getLiteral(oldLit));

                Literal lit;
                if (!rel->getRelation(vertex, lit))
                {
                    if (oldLit.sign())
                    {
                        outSomeFailed = true;
                    }
                }
                else if (!m_solver.getVariableDB()->anyPossible(lit))
                {
                    outSomeFailed = true;
                }

                continue;
            }
            
            auto& mappings = oldAbsInfo->concreteAtoms; 
            auto found = mappings.find(args);
            if (found == mappings.end())
            {
                // No head defining this atom
                if (oldLit.sign())
                {
                    outSomeFailed = true;
                }

                continue;
            }

            AtomLiteral newLit(found->second->id, oldLit.sign(), oldLit.getMask(), oldLit.getRelationInfo());

            auto foundAtom = found->second;
            if (auto status = foundAtom->getTruthStatus(newLit.getMask());
                (newLit.sign() && status == ETruthStatus::False) || (!newLit.sign() && status == ETruthStatus::True))
            {
                outSomeFailed = true;
            }

            newLits.push_back(newLit);
        }
    }
    
    return newLits;
}

void RuleDatabase::groundBodyToConcrete(BodyInfo& oldBody, GroundingData& groundingData)
{
    if (auto oldAbstractBody = oldBody.asAbstract())
    {
        // Abstract body needs to be instantiated for every vertex of the topology.        
        auto& bodyMapping = groundingData.bodyMappings[oldBody.id];
        bodyMapping.resize(oldAbstractBody->topology->getNumVertices(), -1);

        for (int vertex = 0; vertex < oldAbstractBody->topology->getNumVertices(); ++vertex)
        {
            vector<int> headArgs;
            if (oldAbstractBody->headRelationInfo == nullptr)
            {
                headArgs.push_back(vertex);
            }
            else if (!oldAbstractBody->getHeadArgumentsForVertex(vertex, headArgs))
            {
                continue;
            }

            bool someValsFailed;
            vector<AtomLiteral> newValues = groundLiteralsToConcrete(vertex, oldAbstractBody->atomLits, groundingData, someValsFailed);
            if (someValsFailed)
            {
                continue;
            }
            
            bool someHeadsFailed;
            vector<AtomLiteral> newHeads = groundLiteralsToConcrete(vertex, oldAbstractBody->heads, groundingData, someHeadsFailed);
            if (newHeads.empty() && !oldAbstractBody->heads.empty())
            {
                continue;
            }

            const bool hasUntrueLits = containsPredicate(newValues.begin(), newValues.end(), [&](auto&& lit)
            {
                auto status = getAtom(lit.id())->asConcrete()->getTruthStatus(lit.getMask());
                return ((lit.sign() && status != ETruthStatus::True) || (!lit.sign() && status == ETruthStatus::False));
            });
            if (!hasUntrueLits)
            {
                for (auto& head : newHeads)
                {
                    setAtomLiteralStatus(head.id(), head.getMask(), ETruthStatus::True);
                }
            }
            
            size_t hash;
            if (auto existingBody = findBodyInfo(newValues, nullptr, hash))
            {
                for (auto& head : newHeads)
                {
                    linkHeadToBody(head, existingBody);
                }
                oldAbstractBody->concreteBodies.insert({move(headArgs), existingBody->asConcrete()});
                if (existingBody->status == ETruthStatus::Undetermined)
                {
                    oldAbstractBody->numUnknownConcretes++;
                }
            }
            else
            {
                auto newBody = make_unique<ConcreteBodyInfo>();
                newBody->id = m_bodies.size();
                newBody->isNegativeConstraint = oldAbstractBody->isNegativeConstraint;
                newBody->atomLits = move(newValues);
                newBody->status = oldAbstractBody->status;
                newBody->abstractParent = oldAbstractBody;
                newBody->parentVertex = vertex;

                oldAbstractBody->numUnknownConcretes++;
                setBodyStatus(newBody.get(), oldAbstractBody->status);
                
                hookupGroundedDependencies(newHeads, newBody.get(), groundingData);

                oldAbstractBody->concreteBodies.insert({move(headArgs), newBody.get()});
                
                bodyMapping[vertex] = newBody->id;
                m_bodySet.insert(hash, nullptr, newBody.get());
                m_bodies.push_back(move(newBody));
            }
        }
    }
}

void RuleDatabase::hookupGroundedDependencies(const vector<AtomLiteral>& newHeads, ConcreteBodyInfo* newBodyInfo, GroundingData& groundingData)
{
    for (auto& headLit : newHeads)
    {
        vxy_assert(headLit.sign());
        linkHeadToBody(headLit, newBodyInfo);
    }

    for (auto& bodyLit : newBodyInfo->atomLits)
    {
        addAtomDependency(bodyLit, newBodyInfo);
    }
    newBodyInfo->numUndeterminedTails = newBodyInfo->atomLits.size();
}

void RuleDatabase::groundAtomToConcrete(const AtomLiteral& oldAtom, GroundingData& groundingData)
{
    vxy_assert(oldAtom.sign());
    
    if (auto oldAbstractAtom = getAtom(oldAtom.id())->asAbstract())
    {
        auto atomRel = oldAtom.getRelationInfo()->literalRelation;
        vxy_assert(!oldAbstractAtom->isExternal);
        
        auto& relationInfo = oldAtom.getRelationInfo();
        auto litEntry = oldAbstractAtom->abstractLiterals.find(oldAtom);

        vector<int> args;

        auto& vertexMap = oldAbstractAtom->concreteAtoms;
        for (int vertex = 0; vertex < oldAbstractAtom->topology->getNumVertices(); ++vertex)
        {
            if (!getConcreteArgumentsForRelation(relationInfo, vertex, args))
            {
                continue;
            }

            ETruthStatus truthStatus = litEntry->second;
            if (auto found = vertexMap.find(args);
                found != vertexMap.end())
            {
                setAtomLiteralStatus(found->second->id, oldAtom.getMask(), truthStatus);
                continue;
            }

            wstring name = oldAbstractAtom->name + TEXT("(");
            for (int i = 0; i < args.size(); ++i)
            {
                if (i > 0) name += TEXT(", ");
                // !!FIXME!! This isn't quite right: we're just assuming the first index is a vertex and remaining
                // indices are not.
                name += i == 0 ? oldAbstractAtom->getTopology()->vertexIndexToString(args[i]) : to_wstring(args[i]);
            }
            name += TEXT(")");

            AtomID newAtomID(m_atoms.size());
            auto newAtom = make_unique<ConcreteAtomInfo>(newAtomID, oldAbstractAtom->domainSize);
            newAtom->name = name;
            newAtom->isExternal = oldAbstractAtom->isExternal;
            newAtom->abstractParent = oldAbstractAtom;
            newAtom->parentVertex = vertex;
            newAtom->parentRelationInfo = oldAtom.getRelationInfo();
            vxy_assert(newAtom->parentRelationInfo != nullptr);
            
            oldAbstractAtom->concreteAtoms.insert({args, newAtom.get()});
            m_atoms.push_back(move(newAtom));

            setAtomLiteralStatus(newAtomID, oldAtom.getMask(), truthStatus);            
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

bool RuleDatabase::BodyHasher::compareBodies(const vector<AtomLiteral>& lbody, const vector<AtomLiteral>& rbody)
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
    }

    return true;
}

RuleDatabase::AtomInfo::AtomInfo(AtomID id, int domainSize)
    : id(id)
    , domainSize(domainSize)
    , trueFacts(domainSize, false)
    , falseFacts(domainSize, false)
{
    vxy_assert(domainSize >= 1);
}

RuleDatabase::ALiteral RuleDatabase::ConcreteAtomInfo::getLiteral(const AtomLiteral& atomLit) const
{
    vxy_assert_msg(abstractParent == nullptr, "Should not call getLiteral on a concrete atom created from an abstract!");
   
    if (binder != nullptr)
    {
        Literal lit = binder(atomLit.getMask());
        return atomLit.sign() ? lit : lit.inverted();
    }
    else
    {
        vxy_assert(equivalence.isValid());
        ValueSet mask(1, false);
        mask.append(atomLit.getMask(), domainSize);
        return Literal(equivalence.variable, atomLit.sign() ? mask : mask.inverted());
    }
}

Literal RuleDatabase::ConcreteAtomInfo::getLiteralForIndex(int index) const
{
    vxy_assert(getTruthStatus(index) == ETruthStatus::Undetermined);
    if (abstractParent != nullptr)
    {
        vxy_assert(binder == nullptr);
        ValueSet mask(domainSize, false);
        mask[index] = true;

        AtomLiteral parentLit(abstractParent->id, true, mask, parentRelationInfo);
        auto rel = get<GraphLiteralRelationPtr>(abstractParent->getLiteral(parentLit));

        Literal outLit;
        vxy_verify(rel->getRelation(parentVertex, outLit));
        return outLit;
    }
    else if (binder != nullptr)
    {
        ValueSet mask(domainSize, false);
        mask[index] = true;
        return binder(mask);
    }
    else
    {
        vxy_assert(equivalence.isValid());
        ValueSet mask(domainSize+1, false);
        mask[index+1] = true;
        return Literal(equivalence.variable, mask);
    }
}

bool RuleDatabase::ConcreteAtomInfo::containsUnknowns(const ValueSet& values) const
{
    return !values.excluding(trueFacts).excluding(falseFacts).isZero();
}

RuleDatabase::ETruthStatus RuleDatabase::ConcreteAtomInfo::getTruthStatus(const ValueSet& values) const
{
    vxy_sanity(!values.isZero());
    // Definitely true if all definitely-true bits are included in the mask. 
    if (!trueFacts.isZero() && trueFacts.isSubsetOf(values))
    {
        return ETruthStatus::True;
    }
    // Definitely false if all bits in the mask are definitely-false.
    else if (values.isSubsetOf(falseFacts))
    {
        return ETruthStatus::False;
    }
    return ETruthStatus::Undetermined;
}

RuleDatabase::ETruthStatus RuleDatabase::ConcreteAtomInfo::getTruthStatus(int index) const
{
    if (index < 0 || index >= domainSize)
    {
        return ETruthStatus::False;
    }

    if (trueFacts[index])
    {
        return ETruthStatus::True;
    }
    else if (falseFacts[index])
    {
        return ETruthStatus::False;
    }

    return ETruthStatus::Undetermined;
}

void RuleDatabase::ConcreteAtomInfo::createLiteral(RuleDatabase& rdb)
{
    if (!equivalence.variable.isValid())
    {
        if (abstractParent != nullptr)
        {            
            vxy_assert(binder == nullptr);
            AtomLiteral parentLit(abstractParent->id, true, ValueSet(domainSize, true), parentRelationInfo);
            auto rel = get<GraphLiteralRelationPtr>(abstractParent->getLiteral(parentLit));
            vxy_verify(rel->getRelation(parentVertex, equivalence));
        }
        else if (binder != nullptr)
        {
            equivalence = binder(ValueSet(domainSize, true));
        }
        else
        {
            VarID var = rdb.m_solver.makeVariable(name, SolverVariableDomain(0, domainSize));
            equivalence = Literal(var, ValueSet(domainSize+1, true));
        }

        vxy_assert(equivalence.isValid());
        if (!synchronize(rdb))
        {
            rdb.setConflicted();
        }
    }
}

bool RuleDatabase::ConcreteAtomInfo::synchronize(RuleDatabase& rdb)
{
    if (trueFacts.isZero() && falseFacts.isZero())
    {
        return true;
    }
    
    Literal trueEquivalence, falseEquivalence;
    if (parentRelationInfo != nullptr)
    {
        vxy_assert(binder == nullptr);
        parentRelationInfo->literalRelation->instantiateNecessary(parentVertex, trueFacts, trueEquivalence);
        parentRelationInfo->literalRelation->instantiateNecessary(parentVertex, falseFacts, falseEquivalence);
    }
    else if (binder != nullptr)
    {
        trueEquivalence = binder(trueFacts);
        falseEquivalence = binder(falseFacts);
    }
    else if (equivalence.isValid())
    {
        trueEquivalence.variable = equivalence.variable;
        trueEquivalence.values = ValueSet(1, false);
        trueEquivalence.values.append(trueFacts, trueFacts.size());

        falseEquivalence.variable = equivalence.variable;
        falseEquivalence.values = ValueSet(1, true);
        falseEquivalence.values.append(falseFacts, falseFacts.size());
    }

    if (trueEquivalence.isValid() && !rdb.m_solver.getVariableDB()->constrainToValues(trueEquivalence, nullptr))
    {
        rdb.setConflicted();
        return false;
    }
    if (falseEquivalence.isValid() && !rdb.m_solver.getVariableDB()->excludeValues(falseEquivalence, nullptr))
    {
        rdb.setConflicted();
        return false;
    }

    return true;
}

RuleDatabase::AbstractAtomInfo::AbstractAtomInfo(AtomID inID, int inDomainSize, const ITopologyPtr& topology)
    : AtomInfo(inID, inDomainSize)
    , topology(topology)
    , indicesWithConcretes(inDomainSize, false)
{
}

RuleDatabase::ALiteral RuleDatabase::AbstractAtomInfo::getLiteral(const AtomLiteral& atomLit) const
{
    return atomLit.sign() ? atomLit.getRelationInfo()->literalRelation : atomLit.getRelationInfo()->getInverseRelation();
}

FactGraphFilterPtr RuleDatabase::AbstractAtomInfo::getFilter(const AtomLiteral& literal) const
{
    return make_shared<FactGraphFilter>(this, literal.getMask(), literal.getRelationInfo());
}

bool RuleDatabase::AbstractAtomInfo::containsUnknowns(const ValueSet& values) const
{
    for (auto& concreteAtomEntry : concreteAtoms)
    {
        if (concreteAtomEntry.second->containsUnknowns(values))
        {
            return true;
        }
    }
    return false;
}

RuleDatabase::ETruthStatus RuleDatabase::AbstractAtomInfo::getTruthStatus(const ValueSet& values) const
{
    ETruthStatus currentStatus = ETruthStatus::Undetermined;
    for (auto& concreteAtomEntry : concreteAtoms)
    {
        auto atomStatus = concreteAtomEntry.second->getTruthStatus(values);
        if (atomStatus == ETruthStatus::Undetermined || (currentStatus != ETruthStatus::Undetermined && atomStatus != currentStatus))
        {
            return ETruthStatus::Undetermined;
        }
        currentStatus = atomStatus;
    }
    return currentStatus;
}

void RuleDatabase::AbstractAtomInfo::lockVariableCreation()
{
    for (auto& absLit : abstractLiterals)
    {
        absLit.first.getRelationInfo()->literalRelation->lockVariableCreation();
    }
}

RuleDatabase::ALiteral RuleDatabase::ConcreteBodyInfo::getLiteral(RuleDatabase& rdb, bool allowCreation, bool inverted) const
{
    if (!equivalence.variable.isValid())
    {
        if (abstractParent != nullptr)
        {
            auto rel = get<GraphLiteralRelationPtr>(abstractParent->getLiteral(rdb, allowCreation, false));
            vxy_verify(rel->getRelation(parentVertex, equivalence));
        }
        else
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
        if (createRelation == nullptr)
        {
            createRelation = make_shared<BodyInstantiatorRelation>(bodyMapper, true);
        }

        if (inverted)
        {
            if (createInverseRelation == nullptr)
            {
                createInverseRelation = make_shared<InvertLiteralGraphRelation>(createRelation);
            }
            return createInverseRelation;
        }
        else
        {
            return createRelation;
        }
    }
    else
    {
        if (noCreateRelation == nullptr)
        {
            noCreateRelation = make_shared<BodyInstantiatorRelation>(bodyMapper, false);
        }

        if (inverted)
        {
            if (noCreateInverseRelation == nullptr)
            {
                noCreateInverseRelation = make_shared<InvertLiteralGraphRelation>(noCreateRelation);
            }
            return noCreateInverseRelation;
        }
        else
        {
            return noCreateRelation;
        }
    }
}

void RuleDatabase::AbstractBodyInfo::lockVariableCreation()
{
    if (bodyMapper != nullptr)
    {
        bodyMapper->lockVariableCreation();
    }
}

bool RuleDatabase::AbstractBodyInfo::getHeadArgumentsForVertex(int vertex, vector<int>& outArgs) const
{
    outArgs.clear();
    outArgs.reserve(headRelationInfo->argumentRelations.size());
    for (auto& argRel : headRelationInfo->argumentRelations)
    {
        int argVal;
        if (!argRel->getRelation(vertex, argVal))
        {
            return false;
        }
        outArgs.push_back(argVal);
    }
    return true;
}

FactGraphFilterPtr RuleDatabase::AbstractBodyInfo::getFilter() const
{
    if (filter == nullptr)
    {
        filter = make_shared<FactGraphFilter>(this);
    }
    return filter;
}

bool RuleDatabase::AbstractBodyInfo::isFullyKnown() const
{
    if (BodyInfo::isFullyKnown())
    {
        return true;
    }

    return numUnknownConcretes == 0;
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

void RuleDatabase::NogoodBuilder::emit(RuleDatabase& rdb, const IGraphRelationPtr<bool>& filter)
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
        vxy_assert(filter == nullptr);
        
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
            solver.makeGraphConstraintWithFilter<ClauseConstraint>(topology, filter, abstractLits);    
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
    : m_rdb(&rdb)
    , m_headRelationInfo(headRelationInfo)
    , m_bodyInfo(bodyInfo)
{
}

void AbstractBodyMapper::lockVariableCreation()
{
    // We have created all necessary body variables at this point, and the RDB is being destroyed.
    // Clear our pointer so that we don't attempt to create any more variables.
    m_rdb = nullptr;
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

    if (!allowCreation || m_rdb == nullptr)
    {
        return false;
    }
    
    vxy_assert(m_headRelationInfo != nullptr || m_bodyInfo->isNegativeConstraint);

    if (!m_rdb->getLiteralForBody(*m_bodyInfo, *args, outLit))
    {
        return false;
    }
    vxy_assert(outLit.isValid());

    m_bindMap.insert(argHash, nullptr, {*args, outLit});
    return true;
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

bool BodyInstantiatorRelation::equals(const IGraphRelation<Literal>& rhs) const
{
    if (this == &rhs) { return true; }
    if (auto rrhs = dynamic_cast<const BodyInstantiatorRelation*>(&rhs))
    {
        return m_mapper == rrhs->m_mapper && m_allowCreation == rrhs->m_allowCreation;
    }
    return false;
}

size_t BodyInstantiatorRelation::hash() const
{
    return 0;
}

FactGraphFilter::FactGraphFilter(const RuleDatabase::AbstractAtomInfo* atomInfo, const ValueSet& mask, const AbstractAtomRelationInfoPtr& relationInfo)
{
    m_topology = atomInfo->getTopology();
    m_filter.pad(atomInfo->getTopology()->getNumVertices(), true);

    vector<int> args;
    for (int vertex = 0; vertex < m_topology->getNumVertices(); ++vertex)
    {
        if (!RuleDatabase::getConcreteArgumentsForRelation(relationInfo, vertex, args))
        {
            continue;
        }

        // Exclude any vertices where the concrete atom for this vertex is known to be within the mask
        // (or known to be outside the mask).
        auto it = atomInfo->concreteAtoms.find(args);
        if (it != atomInfo->concreteAtoms.end())
        {
            if (!it->second->containsUnknowns(mask))
            {
                m_filter[vertex] = false;
            }
        }
    }
}

FactGraphFilter::FactGraphFilter(const RuleDatabase::AbstractBodyInfo* bodyInfo)
{
    m_topology = bodyInfo->getTopology();
    
    // only include vertices where the body truth has not been proven already.
    m_filter.pad(bodyInfo->getTopology()->getNumVertices(), false);
    for (auto& concreteBodyEntry : bodyInfo->concreteBodies)
    {
        if (concreteBodyEntry.second->status == RuleDatabase::ETruthStatus::Undetermined)
        {
            m_filter[concreteBodyEntry.second->parentVertex] = true;
        }
    }
}

bool FactGraphFilter::getRelation(VertexID sourceVertex, bool& out) const
{
    out = m_filter[sourceVertex];
    return true;
}

size_t FactGraphFilter::hash() const
{
    return 0;
}

bool FactGraphFilter::equals(const IGraphRelation<bool>& rhs) const
{
    if (this == &rhs) { return true; }
    if (auto rrhs = dynamic_cast<const FactGraphFilter*>(&rhs))
    {
        return rrhs->m_filter == m_filter;
    }
    return false;
}

FactGraphFilterPtr FactGraphFilter::combine(const FactGraphFilterPtr& a, const FactGraphFilterPtr& b)
{    
    if (a == nullptr)
    {
        return b;
    }
    else if (b == nullptr)
    {
        return a;
    }

    vxy_assert(a->m_topology == b->m_topology);

    auto newFilter = shared_ptr<FactGraphFilter>(new FactGraphFilter());
    newFilter->m_topology = a->m_topology;
    newFilter->m_filter = a->m_filter;
    newFilter->m_filter.intersect(b->m_filter);
    return newFilter;
}
