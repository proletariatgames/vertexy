// Copyright Proletariat, Inc. All Rights Reserved.
#include "rules/RuleDatabase.h"

#include "ConstraintSolver.h"

using namespace Vertexy;

static const SolverVariableDomain booleanVariableDomain(0, 1);
static const ValueSet FALSE_VALUE = booleanVariableDomain.getBitsetForValue(0);
static const ValueSet TRUE_VALUE = booleanVariableDomain.getBitsetForValue(1);

#define VERTEXY_RULE_NAME_ATOMS 1

RuleDatabase::RuleDatabase(ConstraintSolver& solver)
    : m_solver(solver)
{
    m_atoms.push_back(make_unique<AtomInfo>());
}

bool RuleDatabase::finalize()
{
    if (!propagateFacts())
    {
        return false;
    }

    auto db = m_solver.getVariableDB();

    //
    // First go through each body, creating a boolean variable representing whether the body is satisfied,
    // and constraint that variable so it is true IFF all literals are true, and false IFF any literal is false.
    // Additionally, for each head attached to this body, constrain the head to be true if the body variable is true.
    //
    for (auto it = m_bodies.begin(), itEnd = m_bodies.end(); it != itEnd; ++it)
    {
        auto bodyInfo = it->get();
        if (bodyInfo->status != ETruthStatus::Undetermined)
        {
            continue;
        }

        vxy_assert(!bodyInfo->body.isSum);
        vxy_assert(!bodyInfo->lit.variable.isValid());

        // Create a new boolean variable representing the body and constrain it.
        wstring bodyName;
        #if VERTEXY_RULE_NAME_ATOMS
            bodyName.append_sprintf(TEXT("body-%d["), bodyInfo->id);
        #endif

        m_nogoodBuilder.reserve(bodyInfo->body.values.size()+1);
        bool first = true;
        for (auto itv = bodyInfo->body.values.begin(), itvEnd = bodyInfo->body.values.end(); itv != itvEnd; ++itv)
        {
            if (isLiteralAssumed(*itv))
            {
                // literal is always true, no need to include.
                continue;
            }

            auto atomLit = instantiateAtomLiteral(*itv);
            vxy_sanity(bodyInfo->lit.variable != atomLit.variable);
            m_nogoodBuilder.add(atomLit);

            #if VERTEXY_RULE_NAME_ATOMS
                if (!first) bodyName.append(TEXT(","));
                bodyName.append_sprintf(TEXT("(%s=%s)"), m_solver.getVariableName(atomLit.variable).c_str(), atomLit.values.toString().c_str());
            #endif
            first = false;
        }

        vxy_assert(!m_nogoodBuilder.empty());

        #if VERTEXY_RULE_NAME_ATOMS
            bodyName += TEXT("]");
        #endif

        // create the solver variable for the body
        VarID boolVar = m_solver.makeVariable(bodyName, booleanVariableDomain);
        bodyInfo->lit = Literal(boolVar, TRUE_VALUE);
        Literal invertedBodyLit = bodyInfo->lit.inverted();

        if (!db->getPotentialValues(invertedBodyLit.variable).isSubsetOf(invertedBodyLit.values))
        {
            for (int i = 0; i < m_nogoodBuilder.literals.size(); ++i)
            {
                auto& lit = m_nogoodBuilder.literals[i];
                if (db->getPotentialValues(lit.variable).isSubsetOf(lit.values))
                {
                    continue;
                }

                // nogood(B, -Bv)
                vector clauses { bodyInfo->lit.inverted(), m_nogoodBuilder.literals[i] };
                m_solver.makeConstraint<ClauseConstraint>(clauses);
            }
        }

        // nogood(-B, Bv1, Bv2, Bv3, ...)
        if (db->getPotentialValues(bodyInfo->lit.variable).isSubsetOf(bodyInfo->lit.values))
        {
            m_nogoodBuilder.clear();
        }
        else
        {
            m_nogoodBuilder.add(bodyInfo->lit.inverted());
            m_nogoodBuilder.emit(m_solver);
        }

        for (auto ith = bodyInfo->heads.begin(), ithEnd = bodyInfo->heads.end(); ith != ithEnd; ++ith)
        {
            if (isLiteralAssumed((*ith)->id.pos()))
            {
                continue;
            }

            // nogood(-H, B)
            auto& headLit = getLiteralForAtom(*ith);
            vxy_sanity(headLit.variable != bodyInfo->lit.variable);

            vector clauses {headLit, bodyInfo->lit.inverted() };
            m_solver.makeConstraint<ClauseConstraint>(clauses);
        }

        if (bodyInfo->isNegativeConstraint)
        {
            // body can't be true
            if (!m_solver.getVariableDB()->excludeValues(bodyInfo->lit, nullptr))
            {
                m_conflict = true;
                return false;
            }
        }
    }

    //
    // Go through each head, and constrain it to be false if ALL supporting bodies are false.
    //
    for (auto it = m_atoms.begin()+1, itEnd = m_atoms.end(); it != itEnd; ++it)
    {
        auto atomInfo = it->get();
        if (atomInfo->status != ETruthStatus::Undetermined)
        {
            continue;
        }

        vxy_assert(atomInfo->equivalence.variable.isValid());

        if (isLiteralAssumed(atomInfo->id.neg()))
        {
            continue;
        }

        // nogood(H, -B1, -B2, ...)
        m_nogoodBuilder.reserve(atomInfo->supports.size()+1);
        m_nogoodBuilder.add(getLiteralForAtom(atomInfo));
        for (auto itb = atomInfo->supports.begin(), itbEnd = atomInfo->supports.end(); itb != itbEnd; ++itb)
        {
            auto bodyInfo = *itb;
            // we should've been marked trivially true if one of our supports was,
            // or it should've been removed as a support if it is trivially false.
            vxy_assert(bodyInfo->status == ETruthStatus::Undetermined);
            vxy_sanity(bodyInfo->lit.variable.isValid());

            if (db->getPotentialValues(bodyInfo->lit.variable).isSubsetOf(bodyInfo->lit.values))
            {
                // body can never be false, so no need to include it.
                continue;
            }

            // if the body is false, it cannot support us
            m_nogoodBuilder.add(bodyInfo->lit.inverted());
        }
        m_nogoodBuilder.emit(m_solver);
    }

    if (!m_conflict)
    {
        computeSCCs();
    }

    return !m_conflict;
}

void RuleDatabase::NogoodBuilder::add(const Literal& lit)
{
    auto found = find_if(literals.begin(), literals.end(), [&](auto& t)
    {
        return t.variable == lit.variable;
    });

    if (found != literals.end())
    {
        found->values.include(lit.values);
    }
    else
    {
        literals.push_back(lit);
    }
}

void RuleDatabase::NogoodBuilder::emit(ConstraintSolver& solver)
{
    for (auto& lit : literals)
    {
        lit = lit.inverted();
    }

    solver.makeConstraint<ClauseConstraint>(literals);
    literals.clear();
}

const Literal& RuleDatabase::getLiteralForAtom(AtomInfo* atomInfo)
{
    if (atomInfo->equivalence.variable.isValid())
    {
        // TODO: potentially make intermediate variables where the source literal has a large ValueSet and/or is referenced by many bodies
        return atomInfo->equivalence;
    }

    VarID var = m_solver.makeVariable(atomInfo->name, booleanVariableDomain);
    atomInfo->equivalence = Literal(var, TRUE_VALUE);
    return atomInfo->equivalence;
}

Literal RuleDatabase::instantiateAtomLiteral(AtomLiteral lit)
{
    auto atomInfo = getAtom(lit.id());
    auto& translatedLit = getLiteralForAtom(atomInfo);
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
        if (normalized.getNumBodyElements() > 0)
        {
            transformRule(TRuleHead(normalized.getHead()), normalized.getBodyElement(0));
        }
        else
        {
            static RuleBody emptyBody = RuleBody::create(vector<AtomLiteral>{});
            transformRule(normalized.getHead(), emptyBody);
        }
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
        vxy_sanity(head.heads.size() <= 1);
        simplifyAndEmitRule(head.heads.empty() ? AtomID() : head.heads[0], body);
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
        wstring choiceAtomName;
        #if VERTEXY_RULE_NAME_ATOMS
        choiceAtomName.append_sprintf(TEXT("off-%s"), getAtom(head.heads[i])->name.c_str());
        #endif
        AtomID choiceAtom = createAtom(choiceAtomName.c_str());
        TRuleBodyElement<AtomLiteral> extBody = body;
        extBody.values.push_back(choiceAtom.neg());
        simplifyAndEmitRule(head.heads[i], extBody);
        simplifyAndEmitRule(choiceAtom, RuleBody::create(head.heads[i].neg()));
    }
}

void RuleDatabase::transformDisjunction(const RuleHead& head, const RuleBody& body)
{
    vxy_assert(head.type != ERuleHeadType::Choice);
    vxy_assert(!body.isSum);
    vxy_assert(head.heads.size() > 1);

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

bool RuleDatabase::isLiteralAssumed(AtomLiteral literal) const
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

    if (atom->equivalence.variable.isValid())
    {
        auto db = m_solver.getVariableDB();
        if (literal.sign() && db->getPotentialValues(atom->equivalence.variable).isSubsetOf(atom->equivalence.values))
        {
            return true;
        }
        else if (!literal.sign() && !db->getPotentialValues(atom->equivalence.variable).anyPossible(atom->equivalence.values))
        {
            return true;
        }
    }

    return false;
}

bool RuleDatabase::simplifyAndEmitRule(AtomID head, const RuleBody& body)
{
    vxy_assert(!body.isSum);

    // remove duplicates
    // silently discard rule if it is self-contradicting (p and -p)
    RuleBody newBody = body;
    for (auto it = newBody.values.begin(); it != newBody.values.end(); ++it)
    {
        AtomLiteral cur = *it;

        AtomLiteral inversed = cur.inverted();
        if (contains(it+1, newBody.values.end(), inversed))
        {
            // body contains an atom and its inverse == impossible to satisfy, no need to add rule.
            return false;
        }

        // remove duplicates of the same atom
        auto next = it+1;
        while (true)
        {
            next = find(next, newBody.values.end(), cur);
            if (next == newBody.values.end())
            {
                break;
            }
            next = newBody.values.erase_unsorted(next);
        }
    }

    bool isFact = false;
    if (body.values.empty())
    {
        // Empty input body means this is a fact. Set the body to the fact atom, which is always true.
        newBody.values.push_back(getFactAtom().pos());
        isFact = true;
    }

    // create the BodyInfo (or return the existing one if this is a duplicate)
    auto newBodyInfo = findOrCreateBodyInfo(newBody);

    // Link the body to the head relying on it, and the head to the body supporting it.
    if (head.isValid())
    {
        auto headInfo = getAtom(head);
        headInfo->supports.push_back(newBodyInfo);
        newBodyInfo->heads.push_back(headInfo);

        if (isFact)
        {
            setAtomStatus(headInfo, ETruthStatus::True);
        }
    }
    else
    {
        // this body has no head, so it should never hold true.
        newBodyInfo->isNegativeConstraint = true;
    }

    // Link each atom in the body to the body depending on it.
    for (auto it = newBody.values.begin(), itEnd = newBody.values.end(); it != itEnd; ++it)
    {
        auto atomInfo = getAtom(it->id());
        auto& deps = it->sign() ? atomInfo->positiveDependencies : atomInfo->negativeDependencies;
        if (!contains(deps.begin(), deps.end(), newBodyInfo))
        {
            deps.push_back(newBodyInfo);
        }
    }

    return true;
}

RuleDatabase::BodyInfo* RuleDatabase::findOrCreateBodyInfo(const RuleBody& body)
{
    vxy_assert(!body.values.empty());

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
    newBodyInfo->numUndeterminedTails = body.values.size();

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

AtomID RuleDatabase::getFactAtom()
{
    if (m_factAtom.isValid())
    {
        return m_factAtom;
    }
    m_factAtom = createAtom(TEXT("<true-fact>"));

    setAtomStatus(getAtom(m_factAtom), ETruthStatus::True);
    return m_factAtom;
}

AtomID RuleDatabase::createHeadAtom(const Literal& equivalence, const wchar_t* name)
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
        // flip the sign of the atom
        AtomID foundID = found->second;
        AtomInfo* atomInfo = m_atoms[foundID.value].get();
        vxy_assert_msg(atomInfo->supports.size() == 0, "rule heads assigned with opposing values?");

        atomInfo->equivalence = equivalence;
        if (atomInfo->status == ETruthStatus::False)
        {
            atomInfo->status = ETruthStatus::True;
        }
        else if (atomInfo->status == ETruthStatus::True)
        {
            atomInfo->status = ETruthStatus::False;
        }

        // flip the sign in any bodies this atom appears in.
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
                        getAtom(it->id())->negativeDependencies.erase_first_unsorted(bodyPtr.get());
                    }
                    else
                    {
                        getAtom(it->id())->positiveDependencies.erase_first_unsorted(bodyPtr.get());
                        getAtom(it->id())->negativeDependencies.push_back(bodyPtr.get());
                    }
                }
            }
        }

        return foundID;
    }

    wstring nameStr;
    #if VERTEXY_RULE_NAME_ATOMS
    if (name == nullptr)
    {
        nameStr.append_sprintf(TEXT("atom%d(%s=%s)"), m_atoms.size(), m_solver.getVariableName(equivalence.variable).c_str(), equivalence.values.toString().c_str());
        name = nameStr.c_str();
    }
    #endif
    AtomID newAtom = createAtom(name);

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

    #if VERTEXY_RULE_NAME_ATOMS
        wstring sname;
        if (name == nullptr)
        {
            sname = {wstring::CtorSprintf(), TEXT("atom%d(%s=%s)"), m_atoms.size(), m_solver.getVariableName(lit.variable).c_str(), lit.values.toString().c_str()};
            name = sname.c_str();
        }
    #endif

    AtomID newAtom = createAtom(name);

    m_atomMap[lit] = newAtom;
    m_atoms[newAtom.value]->equivalence = lit;

    return AtomLiteral(newAtom, true);
}

AtomID RuleDatabase::createAtom(const wchar_t* name)
{
    AtomID newAtom(m_atoms.size());

    m_atoms.push_back(make_unique<AtomInfo>(newAtom));
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

vector<TRuleBodyElement<AtomLiteral>> RuleDatabase::normalizeBody(const vector<AnyBodyElement>& elements)
{
    vector<TRuleBodyElement<AtomLiteral>> out;
    out.reserve(elements.size());

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

bool RuleDatabase::setAtomStatus(AtomInfo* atom, ETruthStatus status)
{
    vxy_assert(status != ETruthStatus::Undetermined);
    if (atom->status != status)
    {
        if (atom->status == ETruthStatus::Undetermined)
        {
            atom->status = status;
        }
        else
        {
            m_conflict = true;
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

bool RuleDatabase::setBodyStatus(BodyInfo* body, ETruthStatus status)
{
    vxy_assert(status != ETruthStatus::Undetermined);
    if (body->status != status)
    {
        if (body->status == ETruthStatus::Undetermined)
        {
            body->status = status;
        }
        else
        {
            m_conflict = true;
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
        if (atom->id != m_factAtom && atom->supports.empty())
        {
            if (!setAtomStatus(atom.get(), ETruthStatus::False))
            {
                return false;
            }
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
        AtomInfo* atom = m_atomsToPropagate.back();
        m_atomsToPropagate.pop_back();

        vxy_assert(atom->enqueued);
        atom->enqueued = false;

        vxy_assert(atom->status != ETruthStatus::Undetermined);
        if (!synchronizeAtomVariable(atom))
        {
            return false;
        }

        auto positiveSide = (atom->status == ETruthStatus::True) ? &atom->positiveDependencies : &atom->negativeDependencies;
        auto negativeSide = (atom->status == ETruthStatus::True) ? &atom->negativeDependencies : &atom->positiveDependencies;

        // For each body this atom is in positively, reduce that bodies' number of undeterminedTails.
        // If all the body's tails (i.e. atoms that make up the body) are determined, we can mark the body as true.
        for (auto it = positiveSide->begin(), itEnd = positiveSide->end(); it != itEnd; ++it)
        {
            BodyInfo* depBody = *it;
            vxy_assert(depBody->numUndeterminedTails > 0);
            depBody->numUndeterminedTails--;
            if (depBody->numUndeterminedTails == 0)
            {
                if (!setBodyStatus(depBody, ETruthStatus::True))
                {
                    return false;
                }
            }
        }

        // for each body this atom is in negatively, falsify the body
        for (auto it = negativeSide->begin(), itEnd = negativeSide->end(); it != itEnd; ++it)
        {
            BodyInfo* depBody = *it;
            vxy_assert(depBody->numUndeterminedTails > 0);
            depBody->numUndeterminedTails--;

            if (!setBodyStatus(depBody, ETruthStatus::False))
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
        BodyInfo* body = m_bodiesToPropagate.back();
        m_bodiesToPropagate.pop_back();

        vxy_assert(body->enqueued);
        body->enqueued = false;

        vxy_assert(body->status != ETruthStatus::Undetermined);

        if (body->status == ETruthStatus::True)
        {
            // mark all heads of this body as true
            for (auto it = body->heads.begin(), itEnd = body->heads.end(); it != itEnd; ++it)
            {
                if (!setAtomStatus(*it, ETruthStatus::True))
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
                AtomInfo* atom = *it;
                vxy_assert(contains(atom->supports.begin(), atom->supports.end(), body));
                atom->supports.erase_first_unsorted(body);
                if (atom->supports.size() == 0)
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

bool RuleDatabase::synchronizeAtomVariable(const AtomInfo* atom)
{
    vxy_assert(atom->status != ETruthStatus::Undetermined);
    if (!atom->equivalence.variable.isValid())
    {
        // no variable created yet
        return true;
    }

    if (atom->status == ETruthStatus::True)
    {
        if (!m_solver.getVariableDB()->constrainToValues(atom->equivalence, nullptr))
        {
            m_conflict = true;
            return false;
        }
    }
    else if (atom->status == ETruthStatus::False)
    {
        if (!m_solver.getVariableDB()->excludeValues(atom->equivalence, nullptr))
        {
            m_conflict = true;
            return false;
        }
    }
    return true;
}

const SolverVariableDomain& RuleDatabase::getDomain(VarID varID) const
{
    return m_solver.getDomain(varID);
}

