// Copyright Proletariat, Inc. All Rights Reserved.
#include "rules/RuleDatabase.h"

#include "ConstraintSolver.h"
#include "topology/GraphRelations.h"

using namespace Vertexy;

static const SolverVariableDomain booleanVariableDomain(0, 1);
static const ValueSet FALSE_VALUE = booleanVariableDomain.getBitsetForValue(0);
static const ValueSet TRUE_VALUE = booleanVariableDomain.getBitsetForValue(1);

RuleDatabase::RuleDatabase(ConstraintSolver& solver)
    : m_solver(solver)
{
    m_atoms.push_back(make_unique<AtomInfo>());
}

void RuleDatabase::finalize()
{
    computeSCCs();

    //
    // First go through each body, creating a boolean variable representing whether the body is satisfied,
    // and constraint that variable so it is true IFF all literals are true, and false IFF any literal is false.
    // Additionally, for each head attached to this body, constrain the head to be true if the body variable is true.
    //
    int i = 0;
    for (auto it = m_bodies.begin(), itEnd = m_bodies.end(); it != itEnd; ++it, ++i)
    {
        auto bodyInfo = it->get();
        vxy_assert(!bodyInfo->body.isSum);
        vxy_assert(!bodyInfo->lit.variable.isValid());

        // Create a new boolean variable representing the body and constrain it.
        VarID boolVar = m_solver.makeVariable({wstring::CtorSprintf(), TEXT("body-%d"), i}, booleanVariableDomain);
        bodyInfo->lit = Literal(boolVar, TRUE_VALUE);

        m_nogoodBuilder.reserve(bodyInfo->body.values.size()+1);
        for (auto itv = bodyInfo->body.values.begin(), itvEnd = bodyInfo->body.values.end(); itv != itvEnd; ++itv)
        {
            auto atomLit = translateAtomLiteral(*itv);
            m_nogoodBuilder.add(atomLit);

            // nogood(B, -Bv)
            vxy_sanity(bodyInfo->lit.variable != atomLit.variable);
            vector clauses { bodyInfo->lit.inverted(), atomLit };
            m_solver.makeConstraint<ClauseConstraint>(clauses);
        }

        // nogood(-B, Bv1, Bv2, Bv3, ...)
        m_nogoodBuilder.add(bodyInfo->lit.inverted());
        m_nogoodBuilder.emit(m_solver);

        for (auto ith = bodyInfo->heads.begin(), ithEnd = bodyInfo->heads.end(); ith != ithEnd; ++ith)
        {
            // nogood(-H, B)
            auto headLit = getLiteralForAtom(*ith);
            vxy_sanity(headLit.variable != bodyInfo->lit.variable);
            vector clauses {headLit, bodyInfo->lit.inverted() };
            m_solver.makeConstraint<ClauseConstraint>(clauses);
        }

        if (bodyInfo->isConstraint)
        {
            // body can't be true
            vector invertedBody { bodyInfo->lit.inverted() };
            m_solver.makeConstraint<ClauseConstraint>(invertedBody);
        }
    }

    //
    // Go through each head, and constrain it to be false if ALL supporting bodies are false.
    //
    for (auto it = m_atoms.begin()+1, itEnd = m_atoms.end(); it != itEnd; ++it)
    {
        auto atomInfo = it->get();
        // nogood(H, -B1, -B2, ...)
        m_nogoodBuilder.reserve(atomInfo->supports.size()+1);
        m_nogoodBuilder.add(getLiteralForAtom(atomInfo));
        for (auto itb = atomInfo->supports.begin(), itbEnd = atomInfo->supports.end(); itb != itbEnd; ++itb)
        {
            auto bodyInfo = *itb;
            vxy_sanity(bodyInfo->lit.variable.isValid());
            m_nogoodBuilder.add(bodyInfo->lit.inverted());
        }
        m_nogoodBuilder.emit(m_solver);
    }
}

void RuleDatabase::NogoodBuilder::add(const Literal& lit)
{
    auto found = find_if(m_literals.begin(), m_literals.end(), [&](auto& t)
    {
        return t.variable == lit.variable;
    });

    if (found != m_literals.end())
    {
        found->values.exclude(lit.values);
    }
    else
    {
        m_literals.push_back(lit.inverted());
    }
}

void RuleDatabase::NogoodBuilder::emit(ConstraintSolver& solver)
{
    solver.makeConstraint<ClauseConstraint>(m_literals);
    m_literals.clear();
}

Literal RuleDatabase::getLiteralForAtom(AtomInfo* atomInfo)
{
    if (atomInfo->equivalence.variable.isValid())
    {
        // TODO: potentially make intermediate variables where the source literal has a large ValueSet and/or is referenced by many bodies
        return atomInfo->equivalence;
    }

    if (!atomInfo->variable.isValid())
    {
        atomInfo->variable = m_solver.makeVariable({wstring::CtorSprintf(), TEXT("atom-%d"), atomInfo->id.value}, booleanVariableDomain);
    }
    atomInfo->equivalence = Literal(atomInfo->variable, TRUE_VALUE);
    return atomInfo->equivalence;
}

Literal RuleDatabase::translateAtomLiteral(AtomLiteral lit)
{
    auto atomInfo = getAtom(lit.id());
    auto translatedLit = getLiteralForAtom(atomInfo);
    return lit.sign() ? translatedLit : translatedLit.inverted();
}

void RuleDatabase::addRule(const TRuleDefinition<Literal>& rule)
{
    TRuleHead<AtomID> newHead(rule.getHead().type);
    for (auto& lit : rule.getHead().heads)
    {
        newHead.heads.push_back(createHeadAtom(lit));
    }

    return addRule(TRuleDefinition<AtomID>(move(newHead), rule.getBody()));
}

void RuleDatabase::addRule(const TRuleDefinition<SignedClause>& rule)
{
    TRuleHead<AtomID> newHead(rule.getHead().type);
    for (auto& clause : rule.getHead().heads)
    {
        ValueSet values = clause.translateToDomain(m_solver.getDomain(clause.variable));
        newHead.heads.push_back(createHeadAtom(Literal(clause.variable, values)));
    }

    return addRule(TRuleDefinition<AtomID>(move(newHead), rule.getBody()));
}

void RuleDatabase::addRule(const TRuleDefinition<AtomID>& rule)
{
    NormalizedRule normalized(rule.getHead(), normalizeBody(rule.getBody()));

    int sumCount = 0;
    int litCount = 0;
    int firstLitIndex = -1;
    for (int i = 0; i < normalized.getNumBodyElements(); ++i)
    {
        if (normalized.getBodyElement(i).isSum)
        {
            ++sumCount;
        }
        else
        {
            if (litCount == 0)
            {
                firstLitIndex = i;
            }
            ++litCount;
        }
    }

    if (litCount > 1)
    {
        // Flatten all literals into one element.
        // given S, T are a Sum constraints,
        // from:
        //   A <- B, C, S, T
        // to:
        //  A <- (B+C), S, T
        vxy_sanity(firstLitIndex >= 0);
        for (int j = normalized.getNumBodyElements()-1; j > firstLitIndex; --j)
        {
            auto& el = normalized.getBodyElement(j);
            if (!el.isSum)
            {
                auto& dest = normalized.getBodyElement(firstLitIndex).values;
                dest.insert(dest.end(), el.values.begin(), el.values.end());

                normalized.getBody().erase_unsorted(normalized.getBody().begin() + j);
                --litCount;
            }
        }
        vxy_sanity(litCount == 1);
    }

    if (litCount + sumCount > 1)
    {
        // Split into multiple rules. Precondition: only one non-sum constraint.
        // from:
        //     A <- B1, B2, B3
        // to:
        //     X <- B1
        //     Y <- B2
        //     Z <- B3
        //     A <- X, Y, Z
        vector<AtomLiteral> subRuleHeads;
        subRuleHeads.reserve(normalized.getNumBodyElements());
        for (int i = 0; i < normalized.getNumBodyElements(); ++i)
        {
            auto subHead = createAtom();
            transformRule(TRuleHead(subHead), normalized.getBodyElement(i));
            subRuleHeads.push_back(subHead.pos());
        }

        transformRule(TRuleHead(normalized.getHead()), TRuleBodyElement<AtomLiteral>::create(subRuleHeads));
    }
    else
    {
        vxy_sanity(normalized.getNumBodyElements() <= 1);
        transformRule(TRuleHead(normalized.getHead()), normalized.getBodyElement(0));
    }
}

void RuleDatabase::transformRule(const RuleHead& head, const RuleBody& body)
{
    if (body.isSum)
    {
        AtomID finalHead = head.heads.empty() ? AtomID() : head.heads[0];
        if (head.type == ERuleHeadType::Choice || head.heads.size() > 1)
        {
            // for H1 \/ H2 <- X,
            // define:
            //   H1 \/ H2 <- a1
            //   a1 <- X
            // *X is a sum body
            vxy_assert(head.type != ERuleHeadType::Normal);
            finalHead = createAtom();
            transformRule(head, RuleBody::create(finalHead.pos()));
        }
        transformSum(finalHead, body);
    }
    else if (head.type == ERuleHeadType::Choice || (head.type == ERuleHeadType::Disjunction && head.heads.size() > 1))
    {
        if (body.values.size() > 1 && head.heads.size() > 1)
        {
            // For H1 \/ H2 <- B (or {H1, H2} <- B),
            // define:
            // a1 <- B
            // H1 \/ H2 <- a1 (or {H1, H2} <- a1)
            auto auxHead = createAtom();
            simplifyAndEmitRule(auxHead, body);
            if (head.type == ERuleHeadType::Choice)
            {
                transformChoice(head, RuleBody::create(auxHead.pos()));
            }
            else
            {
                transformDisjunction(head, RuleBody::create(auxHead.pos()));
            }
        }
        else
        {
            if (head.type == ERuleHeadType::Choice)
            {
                transformChoice(head, body);
            }
            else
            {
                transformDisjunction(head, body);
            }
        }
    }
    else
    {
        vxy_sanity(head.heads.size() == 1);
        simplifyAndEmitRule(head.heads[0], body);
    }
}

void RuleDatabase::transformChoice(const RuleHead& head, const RuleBody& body)
{
    vxy_sanity(head.type == ERuleHeadType::Choice);
    vxy_sanity(!body.isSum);

    // head choice "H1 .. \/ Hn" becomes
    // H1 <- <body> /\ not Choice1
    // Choice1 <- not H1
    // ...
    // Hn <- <body> /\ not ChoiceN
    // ChoiceN <- not Hn

    for (int i = 0; i < head.heads.size(); ++i)
    {
        AtomID choiceAtom = createAtom();
        TRuleBodyElement<AtomLiteral> extBody = body;
        extBody.values.push_back(choiceAtom.neg());
        simplifyAndEmitRule(head.heads[i], extBody);
        simplifyAndEmitRule(choiceAtom, RuleBody::create(head.heads[i].neg()));
    }
}

void RuleDatabase::transformDisjunction(const RuleHead& head, const RuleBody& body)
{
    vxy_sanity(head.type != ERuleHeadType::Choice);
    vxy_sanity(!body.isSum);

    if (head.heads.size() <= 1)
    {
        simplifyAndEmitRule(head.heads.empty() ? AtomID() : head.heads[0], body);
        return;
    }

    // For each head:
    // Hi <- <body> /\ {not Hn | n != i}
    for (int i = 0; i < head.heads.size(); ++i)
    {
        RuleBody extBody = body;
        for (int j = 0; j < head.heads.size(); ++j)
        {
            if (i == j) continue;
            extBody.values.push_back(head.heads[j].neg());
        }
        simplifyAndEmitRule(head.heads[i], extBody);
    }
}

void RuleDatabase::transformSum(AtomID head, const RuleBody& sumBody)
{
    vxy_fail_msg("NYI");
}

bool RuleDatabase::isLiteralPossible(AtomLiteral literal) const
{
    const AtomInfo* atomInfo = getAtom(literal.id());
    if (atomInfo->equivalence.variable.isValid())
    {
        SolverVariableDatabase* db = m_solver.getVariableDB();
        const ValueSet& values = m_solver.getVariableDB()->getPotentialValues(atomInfo->equivalence.variable);
        if ((literal.sign() && !values.anyPossible(atomInfo->equivalence.values)) ||
            (!literal.sign() && !values.anyPossible(atomInfo->equivalence.values.inverted())))
        {
            return false;
        }
    }

    // No variable created yet, so we can assume possible.
    return true;
}

bool RuleDatabase::simplifyAndEmitRule(AtomID head, const RuleBody& body)
{
    vxy_assert(!body.isSum);

    // silently discard rule if head is initially false
    if (head.isValid() && !isLiteralPossible(head.pos()))
    {
        return false;
    }

    // remove duplicates
    // silently discard rule if it is self-contradicting (p and -p)
    RuleBody newBody = body;
    for (auto it = newBody.values.begin(), itEnd = newBody.values.end(); it != itEnd; ++it)
    {
        AtomLiteral cur = *it;
        if (!isLiteralPossible(cur))
        {
            return false;
        }

        auto next = it+1;
        while (next != itEnd)
        {
            next = find(next, itEnd, cur);
            if (next != itEnd)
            {
                next = newBody.values.erase_unsorted(next);
            }
        }

        AtomLiteral inversed = cur.inverted();
        if (find(it+1, itEnd, inversed) != itEnd)
        {
            return false;
        }
    }

    // create the BodyInfo (or return the existing one if this is a duplicate)
    auto newBodyInfo = findOrCreateBodyInfo(newBody);

    // Link the body to the head relying on it, and the head to the body supporting it.
    if (head.isValid())
    {
        auto headInfo = getAtom(head);
        headInfo->supports.push_back(newBodyInfo);
        newBodyInfo->heads.push_back(headInfo);
    }
    else
    {
        newBodyInfo->isConstraint = true;
    }

    // Link each positive atom in the body to the body depending on it.
    for (auto it = newBody.values.begin(), itEnd = newBody.values.end(); it != itEnd; ++it)
    {
        if (it->sign())
        {
            auto atomInfo = getAtom(it->id());
            atomInfo->positiveDependencies.push_back(newBodyInfo);
        }
    }

    return true;
}

RuleDatabase::BodyInfo* RuleDatabase::findOrCreateBodyInfo(const RuleBody& body)
{
    int32_t hash = BodyHasher::hashBody(body);
    auto found = m_bodySet.find_range_by_hash(hash);
    for (auto it = found.first; it != found.second; ++it)
    {
        if (BodyHasher::compareBodies((*it)->body, body))
        {
            return *it;
        }
    }

    auto newBodyInfo = make_unique<BodyInfo>(m_bodies.size(), body);

    m_bodySet.insert(hash, nullptr, newBodyInfo.get());
    m_bodies.push_back(move(newBodyInfo));
    return m_bodies.back().get();
}

int32_t RuleDatabase::BodyHasher::hashBody(const RuleBody& body)
{
    // !!TODO!! a real hash function for AtomIDs
    hash<int32_t> intHasher;

    // NOTE: we do not want to hash value here, because it can change (via createHeadAtom)
    int32_t hash = 0;
    for (auto it = body.values.begin(); it != body.values.end(); ++it)
    {
        hash += intHasher(it->id().value);
    }
    return hash;
}

bool RuleDatabase::BodyHasher::compareBodies(const RuleBody& lbody, const RuleBody& rbody)
{
    if (lbody.values.size() != rbody.values.size())
    {
        return false;
    }

    if (lbody.isSum != rbody.isSum)
    {
        return false;
    }

    if (lbody.isSum)
    {
        if (lbody.lowerBound != rbody.lowerBound)
        {
            return false;
        }
        if (lbody.weights.size() != rbody.weights.size())
        {
            return false;
        }
    }

    for (int i = 0; i < lbody.values.size(); ++i)
    {
        auto idx = indexOf(rbody.values.begin(), rbody.values.end(), lbody.values[i]);
        if (idx < 0)
        {
            return false;
        }
        if (lbody.isSum && lbody.weights[i] != rbody.weights[idx])
        {
            return false;
        }
    }

    return true;
}

AtomID RuleDatabase::createHeadAtom(const Literal& equivalence)
{
    auto found = m_atomMap.find(equivalence);
    if (found != m_atomMap.end())
    {
        return found->second;
    }

    Literal inverted = equivalence.inverted();
    found = m_atomMap.find(inverted);
    if (found != m_atomMap.end())
    {
        AtomID foundID = found->second;
        AtomInfo* atomInfo = m_atoms[foundID.value].get();
        vxy_assert_msg(atomInfo->supports.size() == 0, "rule heads assigned with opposing values?");

        atomInfo->equivalence = equivalence;
        // replace all occurrences of this literal with the negation.
        for (auto& bodyPtr : m_bodies)
        {
            RuleBody& body = bodyPtr->body;
            for (auto it = body.values.begin(), itEnd = body.values.end(); it != itEnd; ++it)
            {
                if (it->id() == foundID)
                {
                    *it = it->inverted();
                    if (it->sign())
                    {
                        getAtom(it->id())->positiveDependencies.push_back(bodyPtr.get());
                    }
                    else
                    {
                        getAtom(it->id())->positiveDependencies.erase_first_unsorted(bodyPtr.get());
                    }
                }
            }
        }

        return foundID;
    }

    wstring name(wstring::CtorSprintf(), TEXT("atom-%d(%s=%s)"), m_atoms.size(), m_solver.getVariableName(equivalence.variable).c_str(), equivalence.values.toString().c_str());
    AtomID newAtom = createAtom(name.c_str());

    m_atomMap[equivalence] = newAtom;
    m_atoms[newAtom.value]->equivalence = equivalence;

    return newAtom;
}

AtomLiteral RuleDatabase::createAtom(const Literal& lit, const wchar_t* name)
{
    auto found = m_atomMap.find(lit);
    if (found != m_atomMap.end())
    {
        return AtomLiteral(found->second, true);
    }

    Literal inverted = lit.inverted();
    found = m_atomMap.find(inverted);
    if (found != m_atomMap.end())
    {
        return AtomLiteral(found->second, false);
    }

    wstring sname;
    if (name == nullptr)
    {
        sname = {wstring::CtorSprintf(), TEXT("atom-%d(%s=%s)"), m_atoms.size(), m_solver.getVariableName(lit.variable).c_str(), lit.values.toString().c_str()};
        name = sname.c_str();
    }

    AtomID newAtom = createAtom(name);

    m_atomMap[lit] = newAtom;
    m_atoms[newAtom.value]->equivalence = lit;

    return AtomLiteral(newAtom, true);
}

AtomID RuleDatabase::createAtom(const wchar_t* name)
{
    AtomID newAtom(m_atoms.size());

    m_atoms.push_back(make_unique<AtomInfo>(newAtom));
    if (name == nullptr)
    {
        m_atoms.back()->name.sprintf(TEXT("atom-%d"), newAtom.value);
    }
    else
    {
        m_atoms.back()->name = name;
    }

    return newAtom;
}


vector<TRuleBodyElement<AtomLiteral>> RuleDatabase::normalizeBody(const vector<AnyBodyElement>& elements)
{
    vector<TRuleBodyElement<AtomLiteral>> out;
    for (auto it = elements.begin(), itEnd = elements.end(); it != itEnd; ++it)
    {
        out.push_back(normalizeBodyElement(*it));
    }
    return out;
}

TRuleBodyElement<AtomLiteral> RuleDatabase::normalizeBodyElement(const AnyBodyElement& element)
{
    return visit([&](auto&& typedElement)
    {
        using ElementType = decay_t<decltype(typedElement)>;
        if constexpr (is_same_v<ElementType, TRuleBodyElement<AtomLiteral>>)
        {
            return typedElement;
        }
        else if constexpr (is_same_v<ElementType, TRuleBodyElement<Literal>>)
        {
            TRuleBodyElement<AtomLiteral> out;

            out.values.reserve(typedElement.values.size());
            out.weights.reserve(typedElement.weights.size());
            for (int i = 0; i < typedElement.values.size(); ++i)
            {
                out.values.push_back(createAtom(typedElement.values[i]));
                if (i < typedElement.weights.size())
                {
                    out.weights.push_back(typedElement.weights[i]);
                }
            }
            out.isSum = typedElement.isSum;
            out.lowerBound = typedElement.lowerBound;
            return move(out);
        }
        else
        {
            static_assert(is_same_v<ElementType, TRuleBodyElement<SignedClause>>);
            TRuleBodyElement<AtomLiteral> out;

            out.values.reserve(typedElement.values.size());
            out.weights.reserve(typedElement.weights.size());
            for (int i = 0; i < typedElement.values.size(); ++i)
            {
                ValueSet litValues = typedElement.values[i].translateToDomain(m_solver.getDomain(typedElement.values[i].variable));

                out.values.push_back(createAtom(Literal(typedElement.values[i].variable, litValues)));
                if (i < typedElement.weights.size())
                {
                    out.weights.push_back(typedElement.weights[i]);
                }
            }
            out.isSum = typedElement.isSum;
            out.lowerBound = typedElement.lowerBound;
            return move(out);
        }
    }, element);
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
        }

        ++nextSCC;
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
        for (auto it = atomInfo->positiveDependencies.begin(), itEnd = atomInfo->positiveDependencies.end(); it != itEnd; ++it)
        {
            auto refBodyInfo = *it;
            const auto& depBodyLits = refBodyInfo->body.values;
            vxy_sanity(find(depBodyLits.begin(), depBodyLits.end(), AtomLiteral(atom, true)) != depBodyLits.end());

            visitor(m_atoms.size()-1 + refBodyInfo->id);
        }
    }
    else
    {
        auto refBodyInfo = m_bodies[node - (m_atoms.size()-1)].get();
        // visit each head that this body is supporting.
        for (auto ith = refBodyInfo->heads.begin(), ithEnd = refBodyInfo->heads.end(); ith != ithEnd; ++ith)
        {
            visitor((*ith)->id.value-1);
        }
    }
}

GraphAtomLiteral RuleDatabase::createGraphAtom(const shared_ptr<ITopology>& topology, const RuleGraphRelation& equivalence, const wchar_t* name)
{
    auto relation = normalizeGraphRelation(equivalence);

    auto it = m_graphAtomMaps.find(topology);
    if (it == m_graphAtomMaps.end())
    {
        auto newRelationMap = make_unique<GraphRelationList>();
        it = m_graphAtomMaps.insert(make_pair(topology, move(newRelationMap))).first;
    }

    GraphRelationList& relationList = *it->second.get();
    if (GraphAtomLiteral existing = findExistingAtomForRelation(topology, relation, relationList); existing.isValid())
    {
        return existing;
    }

    wstring sname;
    if (name == nullptr)
    {
        sname = {wstring::CtorSprintf(), TEXT("graphAtom-%d(%s)"), m_nextGraphAtomID, relation->toString().c_str()};
        name = sname.c_str();
    }

    auto newAtom = createGraphAtom(topology, name);
    relationList.push_back(make_tuple(relation, newAtom));

    return GraphAtomLiteral(newAtom, true);
}

GraphAtomID RuleDatabase::createGraphAtom(const shared_ptr<ITopology>& topology, const wchar_t* name)
{
    auto it = m_graphAtoms.find(topology);
    if (it == m_graphAtoms.end())
    {
        it = m_graphAtoms.insert(make_pair(topology, make_unique<GraphAtomSet>())).first;
    }

    GraphAtomSet& atomSet = *it->second.get();
    GraphAtomID newAtom(m_nextGraphAtomID++);

    wstring sname;
    if (name == nullptr)
    {
        sname = {wstring::CtorSprintf(), TEXT("graphAtom-%d"), newAtom.value};
        name = sname.c_str();
    }

    atomSet[newAtom.value] = make_unique<GraphAtomInfo>(name, nullptr);
    return newAtom;
}

GraphAtomLiteral RuleDatabase::findExistingAtomForRelation(const shared_ptr<ITopology>& topology, const GraphLiteralRelationPtr& relation, const GraphRelationList& list) const
{
    GraphLiteralRelationPtr invRelation;
    if (auto inversion = dynamic_cast<const InvertLiteralGraphRelation*>(relation.get()))
    {
        invRelation = inversion->getInner();
    }

    for (auto it = list.begin(), itEnd = list.end(); it != itEnd; ++it)
    {
        auto itRel = get<GraphLiteralRelationPtr>(*it);
        if (itRel->equals(topology, *relation.get()))
        {
            return GraphAtomLiteral(get<GraphAtomID>(*it), true);
        }
        else if (invRelation != nullptr && itRel->equals(topology, *invRelation.get()))
        {
            return GraphAtomLiteral(get<GraphAtomID>(*it), false);
        }
    }

    return GraphAtomLiteral();
}

GraphLiteralRelationPtr RuleDatabase::normalizeGraphRelation(const RuleGraphRelation& relation) const
{
    return visit([&](auto&& typedRel) -> GraphLiteralRelationPtr
    {
        using ElementType = decay_t<decltype(typedRel)>;
        if constexpr (is_same_v<ElementType, GraphLiteralRelationPtr>)
        {
            return typedRel;
        }
        else
        {
            return make_shared<ClauseToLiteralGraphRelation>(m_solver, typedRel);
        }
    }, relation);
}
