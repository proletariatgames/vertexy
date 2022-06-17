// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramInstantiators.h"

using namespace Vertexy;

FunctionInstantiator::FunctionInstantiator(FunctionTerm& term, const ProgramCompiler::AtomDomain& domain, bool canBeAbstract, const ITopologyPtr& topology)
    : m_term(term)
    , m_domain(domain)
    , m_canBeAbstract(canBeAbstract)
    , m_topology(topology)
{
    vxy_assert(m_term.provider == nullptr);
    m_numDomainAtoms = m_domain.list.size();
}

void FunctionInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    m_index = 0;
    m_subIndex = 0;
    m_forceConcrete = !m_canBeAbstract || m_term.domainContainsAbstracts() || !m_term.containsAbstracts();
    m_visited.clear();
    match(overrideMap, boundVertex);
}

void FunctionInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    vxy_assert(m_term.provider == nullptr);
    if (m_hitEnd)
    {
        return;
    }

    if (m_term.negated)
    {
        // all wildcards should be fully bound at this point, because positive literals are always earlier in
        // the dependency list. Therefore, we can eval safely.
        m_term.boundMask = m_term.getDomain(overrideMap, boundVertex);
        ProgramSymbol matched = m_term.eval(overrideMap, boundVertex);
        if (matched.isInvalid())
        {
            m_hitEnd = true;
            return;
        }

        vxy_assert(matched.getFormula()->uid == m_term.functionUID);
        auto found = m_domain.map.find(matched.negatedFormula().unmasked());
        if (found != m_domain.map.end())
        {
            auto& facts = m_domain.list[found->second].facts;
            if (matched.getFormula()->mask.isSubsetOf(facts))
            {
                m_hitEnd = true;
            }
        }
        
        m_term.assignedToFact = false;
    }
    else
    {
        auto isFact = [&](const CompilerAtom& assignedAtom)
        {
            return !assignedAtom.facts.isZero() &&
                   assignedAtom.facts.isSubsetOf(m_term.boundMask) &&
                   !m_term.eval(overrideMap, boundVertex).containsAbstract();
        };
        
        for (; m_index < m_numDomainAtoms; moveNextDomainAtom())
        {
            const CompilerAtom& atom = m_domain.list[m_index];
            vxy_assert(!atom.symbol.isNegated());

            if (m_forceConcrete && !boundVertex.isInteger() &&
                (atom.symbol.containsAbstract() || m_term.domainContainsAbstracts()))
            {
                for (; m_subIndex < m_topology->getNumVertices(); ++m_subIndex)
                {
                    int vertex = m_subIndex;
                    if (boundVertex.isAbstract() && !boundVertex.getAbstractRelation()->getRelation(m_subIndex, vertex))
                    {
                        continue;
                    }
                    
                    ProgramSymbol concreteSymbol = atom.symbol.makeConcrete(vertex);
                    if (!concreteSymbol.isValid())
                    {
                        continue;
                    }

                    auto prevBoundVertex = boundVertex;
                    boundVertex = ProgramSymbol(vertex); 
                    if (matches(concreteSymbol, overrideMap, boundVertex))
                    {
                        m_term.assignedToFact = isFact(atom);
                        ++m_subIndex;
                        return;
                    }
                    boundVertex = prevBoundVertex;
                }
            
                continue;
            }

            auto prevBoundVertex = boundVertex;
            if (matches(atom.symbol, overrideMap, boundVertex))
            {
                m_visited.insert(atom.symbol);
                m_term.assignedToFact = isFact(atom);
                moveNextDomainAtom();
                return;
            }
            boundVertex = prevBoundVertex;
        }
        m_hitEnd = true;
    }
}

void FunctionInstantiator::moveNextDomainAtom()
{
    ++m_index;
    m_subIndex = 0;
}

bool FunctionInstantiator::matches(const ProgramSymbol& symbol, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    AbstractOverrideMap newOverrideMap = overrideMap;
    if (!m_term.match(symbol, newOverrideMap, boundVertex))
    {
        return false;
    }

    auto applied = m_term.eval(newOverrideMap, boundVertex);
    if (m_visited.find(applied) == m_visited.end())
    {
        m_visited.insert(applied);
        overrideMap = move(newOverrideMap);
        return true; 
    }
    return false;
}

bool FunctionInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_term.negated)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

ExternalFunctionInstantiator::ExternalFunctionInstantiator(FunctionTerm& term)
    : m_term(term)
    , m_hitEnd(false)
{
    vxy_assert(m_term.provider != nullptr);
}

void ExternalFunctionInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;

    bool allArgumentsBound = true;
    bool anyAbstractArguments = false;
    
    vector<ExternalFormulaMatchArg> matchArgs;
    matchArgs.reserve(m_term.arguments.size());
    for (auto& arg : m_term.arguments)
    {
        if (auto wcArg = dynamic_cast<WildcardTerm*>(arg.get()))
        {
            if (wcArg->isBinder)
            {
                allArgumentsBound = false;
                matchArgs.push_back(ExternalFormulaMatchArg::makeUnbound(wcArg->sharedBoundRef));
            }
            else
            {
                ProgramSymbol boundWcVal = wcArg->eval(overrideMap, boundVertex);
                if (!boundWcVal.isValid())
                {
                    m_hitEnd = true;
                    return;
                }                
                matchArgs.push_back(ExternalFormulaMatchArg::makeBound(boundWcVal));
                if (boundWcVal.isAbstract())
                {
                    anyAbstractArguments = true;
                }
            }
        }
        else if (auto symArg = dynamic_cast<SymbolTerm*>(arg.get()))
        {
            matchArgs.push_back(ExternalFormulaMatchArg::makeBound(symArg->sym));
        }
        else if (auto vertexArg = dynamic_cast<VertexTerm*>(arg.get()))
        {
            matchArgs.push_back(ExternalFormulaMatchArg::makeBound(vertexArg->eval(overrideMap, boundVertex)));
        }
        else
        {
            vxy_fail_msg("Unsupported external formula argument");
            m_hitEnd = true;
            return;
        }
    }

    m_needsAbstractRelation = (allArgumentsBound && anyAbstractArguments);
    if (!m_needsAbstractRelation)
    {
        m_term.provider->startMatching(move(matchArgs));
    }
    match(overrideMap, boundVertex);
}

void ExternalFunctionInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    vxy_assert(m_term.provider != nullptr);
    if (m_hitEnd)
    {
        return;
    }

    m_term.boundMask = m_term.getDomain(overrideMap, boundVertex);
    if (m_needsAbstractRelation)
    {
        m_term.assignedToFact = false;
    }
    else
    {
        bool isFact = false;
        m_hitEnd = !m_term.provider->matchNext(isFact);

        if (m_term.negated && !m_hitEnd && isFact)
        {
            m_hitEnd = true;
            return;
        }

        m_term.assignedToFact = isFact;
    }
}

bool ExternalFunctionInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_term.negated || m_needsAbstractRelation)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

ExternalConcreteFunctionInstantiator::ExternalConcreteFunctionInstantiator(FunctionTerm& term, const ITopologyPtr& topology)
    : m_term(term)
    , m_topology(topology)
    , m_nextVertex(0)
    , m_hitEnd(false)
{
    vxy_fail_msg("Not yet working");
}

void ExternalConcreteFunctionInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_nextVertex = 0;
    m_hitEnd = false;
    match(overrideMap, boundVertex);
}

void ExternalConcreteFunctionInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (m_hitEnd)
    {
        return;
    }
    
    if (boundVertex.isValid())
    {
        if (m_nextVertex > 0)
        {
            m_hitEnd = true;
            return;
        }
        
        vxy_assert(boundVertex.isInteger());
        if (matches(boundVertex.getInt(), overrideMap))
        {
            ++m_nextVertex;
            return;
        }
        m_hitEnd = true;
    }
    else
    {
        for (; m_nextVertex < m_topology->getNumVertices(); ++m_nextVertex)
        {
            boundVertex = ProgramSymbol(m_nextVertex); 
            if (matches(m_nextVertex, overrideMap))
            {
                ++m_nextVertex;
                return;
            }
            boundVertex = {};
        }
        m_hitEnd = true;
    }
}

bool ExternalConcreteFunctionInstantiator::hitEnd() const
{
    return m_nextVertex >= m_topology->getNumVertices();
}

bool ExternalConcreteFunctionInstantiator::matches(int vertex, const AbstractOverrideMap& overrideMap)
{
    m_term.boundMask = m_term.getDomain(overrideMap, vertex);

    vector<ProgramSymbol> concreteArgs;
    for (auto& arg : m_term.arguments)
    {
        ProgramSymbol concreteArg = arg->eval(overrideMap, vertex);
        if (!concreteArg.isValid())
        {
            return false;
        }
        concreteArgs.push_back(move(concreteArg));
    }

    if (m_term.negated == m_term.provider->eval(concreteArgs))
    {
        return false;
    }
    return true;
}

EqualityInstantiator::EqualityInstantiator(BinaryOpTerm& term, bool canBeAbstract, const ProgramCompiler& compiler, const ITopologyPtr& topology)
    : m_term(term)
    , m_canBeAbstract(canBeAbstract)
    , m_compiler(compiler)
    , m_topology(topology)
    , m_nextVertex(0)
    , m_hitEnd(false)
{
    vxy_assert(term.op == EBinaryOperatorType::Equality);
}

void EqualityInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    m_nextVertex = 0;
    match(overrideMap, boundVertex);
}

void EqualityInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (m_hitEnd)
    {
        return;
    }

    if (m_canBeAbstract || boundVertex.isValid() || !m_term.containsAbstracts())
    {
        // all wildcards in right hand side should be fully bound now
        ProgramSymbol rhsSym = m_term.rhs->eval(overrideMap, boundVertex);
        if (rhsSym.isAbstract() || (dynamic_cast<VertexTerm*>(m_term.lhs.get()) != nullptr))
        {
            // Create an abstract relation
            ProgramSymbol sym = m_term.eval(overrideMap, boundVertex);
            if (sym.isInvalid())
            {
                m_hitEnd = true;
            }
            else
            {
                vxy_assert(sym.isAbstract());
            }
        }
        else
        {
            if (!rhsSym.isValid() || !m_term.lhs->match(rhsSym, overrideMap, boundVertex))
            {
                m_hitEnd = true;
            }
        }
    }
    else
    {
        for (; m_nextVertex < m_topology->getNumVertices(); ++m_nextVertex)
        {
            boundVertex = ProgramSymbol(m_nextVertex);

            ProgramSymbol rhsSym = m_term.rhs->eval(overrideMap, boundVertex);
            if (rhsSym.isValid())
            {
                vxy_assert(!rhsSym.containsAbstract());
                if (m_term.lhs->match(rhsSym, overrideMap, boundVertex))
                {
                    ++m_nextVertex;
                    return;
                }
            }

            boundVertex = {};
        }
        m_hitEnd = true;
    }
}

bool EqualityInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_canBeAbstract)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

RelationInstantiator::RelationInstantiator(BinaryOpTerm& term, bool canBeAbstract, const ProgramCompiler& compiler, const ITopologyPtr& topology)
    : m_term(term)
    , m_canBeAbstract(canBeAbstract)
    , m_compiler(compiler)
    , m_topology(topology)
    , m_nextVertex(0)
    , m_hitEnd(false)
{
    vxy_assert(isRelationOp(m_term.op));
}

void RelationInstantiator::first(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    m_hitEnd = false;
    m_nextVertex = 0;
    match(overrideMap, boundVertex);
}

void RelationInstantiator::match(AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (m_hitEnd)
    {
        return;
    }

    if (m_canBeAbstract || boundVertex.isValid() || !m_term.containsAbstracts())
    {
        // wildcards in non-assignment binary ops should be fully bound now
        ProgramSymbol sym = m_term.eval(overrideMap, boundVertex);
        // BinOpTerm::eval() will return 0 to indicate false.
        if (sym.isInvalid() || (sym.isInteger() && sym.getInt() == 0))
        {
            m_hitEnd = true;
        }
    }
    else
    {
        for (; m_nextVertex < m_topology->getNumVertices(); ++m_nextVertex)
        {
            boundVertex = ProgramSymbol(m_nextVertex);
            ProgramSymbol sym = m_term.eval(overrideMap, boundVertex);
            if (sym.isValid() && (!sym.isInteger() || sym.getInt() != 0))
            {
                m_nextVertex++;
                return;
            }
            boundVertex = {};
        }
        m_hitEnd = true;
    }
}

bool RelationInstantiator::hitEnd() const
{
    bool hadHit = m_hitEnd;
    if (m_canBeAbstract)
    {
        m_hitEnd = true;
    }
    return hadHit;
}

bool RelationInstantiator::isRelationOp(EBinaryOperatorType op)
{
    switch (op)
    {
    // handled by EqualityInstantiator
    // case EBinaryOperatorType::Equality:
    case EBinaryOperatorType::Inequality:
    case EBinaryOperatorType::LessThan:
    case EBinaryOperatorType::LessThanEq:
    case EBinaryOperatorType::GreaterThan:
    case EBinaryOperatorType::GreaterThanEq:
        return true;
    default:
        return false;
    }
}

