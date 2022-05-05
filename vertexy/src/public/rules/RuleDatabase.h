// Copyright Proletariat, Inc. All Rights Reserved.
#pragma once

#include "ConstraintTypes.h"
#include "rules/RuleTypes.h"
#include "program/ProgramTypes.h"
#include "topology/GraphRelations.h"
#include "topology/algo/Tarjan.h"

namespace Vertexy
{

class AbstractBodyMapper;

// API for adding ASP-style rules to a constraint solver.
class RuleDatabase : public IVariableDomainProvider
{
public:
    struct BodyInfo;

    enum class ETruthStatus : uint8_t
    {
        False,
        True,
        Undetermined
    };

    using ALiteral = variant<Literal, GraphLiteralRelationPtr>;
    struct ConcreteAtomInfo;
    struct AbstractAtomInfo;

    struct AtomInfo
    {
        AtomInfo() {}
        AtomInfo(AtomID id) : id(id) {}
        virtual ~AtomInfo() {}

        virtual const AbstractAtomInfo* asAbstract() const { return nullptr; }
        virtual const ConcreteAtomInfo* asConcrete() const { return nullptr; }

        virtual ALiteral getLiteral(RuleDatabase& rdb, const AtomLiteral& atomLit) = 0;
        virtual ITopologyPtr getTopology() const = 0;
        virtual bool synchronize(RuleDatabase& rdb) const = 0;

        AbstractAtomInfo* asAbstract() { return const_cast<AbstractAtomInfo*>(const_cast<const AtomInfo*>(this)->asAbstract()); }
        ConcreteAtomInfo* asConcrete() { return const_cast<ConcreteAtomInfo*>(const_cast<const AtomInfo*>(this)->asConcrete()); }

        bool isChoiceAtom() const { return status == ETruthStatus::Undetermined; }

        AtomID id;
        // name for debugging
        wstring name;
        // Whether this is an external atom (possibly true even with no supports)
        bool isExternal = false;
        // Bodies this head relies on for support
        vector<BodyInfo*> supports;
        // Bodies referring to this atom positively
        vector<BodyInfo*> positiveDependencies;
        // Bodies referring to this atom negatively
        vector<BodyInfo*> negativeDependencies;
        // the current truth status for this atom
        ETruthStatus status = ETruthStatus::Undetermined;
        // whether this atom is enqueued for early propagation
        bool enqueued = false;
    };

    struct ConcreteAtomInfo : public AtomInfo
    {
        ConcreteAtomInfo() {}
        ConcreteAtomInfo(AtomID id, const shared_ptr<Literal>& bindDestination)
            : AtomInfo(id)
            , bindDestination(bindDestination)
        {
        }

        virtual const ConcreteAtomInfo* asConcrete() const override { return this; }
        virtual ALiteral getLiteral(RuleDatabase& rdb, const AtomLiteral& atomLit) override;

        Literal getConcreteLiteral(RuleDatabase& rdb, bool inverted);

        virtual ITopologyPtr getTopology() const override { return nullptr; }
        virtual bool synchronize(RuleDatabase& rdb) const override;

        bool isVariable() const { return equivalence.variable.isValid(); }

        // the strongly connected component ID this belongs to
        int scc = -1;
        // Optional equivalence to a constraint solver literal
        Literal equivalence;
        shared_ptr<Literal> bindDestination;
    };

    struct AbstractAtomInfo : public AtomInfo
    {
        AbstractAtomInfo() {}
        AbstractAtomInfo(AtomID id, const ITopologyPtr& topology)
            : AtomInfo(id)
            , topology(topology)
        {
        }

        virtual const AbstractAtomInfo* asAbstract() const override { return this; }
        virtual ALiteral getLiteral(RuleDatabase& rdb, const AtomLiteral& atomLit) override;
        virtual ITopologyPtr getTopology() const override { return topology; }
        virtual bool synchronize(RuleDatabase& rdb) const override;

        // Set of relations where this atom was in the head of a rule
        using RelationSet = hash_map<AbstractAtomRelationInfoPtr, ETruthStatus>;
        RelationSet abstractLiterals;
        // The topology used for making this atom concrete
        ITopologyPtr topology;
        // number of literals marked ETruthStatus::Undecided. If all get marked true, then the entire atom gets marked true.
        int numUndeterminedLiterals = 0;
    };

    struct ConcreteBodyInfo;
    struct AbstractBodyInfo;

    struct BodyInfo
    {
        BodyInfo() : id(-1) {}
        explicit BodyInfo(int id, const vector<AtomLiteral>& atomLits) : id(id), atomLits(atomLits) {}

        virtual ~BodyInfo() {}

        virtual const ConcreteBodyInfo* asConcrete() const { return nullptr; }
        virtual const AbstractBodyInfo* asAbstract() const { return nullptr; }

        ConcreteBodyInfo* asConcrete() { return const_cast<ConcreteBodyInfo*>(const_cast<const BodyInfo*>(this)->asConcrete()); }
        AbstractBodyInfo* asAbstract() { return const_cast<AbstractBodyInfo*>(const_cast<const BodyInfo*>(this)->asAbstract()); }

        virtual ALiteral getLiteral(RuleDatabase& rdb, bool inverted=false) = 0;
        virtual ITopologyPtr getTopology() const = 0;

        bool isChoiceBody() const { return status == ETruthStatus::Undetermined; }

        int32_t id;
        // The actual body literals
        vector<AtomLiteral> atomLits;
        // heads relying on this body for (non)support
        vector<AtomLiteral> heads;
        // whether this body must not ever hold true
        bool isNegativeConstraint = false;
        // how many literals within the body that have not yet been assigned True status
        int numUndeterminedTails = 0;
        // the current truth status for this atom
        ETruthStatus status = ETruthStatus::Undetermined;
        // whether this atom is enqueued for early propagation
        bool enqueued = false;
        // for the abstract head literal(s) this is attached to, the abstract relation info for it.
        AbstractAtomRelationInfoPtr headRelationInfo;
    };

    struct ConcreteBodyInfo : public BodyInfo
    {
        ConcreteBodyInfo() {}
        explicit ConcreteBodyInfo(int id, const vector<AtomLiteral>& bodyLits)
            : BodyInfo(id, bodyLits)
        {
        }

        virtual const ConcreteBodyInfo* asConcrete() const override { return this; }
        virtual ALiteral getLiteral(RuleDatabase& rdb, bool inverted=false) override;
        virtual ITopologyPtr getTopology() const override { return nullptr; }

        // the strongly connected component ID this belongs to.
        int32_t scc = -1;
        // the solver literal corresponding with this body
        Literal equivalence;
    };

    struct AbstractBodyInfo : public BodyInfo
    {
        AbstractBodyInfo() {}
        explicit AbstractBodyInfo(int id, const vector<AtomLiteral>& bodyLits, const ITopologyPtr& topology)
            : BodyInfo(id, bodyLits)
            , topology(topology)
        {
        }

        virtual const AbstractBodyInfo* asAbstract() const override { return this; }
        virtual ALiteral getLiteral(RuleDatabase& rdb, bool inverted=false) override;
        virtual ITopologyPtr getTopology() const override { return topology; }

        GraphLiteralRelationPtr makeRelationForAbstractHead(RuleDatabase& rdb, const AbstractAtomRelationInfoPtr& headRelInfo);

        ITopologyPtr topology;
        shared_ptr<AbstractBodyMapper> bodyMapper;
        GraphLiteralRelationPtr relation;
        GraphLiteralRelationPtr invRelation;
    };

    explicit RuleDatabase(ConstraintSolver& solver);
    RuleDatabase(const RuleDatabase&) = delete;
    RuleDatabase(RuleDatabase&&) = delete;

    AtomID createAtom(const wchar_t* name=nullptr, const shared_ptr<Literal>& bindDestination=nullptr, bool external=false);
    AtomID createBoundAtom(const Literal& equivalence, const wchar_t* name=nullptr, bool external=false);
    AtomID createAbstractAtom(const ITopologyPtr& topology, const wchar_t* name=nullptr, bool external=false);

    const ConstraintSolver& getSolver() const { return m_solver; }
    ConstraintSolver& getSolver() { return m_solver; }

    void addRule(const AtomLiteral& head, const vector<AtomLiteral>& body, const ITopologyPtr& topology=nullptr);

    bool finalize();
    bool isTight() const { return m_isTight; }

    int getNumAtoms() const { return m_atoms.size(); }
    const AtomInfo* getAtom(AtomID id) const { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }
    AtomInfo* getAtom(AtomID id) { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }

    AtomID getTrueAtom();

    int getNumBodies() const { return m_bodies.size(); }
    const BodyInfo* getBody(int32_t id) const { return m_bodies[id].get(); }

protected:
    // IVariableDomainProvider
    virtual const SolverVariableDomain& getDomain(VarID varID) const override;

    struct BodyHasher
    {
        bool operator()(const BodyInfo* lhs, const BodyInfo* rhs) const
        {
            return compareBodies(lhs->atomLits, rhs->atomLits);
        }
        int32_t operator()(const BodyInfo* lhs) const
        {
            return hashBody(lhs->atomLits);
        }

        static bool compareBodies(const vector<AtomLiteral>& lhs, const vector<AtomLiteral>& rhs);
        static int32_t hashBody(const vector<AtomLiteral>& body);
    };

    class NogoodBuilder
    {
    public:
        void clear() { m_literals.clear(); }
        void reserve(int n) { m_literals.reserve(n); }
        bool empty() const { return m_literals.empty(); }
        void add(const ALiteral& lit, const ITopologyPtr& topology);
        void emit(ConstraintSolver& solver, bool cullUnresolved);

    protected:
        void recurseTopology(ConstraintSolver& solver, const vector<int>& indices, int pos, vector<Literal>& appendLits);
        vector<ALiteral> m_literals;
        vector<ITopologyPtr> m_topologies;
    };

    struct GroundingData
    {
        GroundingData(int numOldAtoms, int numOldBodies)
        {
            newAtoms.push_back(make_unique<ConcreteAtomInfo>()); // sentinel
            bodyMappings.resize(numOldBodies);
            concreteAtomMappings.resize(numOldAtoms, AtomID());
            abstractAtomMappings.resize(numOldAtoms);
        }

        vector<unique_ptr<AtomInfo>> newAtoms;
        vector<unique_ptr<BodyInfo>> newBodies;
        vector<vector<int32_t>> bodyMappings;
        vector<AtomID> concreteAtomMappings;
        vector<hash_map<Literal, AtomID>> abstractAtomMappings;
    };

    static ALiteral invertLiteral(const ALiteral& lit);
    static GraphLiteralRelationPtr invertRelation(const GraphLiteralRelationPtr& rel);
    static bool isConcreteLiteral(const ALiteral& lit);
    
    bool markAtomFalse(AtomInfo* atom);
    bool markAtomTrue(const AtomLiteral& atomLit);
    bool setAtomStatus(AtomInfo* atom, ETruthStatus status);
    
    bool setBodyStatus(BodyInfo* body, ETruthStatus status);
    bool propagateFacts();
    bool isLiteralAssumed(const AtomLiteral& literal) const;

    bool emptyAtomQueue();
    bool emptyBodyQueue();

    BodyInfo* findOrCreateBodyInfo(const vector<AtomLiteral>& body, const ITopologyPtr& topology, const AbstractAtomRelationInfoPtr& headRelationInfo);

    template<typename T>
    void tarjanVisit(int node, T&& visitor);
    void computeSCCs();

    void makeConcrete();
    void groundBodyToConcrete(const BodyInfo& oldBody, GroundingData& groundingData);
    void groundAtomToConcrete(const AtomID oldAtom, GroundingData& groundingData);
    vector<AtomLiteral> groundLiteralsToConcrete(const vector<AtomLiteral>& oldLits, GroundingData& groundingData, bool& outSomeFailed, int vertex=-1);
    void hookupGroundedDependencies(ConcreteBodyInfo* newBodyInfo, GroundingData& groundingData);

    // Solver that owns us
    ConstraintSolver& m_solver;

    struct HashALiteral
    {
        size_t operator()(const tuple<ALiteral, ITopologyPtr>& alit) const
        {
            return visit([](auto&& typedLit)
            {
                using Type = decay_t<decltype(typedLit)>;
                if constexpr (is_same_v<Type, Literal>)
                {
                    return eastl::hash<Literal>()(typedLit);
                }
                else
                {
                    return typedLit->hash();
                }
            }, get<ALiteral>(alit));
        }
    };

    struct CompareALiteral
    {
        bool operator()(const tuple<ALiteral, ITopologyPtr>& lhs, const tuple<ALiteral, ITopologyPtr>& rhs) const
        {
            if (get<ALiteral>(lhs).index() != get<ALiteral>(rhs).index())
            {
                return false;
            }

            if (get<ITopologyPtr>(lhs) != get<ITopologyPtr>(rhs))
            {
                return false;
            }

            auto& lhsLit = get<ALiteral>(lhs);
            auto& rhsLit = get<ALiteral>(rhs);

            return visit([&](auto&& typedLeft)
            {
                using Type = decay_t<decltype(typedLeft)>;
                if constexpr (is_same_v<Type, Literal>)
                {
                    return typedLeft == get<Literal>(rhsLit);
                }
                else
                {
                    return typedLeft->equals(*get<GraphLiteralRelationPtr>(rhsLit));
                }
            }, lhsLit);
        }
    };

    // Maps atoms to their corresponding boolean variable in the solver, and the literal they should be
    vector<unique_ptr<AtomInfo>> m_atoms;

    // Stored bodies.
    hash_set<BodyInfo*, BodyHasher> m_bodySet;
    vector<unique_ptr<BodyInfo>> m_bodies;

    vector<AtomInfo*> m_atomsToPropagate;
    vector<BodyInfo*> m_bodiesToPropagate;
    bool m_conflict = false;

    NogoodBuilder m_nogoodBuilder;
    TarjanAlgorithm m_tarjan;

    bool m_isTight = true;
    AtomID m_trueAtom;
};

class AbstractBodyMapper
{
public:
    AbstractBodyMapper(RuleDatabase& rdb, const RuleDatabase::AbstractBodyInfo* bodyInfo, const AbstractAtomRelationInfoPtr& headRelationInfo=nullptr);
    Literal getForHead(const vector<int>& concreteHeadArgs);
    bool getForVertex(ITopology::VertexID vertex, Literal& outLit);

protected:
    Literal makeForArgs(const vector<int>& args, size_t argHash);
    bool checkValid(ITopology::VertexID vertex, const vector<int>& args);
    
    wstring makeVarName(const vector<int>& concreteHeadArgs) const;
    wstring litToString(const AtomLiteral& lit) const;

    struct ArgumentHasher
    {
        size_t operator()(const vector<int>& concreteArgs) const
        {
            size_t hash = 0;
            for (auto& arg : concreteArgs)
            {
                combineHashes(hash, eastl::hash<int>()(arg));
            }
            return hash;
        }
    };

    RuleDatabase& m_rdb;
    AbstractAtomRelationInfoPtr m_headRelationInfo;
    const RuleDatabase::AbstractBodyInfo* m_bodyInfo;
    mutable hash_map<vector<int>, Literal, ArgumentHasher> m_bindMap;
    mutable vector<int> m_concrete;
};

class BoundBodyInstantiatorRelation : public IGraphRelation<Literal>
{
public:
    BoundBodyInstantiatorRelation(const shared_ptr<AbstractBodyMapper>& mapper, const AbstractAtomRelationInfoPtr& headRelation);

    virtual bool getRelation(VertexID sourceVertex, Literal& out) const override;
    virtual size_t hash() const override;
    virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
    virtual wstring toString() const override;

protected:    
    shared_ptr<AbstractBodyMapper> m_mapper;
    AbstractAtomRelationInfoPtr m_headRelation;
    mutable vector<int> m_concrete;
};

// Maps a vertex to a body literal
class BodyInstantiatorRelation : public IGraphRelation<Literal>
{
public:
    BodyInstantiatorRelation(const shared_ptr<AbstractBodyMapper>& mapper);

    virtual bool getRelation(VertexID sourceVertex, Literal& out) const override;
    virtual size_t hash() const override;
    virtual wstring toString() const override { return TEXT("BodyInstantiator"); }

protected:
    shared_ptr<AbstractBodyMapper> m_mapper;    
};

} // namespace Vertexy
