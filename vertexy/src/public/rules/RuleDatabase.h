// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "constraints/ConstraintGraphRelationInfo.h"
#include "rules/RuleTypes.h"
#include "topology/algo/tarjan.h"

namespace Vertexy
{

// API for adding ASP-style rules to a constraint solver.
class RuleDatabase
{
public:
    using RuleHead = TRuleHead<AtomID>;
    using RuleBody = TRuleBodyElement<AtomLiteral>;

    struct BodyInfo;

    struct AtomInfo
    {
        AtomInfo() {}
        AtomInfo(AtomID id) : id(id) {}

        AtomID id;
        // name for debugging
        wstring name;
        // the strongly connected component ID this belongs to
        int scc = -1;
        // the boolean variable corresponding to this atom, if created.
        VarID variable = VarID::INVALID;
        // Optional equivalence to a constraint solver literal
        Literal equivalence;
        // Bodies this head relies on for support
        vector<BodyInfo*> supports;
        // Bodies referring to this atom
        vector<BodyInfo*> positiveDependencies;
    };

    struct BodyInfo
    {
        BodyInfo() : id(-1) {}
        explicit BodyInfo(int id, const RuleBody& body) : id(id), body(body) {}

        int32_t id;
        // the strongly connected component ID this belongs to.
        int32_t scc = -1;
        // the solver literal corresponding with this body
        Literal lit;
        // The actual body literals
        RuleBody body;
        // heads relying on this body for (non)support
        vector<AtomInfo*> heads;
        // Whether this body was specified as a constraint (body with no head)
        bool isConstraint = false;
    };

    explicit RuleDatabase(ConstraintSolver& solver);
    RuleDatabase(const RuleDatabase&) = delete;
    RuleDatabase(RuleDatabase&&) = delete;

    AtomID createAtom(const wchar_t* name=nullptr);
    AtomLiteral createAtom(const Literal& equivalence, const wchar_t* name=nullptr);

    GraphAtomID createGraphAtom(const shared_ptr<ITopology>& topology, const wchar_t* name=nullptr);
    GraphAtomLiteral createGraphAtom(const shared_ptr<ITopology>& topology, const RuleGraphRelation& equivalence, const wchar_t* name=nullptr);

    template<typename H, typename B>
    void addRule(const H& head, const vector<B>& body)
    {
        vector<AnyBodyElement> vbody;
        vbody.reserve(body.size());
        for (auto& b : body) { vbody.push_back(TRuleBodyElement<B>::create(b)); }

        addRule(TRule(TRuleHead(head), vbody));
    }

    template<typename H, typename B>
    void addRule(const H& head, const B& singleElementBody)
    {
        vector<AnyBodyElement> vbody;
        vbody.push_back(TRuleBodyElement<B>::create(singleElementBody));

        addRule(TRule(TRuleHead(head), vbody));
    }

    void addRule(const TRuleDefinition<AtomID>& rule);
    void addRule(const TRuleDefinition<Literal>& rule);
    void addRule(const TRuleDefinition<SignedClause>& rule);

    // void addGraphRule(const shared_ptr<ITopology>& topology, const TGraphRuleDefinition<GraphAtomLiteral>& rule);
    // void addGraphRule(const shared_ptr<ITopology>& topology, const TGraphRuleDefinition<RuleGraphRelation>& rule);
    // void addGraphRule(const shared_ptr<ITopology>& topology, const TGraphRuleDefinition<AtomLiteral>& rule);
    // void addGraphRule(const shared_ptr<ITopology>& topology, const TGraphRuleDefinition<Literal>& rule);
    // void addGraphRule(const shared_ptr<ITopology>& topology, const TGraphRuleDefinition<SignedClause>& rule);

    void finalize();
    bool isTight() const { return m_isTight; }

    int getNumAtoms() const { return m_atoms.size(); }
    const AtomInfo* getAtom(AtomID id) const { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }
    AtomInfo* getAtom(AtomID id) { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }

    int getNumBodies() const { return m_bodies.size(); }
    const BodyInfo* getBody(int32_t id) const { return m_bodies[id].get(); }

protected:
    using NormalizedRule = TRule<AtomID, TRuleBodyElement<AtomLiteral>>;

    struct GraphAtomInfo
    {
        GraphAtomInfo() {}
        GraphAtomInfo(const wchar_t* name, const GraphLiteralRelationPtr& relation)
            : name(name)
            , relation(relation)
        {
        }

        wstring name;
        GraphLiteralRelationPtr relation;
    };

    struct BodyHasher
    {
        bool operator()(const BodyInfo* lhs, const BodyInfo* rhs) const
        {
            return compareBodies(lhs->body, rhs->body);
        }
        int32_t operator()(const BodyInfo* lhs) const
        {
            return hashBody(lhs->body);
        }

        static bool compareBodies(const RuleBody& lhs, const RuleBody& rhs);
        static int32_t hashBody(const RuleBody& body);
    };

    class NogoodBuilder
    {
    public:
        void clear() { m_literals.clear(); }
        void reserve(int n) { m_literals.reserve(n); }
        void add(const Literal& lit);
        void emit(ConstraintSolver& solver);

    protected:
        vector<Literal> m_literals;
    };

    using GraphRelationList = vector<tuple<GraphLiteralRelationPtr, GraphAtomID>>;
    using GraphAtomSet = hash_map<int32_t, unique_ptr<GraphAtomInfo>>;

    AtomID createHeadAtom(const Literal& equivalence);
    void transformRule(const RuleHead& head, const RuleBody& body);
    void transformSum(AtomID head, const RuleBody& sumBody);
    void transformChoice(const RuleHead& head, const RuleBody& body);
    void transformDisjunction(const RuleHead& head, const RuleBody& body);
    bool simplifyAndEmitRule(AtomID head, const RuleBody& body);

    BodyInfo* findOrCreateBodyInfo(const RuleBody& body);
    Literal getLiteralForAtom(AtomInfo* atomInfo);
    Literal translateAtomLiteral(AtomLiteral lit);

    bool isLiteralPossible(AtomLiteral literal) const;

    vector<RuleBody> normalizeBody(const vector<AnyBodyElement>& elements);
    RuleBody normalizeBodyElement(const AnyBodyElement& element);

    GraphLiteralRelationPtr normalizeGraphRelation(const RuleGraphRelation& relation) const;
    GraphAtomLiteral findExistingAtomForRelation(const shared_ptr<ITopology>& topology, const GraphLiteralRelationPtr& relation, const GraphRelationList& list) const;

    // void instantiateGraphRule(const TRuleHead<GraphAtomLiteral>& head, const vector<AnyGraphBodyElement>& body);

    template<typename T>
    void tarjanVisit(int node, T&& visitor);
    void computeSCCs();

    // Solver that owns us
    ConstraintSolver& m_solver;

    // Maps atoms to their corresponding boolean variable in the solver, and the literal they should be
    vector<unique_ptr<AtomInfo>> m_atoms;
    // Maps literals to atom that has equivalence
    hash_map<Literal, AtomID> m_atomMap;

    // Stored bodies.
    hash_set<BodyInfo*, BodyHasher> m_bodySet;
    vector<unique_ptr<BodyInfo>> m_bodies;

    // Graph -> list of created graph atoms for that graph
    hash_map<shared_ptr<ITopology>, unique_ptr<GraphAtomSet>> m_graphAtoms;

    // Maps clause relations to corresponding GraphAtom
    hash_map<shared_ptr<ITopology>, unique_ptr<GraphRelationList>> m_graphAtomMaps;

    NogoodBuilder m_nogoodBuilder;
    TarjanAlgorithm m_tarjan;

    bool m_isTight = true;
    int32_t m_nextGraphAtomID = 1;
};

} // namespace Vertexy
