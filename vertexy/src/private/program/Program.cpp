// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/Program.h"

#include "program/ProgramDSL.h"
#include "program/ExternalFormula.h"

using namespace Vertexy;

ProgramInstance* Program::s_currentInstance = nullptr;
int Program::s_nextFormulaUID = 1;
int Program::s_nextVarUID = 1;

// Provider for Program::graphLink()
class GraphLinkProvider : public IExternalFormulaProvider
{
public:
    GraphLinkProvider(FormulaUID uid, const ITopologyPtr& topology, const TopologyLink& link)
        : m_uid(uid)
        , m_topology(topology)
        , m_link(link)
    {
        m_name.sprintf(TEXT("graphLink(%s)"), link.toString(topology).c_str());
    }

    GraphLinkProvider(FormulaUID uid, const ITopologyPtr& topology, TopologyLink&& link)
        : m_uid(uid)
        , m_topology(topology)
        , m_link(move(link))
    {
        m_name.sprintf(TEXT("graphLink(%s)"), link.toString(topology).c_str());
    }

    virtual bool canInstantiate(int argIndex) const override
    {
        // Theoretically possible to support binding first argument, except that topologies don't generally have a way
        // of following a TopologyLink in reverse direction.
        return argIndex != 0;
    }

    virtual bool eval(const vector<ProgramSymbol>& args) const override
    {
        int resolved;
        if (!m_link.resolve(m_topology, args[0].getInt(), resolved))
        {
            return false;
        }

        return resolved == args[1].getInt();
    }

    virtual void startMatching(vector<ExternalFormulaMatchArg>&& args) override
    {
        vxy_assert(args.size() == 2); // should've been caught by type system?
        vxy_assert(args[0].isBound());

        m_matchResult = move(args);
        m_matched = false;
    }

    virtual bool matchNext(bool& isFact) override
    {
        if (m_matched)
        {
            return false;
        }
        m_matched = true;

        isFact = false;

        // bind right hand side if not done already
        const ProgramSymbol& left = m_matchResult[0].get();
        if (!m_matchResult[1].isBound())
        {
            ProgramSymbol* right = m_matchResult[1].getWriteable();
            if (left.isAbstract())
            {
                auto linkRel = make_shared<TopologyLinkIndexGraphRelation>(m_topology, m_link);
                *right = ProgramSymbol(left.getAbstractRelation()->map(linkRel));
            }
            else
            {
                int resolved;
                if (!m_link.resolve(m_topology, left.getInt(), resolved))
                {
                    return false;
                }
                *right = ProgramSymbol(resolved);
            }
        }

        // If this is a concrete call, make sure it fits.
        const ProgramSymbol& right = m_matchResult[1].get();
        if (left.isInteger() && right.isInteger())
        {
            int resolved;
            if (!m_link.resolve(m_topology, left.getInt(), resolved))
            {
                return false;
            }
            if (resolved != right.getInt())
            {
                return false;
            }

            // Only a fact if we have two concrete arguments.
            isFact = true;
        }

        return true;
    }

    virtual size_t hash() const override
    {
        return combineHashes(eastl::hash<FormulaUID>()(m_uid), m_link.hash());
    }

    const wchar_t* getName() const { return m_name.c_str(); }

protected:
    FormulaUID m_uid;
    ITopologyPtr m_topology;
    TopologyLink m_link;
    wstring m_name;
    vector<ExternalFormulaMatchArg> m_matchResult;
    bool m_matched = false;
};

// Provider for Program::hasGraphLink()
class HasGraphLinkProvider : public IExternalFormulaProvider
{
public:
    HasGraphLinkProvider(FormulaUID uid, const ITopologyPtr& topology, const TopologyLink& link)
        : m_uid(uid)
        , m_topology(topology)
        , m_link(link)
    {
        m_name.sprintf(TEXT("hasGraphLink(%s)"), link.toString(topology).c_str());
    }

    HasGraphLinkProvider(FormulaUID uid, const ITopologyPtr& topology, TopologyLink&& link)
        : m_uid(uid)
        , m_topology(topology)
        , m_link(move(link))
    {
        m_name.sprintf(TEXT("hasGraphLink(%s)"), link.toString(topology).c_str());
    }

    virtual bool canInstantiate(int argIndex) const override
    {
        return false;
    }

    virtual bool eval(const vector<ProgramSymbol>& args) const override
    {
        int resolved;
        return m_link.resolve(m_topology, args[0].getInt(), resolved);
    }

    virtual void startMatching(vector<ExternalFormulaMatchArg>&& args) override
    {
        vxy_assert(args.size() == 1); // should've been caught by type system?

        m_matchResult = move(args);
        m_matched = false;
    }

    virtual bool matchNext(bool& isFact) override
    {
        if (m_matched)
        {
            return false;
        }
        m_matched = true;

        isFact = false;

        vxy_assert(m_matchResult[0].isBound());        
        // If this is a concrete call, make sure it fits.
        const ProgramSymbol& arg = m_matchResult[0].get();
        if (arg.isInteger())
        {
            isFact = true;

            int resolved;
            return m_link.resolve(m_topology, arg.getInt(), resolved);
        }

        return true;
    }

    virtual size_t hash() const override
    {
        return combineHashes(eastl::hash<FormulaUID>()(m_uid), m_link.hash());
    }

    const wchar_t* getName() const { return m_name.c_str(); }

protected:
    FormulaUID m_uid;
    ITopologyPtr m_topology;
    TopologyLink m_link;
    wstring m_name;
    vector<ExternalFormulaMatchArg> m_matchResult;
    bool m_matched = false;
};

// Provider for Program::graphEdge
class GraphEdgeProvider : public IExternalFormulaProvider
{
public:
    GraphEdgeProvider(FormulaUID uid, const ITopologyPtr& topology)
        : m_uid(uid)
        , m_topology(topology)
    {
        m_name = TEXT("Edges");

        m_maxOutEdgeCount = 0;
        m_maxInEdgeCount = 0;
        for (int vertex = 0; vertex < topology->getNumVertices(); ++vertex)
        {
            int edgeCount = topology->getNumOutgoing(vertex);
            if (edgeCount > m_maxOutEdgeCount)
            {
                m_maxOutEdgeCount = edgeCount;
            }
            edgeCount = topology->getNumIncoming(vertex);
            if (edgeCount > m_maxInEdgeCount)
            {
                m_maxInEdgeCount = edgeCount;
            }
        }
    }

    virtual size_t hash() const override
    {
        return eastl::hash<FormulaUID>()(m_uid);
    }

    virtual bool eval(const vector<ProgramSymbol>& args) const override
    {
        int from = args[0].getInt();
        int to = args[1].getInt();

        if (!m_topology->isValidVertex(from) || !m_topology->isValidVertex(to))
        {
            return false;
        }

        for (int edgeIdx = 0; edgeIdx < m_topology->getNumOutgoing(from); ++edgeIdx)
        {
            int resolved;
            if (m_topology->getOutgoingDestination(from, edgeIdx, resolved))
            {
                if (resolved == to)
                {
                    return true;
                }
            }
        }
        return false;
    }

    virtual bool canInstantiate(int argIndex) const override
    {
        return true;
    }

    virtual void startMatching(vector<ExternalFormulaMatchArg>&& args) override
    {
        vxy_assert(args.size() == 2);

        m_matchResult = move(args);
        m_nextEdge = 0;
    }

    virtual bool matchNext(bool& isFact) override
    {
        isFact = false;
        bool leftBound = m_matchResult[0].isBound();
        bool rightBound = m_matchResult[1].isBound();

        if (leftBound && rightBound)
        {
            if (m_nextEdge > 0)
            {
                return false;
            }

            isFact = true;

            bool matched = eval({m_matchResult[0].get(), m_matchResult[1].get()});
            m_nextEdge++;
            return matched;
        }
        else if (leftBound)
        {
            return matchRightSide(isFact);
        }
        else if (rightBound)
        {
            return matchLeftSide(isFact);
        }
        else
        {
            return matchBothSides();
        }
    }

    const wchar_t* getName() const { return m_name.c_str(); }

protected:
    using IncomingEdgeRelation = TVertexEdgeToVertexGraphRelation<ITopology, true>;
    using OutgoingEdgeRelation = TVertexEdgeToVertexGraphRelation<ITopology, false>;
    
    bool matchRightSide(bool& isFact)
    {
        const ProgramSymbol& left = m_matchResult[0].get();
        if (left.isAbstract())
        {
            if (m_nextEdge >= m_maxOutEdgeCount)
            {
                return false;
            }

            *m_matchResult[1].getWriteable() = ProgramSymbol(make_shared<OutgoingEdgeRelation>(m_topology, m_nextEdge));
            ++m_nextEdge;
            return true;
        }

        isFact = true;

        const int count = m_topology->getNumOutgoing(left.getInt());
        for (; m_nextEdge < count; ++m_nextEdge)
        {
            int destVertex;
            if (m_topology->getOutgoingDestination(left.getInt(), m_nextEdge, destVertex))
            {
                *m_matchResult[1].getWriteable() = ProgramSymbol(destVertex);
                break;
            }
        }

        if (m_nextEdge >= count)
        {
            return false;
        }

        ++m_nextEdge;
        return true;
    }

    bool matchLeftSide(bool& isFact)
    {
        const ProgramSymbol& right = m_matchResult[1].get();
        if (right.isAbstract())
        {
            if (m_nextEdge >= m_maxInEdgeCount)
            {
                return false;
            }

            *m_matchResult[0].getWriteable() = ProgramSymbol(make_shared<IncomingEdgeRelation>(m_topology, m_nextEdge));
            ++m_nextEdge;
            return true;
        }

        isFact = true;

        const int count = m_topology->getNumIncoming(right.getInt());
        for (; m_nextEdge < count; ++m_nextEdge)
        {
            int sourceVertex;
            if (m_topology->getIncomingSource(right.getInt(), m_nextEdge, sourceVertex))
            {
                *m_matchResult[0].getWriteable() = ProgramSymbol(sourceVertex);
                break;
            }
        }

        if (m_nextEdge >= count)
        {
            return false;
        }

        ++m_nextEdge;
        return true;
    }

    bool matchBothSides()
    {
        int layout = m_nextEdge / m_maxInEdgeCount;
        int localEdge = m_nextEdge - (layout * m_maxInEdgeCount);
        
        switch (layout)
        {
        case 0:
            *m_matchResult[0].getWriteable() = ProgramSymbol(IdentityGraphRelation::get());
            *m_matchResult[1].getWriteable() = ProgramSymbol(make_shared<OutgoingEdgeRelation>(m_topology, localEdge));
            break;
        case 1:
            *m_matchResult[0].getWriteable() = ProgramSymbol(make_shared<IncomingEdgeRelation>(m_topology, localEdge));
            *m_matchResult[1].getWriteable() = ProgramSymbol(IdentityGraphRelation::get());
            break;
        default:
            return false;
        }

        ++m_nextEdge;
        return true;
    }

    FormulaUID m_uid;
    ITopologyPtr m_topology;
    vector<ExternalFormulaMatchArg> m_matchResult;
    wstring m_name;
    int m_maxOutEdgeCount;
    int m_maxInEdgeCount;
    int m_nextEdge = 0;
};

ProgramVariable::ProgramVariable(const wchar_t* name)
    : m_name(name)
{
    m_uid = Program::allocateVariableUID();
}

void Program::disallow(detail::ProgramBodyTerm&& body)
{
    return disallow(detail::ProgramBodyTerms(forward<detail::ProgramBodyTerm>(body)));
}

void Program::disallow(detail::ProgramBodyTerms&& body)
{
    vxy_assert_msg(Program::getCurrentInstance() != nullptr, "Cannot specify rules outside of a Program::define block!");
    vector<UTerm> terms;
    auto rule = make_unique<RuleStatement>(nullptr, move(body.terms));
    s_currentInstance->addRule(move(rule));
}

ExternalFormula<2> Program::graphLink(const TopologyLink& link)
{
    vxy_assert_msg(getCurrentInstance() != nullptr, "graphLink can only be called from within a Program::define() block");
    vxy_assert_msg(getCurrentInstance()->getTopology() != nullptr, "graphLink can only be used with graph programs");

    FormulaUID uid = allocateFormulaUID();
    auto provider = make_shared<GraphLinkProvider>(uid, getCurrentInstance()->getTopology(), link);
    return ExternalFormula<2>(uid, provider, provider->getName());
}

Vertexy::detail::ProgramExternalFunctionTerm Program::hasGraphLink(detail::ProgramBodyTerm&& vertex, const TopologyLink& link)
{
    vxy_assert_msg(getCurrentInstance() != nullptr, "graphLink can only be called from within a Program::define() block");
    vxy_assert_msg(getCurrentInstance()->getTopology() != nullptr, "graphLink can only be used with graph programs");

    FormulaUID uid = allocateFormulaUID();
    auto provider = make_shared<HasGraphLinkProvider>(uid, getCurrentInstance()->getTopology(), link);
    ExternalFormula<1> formula(uid, provider, provider->getName());
    vector<detail::ProgramBodyTerm> args;
    args.push_back(move(vertex));

    return detail::ProgramExternalFunctionTerm(
        uid,
        provider->getName(),
        provider,
        move(args)
    );
}

Vertexy::detail::ProgramExternalFunctionTerm Program::graphEdge(detail::ProgramBodyTerm&& left, detail::ProgramBodyTerm&& right)
{
    vxy_assert_msg(getCurrentInstance() != nullptr, "graphEdge can only be called from within a Program::define() block");
    vxy_assert_msg(getCurrentInstance()->getTopology() != nullptr, "graphEdge can only be used with graph programs");

    FormulaUID uid = allocateFormulaUID();
    auto provider = make_shared<GraphEdgeProvider>(uid, getCurrentInstance()->getTopology());

    ExternalFormula<2> formula(uid, provider, provider->getName());
    vector<detail::ProgramBodyTerm> args;
    args.push_back(move(left));
    args.push_back(move(right));

    return detail::ProgramExternalFunctionTerm(
        uid,
        TEXT("graphEdge"),
        provider,
        move(args)
    );
}

Vertexy::detail::ProgramRangeTerm Program::range(detail::ProgramBodyTerm min, detail::ProgramBodyTerm max)
{
    // TODO: validate arguments
    static LiteralTerm::AbstractOverrideMap tempMap;
    int minV = min.term->eval(tempMap, ProgramSymbol()).getInt();
    int maxV = max.term->eval(tempMap, ProgramSymbol()).getInt();
    vxy_assert_msg(maxV >= minV, "invalid range");
    return detail::ProgramRangeTerm(minV, maxV);
}

ProgramInstance::ProgramInstance(const shared_ptr<ITopology>& topology)
    : m_topology(topology)
{
}

ProgramInstance::~ProgramInstance()
{
}

void ProgramInstance::addRule(URuleStatement&& rule)
{
    m_ruleStatements.push_back(move(rule));
}

void ProgramInstance::addBinder(FormulaUID formulaUID, unique_ptr<BindCaller>&& binder)
{
    vxy_assert(m_binders.find(formulaUID) == m_binders.end());
    m_binders[formulaUID] = move(binder);
}
