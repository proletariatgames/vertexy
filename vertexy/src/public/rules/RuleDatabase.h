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

class BindCaller;

class FactGraphFilter;
using FactGraphFilterPtr = shared_ptr<const FactGraphFilter>;

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
    
    using AClause = variant<SignedClause, GraphRelationClause>;
    struct ConcreteAtomInfo;
    struct AbstractAtomInfo;

    struct ArgumentHasher
    {
        size_t operator()(const vector<int>& concreteArgs) const
        {
            size_t hash = 0;
            for (auto& arg : concreteArgs)
            {
                hash = combineHashes(hash, eastl::hash<int>()(arg));
            }
            return hash;
        }
    };

    struct AtomLinkage
    {
        ValueSet mask;
        BodyInfo* body;
    };

    struct HeadAtomLinkage : AtomLinkage
    {
        HeadAtomLinkage() {}
        HeadAtomLinkage(const ValueSet& inMask, BodyInfo* inBody, bool inChoice)
            : AtomLinkage{inMask, inBody}
            , isChoice(inChoice)
        {            
        }
        bool isChoice = false;
    };
    
    struct AtomInfo
    {
        AtomInfo() {}
        explicit AtomInfo(AtomID id, int domainSize);

        AtomInfo(const AtomInfo&) = delete;
        AtomInfo(AtomInfo&&) = delete;

        AtomInfo& operator=(const AtomInfo&) = delete;
        AtomInfo& operator=(AtomInfo&&) = delete;
        
        virtual ~AtomInfo() {}

        virtual const AbstractAtomInfo* asAbstract() const { return nullptr; }
        virtual const ConcreteAtomInfo* asConcrete() const { return nullptr; }

        virtual AClause getClauseForAtomLiteral(const AtomLiteral& atomLit) const = 0;
        virtual FactGraphFilterPtr getFilter(const AtomLiteral& literal) const = 0;
        virtual ITopologyPtr getTopology() const = 0;

        virtual ETruthStatus getTruthStatus(const ValueSet& values) const = 0;
        virtual bool containsUnknowns(const ValueSet& values) const = 0;
        
        AbstractAtomInfo* asAbstract() { return const_cast<AbstractAtomInfo*>(const_cast<const AtomInfo*>(this)->asAbstract()); }
        ConcreteAtomInfo* asConcrete() { return const_cast<ConcreteAtomInfo*>(const_cast<const AtomInfo*>(this)->asConcrete()); }
        
        AtomID id;
        int domainSize = -1;
        // name for debugging
        wstring name;
        // Whether this is an external atom (possibly true even with no supports)
        bool isExternal = false;
        // the strongly connected component ID this belongs to
        int scc = -1;        
        // Bodies this head relies on for support
        vector<HeadAtomLinkage> supports;
        // Bodies referring to this atom positively
        vector<AtomLinkage> positiveDependencies;
        // Bodies referring to this atom negatively
        vector<AtomLinkage> negativeDependencies;
        // Mask of true facts
        ValueSet trueFacts;
        // Mask of false facts
        ValueSet falseFacts;
        // whether this atom is enqueued for early propagation
        bool enqueued = false;
    };

    struct ConcreteAtomInfo : public AtomInfo
    {
        ConcreteAtomInfo() {}
        ConcreteAtomInfo(AtomID inID, int inDomainSize)
            : AtomInfo(inID, inDomainSize)
        {
        }

        virtual const ConcreteAtomInfo* asConcrete() const override { return this; }
        virtual AClause getClauseForAtomLiteral(const AtomLiteral& atomLit) const override;
        virtual FactGraphFilterPtr getFilter(const AtomLiteral& literal) const override { return nullptr; }        
        virtual ITopologyPtr getTopology() const override { return nullptr; }
        virtual bool containsUnknowns(const ValueSet& values) const override;
        virtual ETruthStatus getTruthStatus(const ValueSet& values) const override;

        bool isChoiceAtom() const { return !trueFacts.isSingleton() && !falseFacts.isSingleton(); }
        
        bool isEstablished(const ValueSet& values) const;
        ETruthStatus getTruthStatus(int index) const;
        Literal getLiteralForIndex(int index) const;

        void createLiteral(RuleDatabase& rdb);
        bool synchronize(RuleDatabase& rdb);

        // The variable created for this atom and the mapping from the atom's domain to the variable's domain.
        SignedClause boundClause;
        // The variable domain associated with the bound clause.
        SolverVariableDomain boundDomain = SolverVariableDomain(0,0);

        AbstractAtomInfo* abstractParent = nullptr;
        int parentVertex = -1;
        AbstractAtomRelationInfoPtr parentRelationInfo;
    };

    struct AbstractAtomInfo : public AtomInfo
    {
        AbstractAtomInfo() {}
        AbstractAtomInfo(AtomID inID, int inDomainSize, const ITopologyPtr& topology);

        virtual const AbstractAtomInfo* asAbstract() const override { return this; }
        virtual AClause getClauseForAtomLiteral(const AtomLiteral& atomLit) const override;
        virtual ITopologyPtr getTopology() const override { return topology; }
        virtual FactGraphFilterPtr getFilter(const AtomLiteral& literal) const override;
        virtual bool containsUnknowns(const ValueSet& values) const override;
        virtual ETruthStatus getTruthStatus(const ValueSet& values) const override;

        void lockVariableCreation();
        
        // Set of relations where this atom was in the head of a rule
        using RelationMap = hash_map<AtomLiteral, ETruthStatus, call_hash>;        
        RelationMap abstractLiterals;

        // The set of AtomLiterals necessary to constrain the entire set of concrete atoms.
        // This will be a subset of abstractLiterals.
        vector<AtomLiteral> abstractLiteralsToConstrain;

        // The topology used for making this atom concrete
        ITopologyPtr topology;

        struct ConcreteAtomRecord
        {
            ConcreteAtomInfo* atom;
            hash_set<ValueSet> seenMasks;
        };

        // The set of concrete atoms created from this abstract atom, keyed by the concrete atom's arguments.
        hash_map<vector<int>, ConcreteAtomRecord, ArgumentHasher> concreteAtoms;
    };

    struct ConcreteBodyInfo;
    struct AbstractBodyInfo;

    struct HeadInfo
    {
        AtomLiteral lit;
        bool isChoice;
    };

    struct BodyInfo
    {
        BodyInfo() : id(-1) {}
        explicit BodyInfo(int id, const vector<AtomLiteral>& atomLits) : id(id), atomLits(atomLits) {}

        virtual ~BodyInfo() {}

        BodyInfo(const BodyInfo&) = delete;
        BodyInfo(BodyInfo&&) = delete;

        BodyInfo& operator=(const BodyInfo&) = delete;
        BodyInfo& operator=(BodyInfo&&) = delete;
        
        virtual const ConcreteBodyInfo* asConcrete() const { return nullptr; }
        virtual const AbstractBodyInfo* asAbstract() const { return nullptr; }

        ConcreteBodyInfo* asConcrete() { return const_cast<ConcreteBodyInfo*>(const_cast<const BodyInfo*>(this)->asConcrete()); }
        AbstractBodyInfo* asAbstract() { return const_cast<AbstractBodyInfo*>(const_cast<const BodyInfo*>(this)->asAbstract()); }

        virtual AClause getClause(RuleDatabase& rdb, bool allowCreation, bool inverted) const = 0;
        virtual FactGraphFilterPtr getFilter() const = 0;
        virtual ITopologyPtr getTopology() const = 0;
        virtual bool isFullyKnown() const { return status != ETruthStatus::Undetermined; }

        bool isChoiceBody() const { return status == ETruthStatus::Undetermined; }

        int32_t id;
        // The actual body literals
        vector<AtomLiteral> atomLits;
        // heads that are true if this body is true.
        vector<HeadInfo> heads;
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
        // the strongly connected component ID this belongs to.
        int32_t scc = -1;
    };

    struct ConcreteBodyInfo : public BodyInfo
    {
        ConcreteBodyInfo() {}
        explicit ConcreteBodyInfo(int inID, const vector<AtomLiteral>& bodyLits)
            : BodyInfo(inID, bodyLits)
        {
        }

        virtual const ConcreteBodyInfo* asConcrete() const override { return this; }
        virtual AClause getClause(RuleDatabase& rdb, bool allowCreation, bool inverted=false) const override;
        virtual ITopologyPtr getTopology() const override { return nullptr; }
        virtual FactGraphFilterPtr getFilter() const override { return nullptr; }

        // the variable associated with this body
        mutable VarID equivalence;

        // Maps an abstract parent of this body to the vertex it was instantiated for.
        hash_map<AbstractBodyInfo*, int> abstractParents;
    };

    struct AbstractBodyInfo : public BodyInfo
    {
        AbstractBodyInfo() {}
        explicit AbstractBodyInfo(int inID, const vector<AtomLiteral>& bodyLits, const ITopologyPtr& topology)
            : BodyInfo(inID, bodyLits)
            , topology(topology)
        {
        }

        virtual const AbstractBodyInfo* asAbstract() const override { return this; }
        virtual AClause getClause(RuleDatabase& rdb, bool allowCreation, bool inverted=false) const override;
        virtual ITopologyPtr getTopology() const override { return topology; }

        GraphVariableRelationPtr makeRelationForAbstractHead(RuleDatabase& rdb, const AbstractAtomRelationInfoPtr& headRelInfo);
        bool getHeadArgumentsForVertex(int vertex, vector<int>& outArgs) const;
        virtual FactGraphFilterPtr getFilter() const override;
        virtual bool isFullyKnown() const override;

        void lockVariableCreation();

        ITopologyPtr topology;
        mutable shared_ptr<AbstractBodyMapper> bodyMapper;
        mutable GraphVariableRelationPtr createRelation;
        mutable GraphVariableRelationPtr noCreateRelation;
        mutable FactGraphFilterPtr filter;

        struct ChildBodyHasher
        {
            size_t operator()(const vector<int>& elems) const
            {
                size_t out = 0;
                for (int i = 0; i < elems.size(); ++i)
                {
                    out ^= (elems[i] << (i%(sizeof(size_t)*8))); 
                }
                return out;
            }
        };
        
        // Maps concrete head arguments to the associated concrete body
        hash_map<vector<int>, ConcreteBodyInfo*, ChildBodyHasher> concreteBodies;
        int numUnknownConcretes = 0;
    };
    
    explicit RuleDatabase(ConstraintSolver& solver);
    RuleDatabase(const RuleDatabase&) = delete;
    RuleDatabase(RuleDatabase&&) = delete;

    AtomID createAtom(const wchar_t* name=nullptr, int domainSize=1, const SignedClause& boundClause={}, bool external=false);
    AtomID createAbstractAtom(const ITopologyPtr& topology, const wchar_t* name=nullptr, int domainSize=1, bool external=false);
    
    const ConstraintSolver& getSolver() const { return m_solver; }
    ConstraintSolver& getSolver() { return m_solver; }

    void addRule(const AtomLiteral& head, bool isChoice, const vector<AtomLiteral>& body, const ITopologyPtr& topology=nullptr);

    bool finalize();
    bool isTight() const { return m_isTight; }

    int getNumAtoms() const { return m_atoms.size(); }
    const AtomInfo* getAtom(AtomID id) const { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }
    AtomInfo* getAtom(AtomID id) { vxy_assert(id.isValid()); return m_atoms[id.value].get(); }

    AtomID getTrueAtom();

    int getNumBodies() const { return m_bodies.size(); }
    const BodyInfo* getBody(int32_t id) const { return m_bodies[id].get(); }

    VarID getVariableForBody(const AbstractBodyInfo& body, const vector<int>& headArguments);

    static bool getConcreteArgumentsForRelation(const AbstractAtomRelationInfoPtr& relationInfo, int vertex, vector<int>& outArgs);

protected:
    wstring literalToString(const AtomLiteral& lit) const;
    wstring literalsToString(const vector<AtomLiteral>& lits, bool cullKnown=true) const;
    
    void setConflicted();
    void lockVariableCreation();
    
    // IVariableDomainProvider
    virtual const SolverVariableDomain& getDomain(VarID varID) const override;

    struct BodyHasher
    {
        bool operator()(const BodyInfo* lhs, const BodyInfo* rhs) const
        {
            return compareBodies(lhs->atomLits, rhs->atomLits);
        }
        size_t operator()(const BodyInfo* lhs) const
        {
            return hashBody(lhs->atomLits);
        }

        bool operator()(const vector<AtomLiteral>& lhs, const vector<AtomLiteral>& rhs) const
        {
            return compareBodies(lhs, rhs);
        }
        size_t operator()(const vector<AtomLiteral>& lhs) const
        {
            return hashBody(lhs);
        }
        
        static bool compareBodies(const vector<AtomLiteral>& lhs, const vector<AtomLiteral>& rhs);
        static int32_t hashBody(const vector<AtomLiteral>& body);
    };

    class NogoodBuilder
    {
    public:
        void clear() { m_clauses.clear(); m_topologies.clear(); }
        void reserve(int n) { m_clauses.reserve(n); }
        bool empty() const { return m_clauses.empty(); }
        void add(const AClause& lit, bool required, const ITopologyPtr& topology);
        void emit(RuleDatabase& rdb, const IGraphRelationPtr<bool>& filter);

    protected:
        vector<pair<AClause,bool/*required*/>> m_clauses;
        vector<ITopologyPtr> m_topologies;
    };
    
    using BodySet = hash_set<BodyInfo*, BodyHasher>;

    struct GroundingData
    {
        GroundingData(int numOldAtoms, int numOldBodies)
        {
            bodyMappings.resize(numOldBodies);
        }

        vector<vector<int32_t>> bodyMappings;
    };

    static bool isConcreteLiteral(const AClause& lit);
    
    bool setAtomLiteralStatus(AtomID atom, const ValueSet& mask, ETruthStatus status);    
    bool setBodyStatus(ConcreteBodyInfo* body, ETruthStatus status);
    
    bool propagateFacts();
    bool isLiteralAssumed(AtomID atomID, bool sign, const ValueSet& mask) const;

    bool emptyAtomQueue();
    bool emptyBodyQueue();

    BodyInfo* findOrCreateBodyInfo(const vector<AtomLiteral>& body, const ITopologyPtr& topology, const AbstractAtomRelationInfoPtr& headRelationInfo, bool forceAbstract);
    BodyInfo* findBodyInfo(const vector<AtomLiteral>& body, const AbstractAtomRelationInfoPtr& headRelationInfo, size_t& outHash) const;

    template<typename T>
    void tarjanVisit(int node, T&& visitor);
    void computeSCCs();

    void makeConcrete();
    void groundBodyToConcrete(BodyInfo& oldBody, GroundingData& groundingData);
    void groundAtomToConcrete(const AtomLiteral& oldAtom, GroundingData& groundingData, ConcreteBodyInfo* concreteBody, bool isChoiceAtom);
    bool groundLiteralToConcrete(int vertex, const AtomLiteral& oldLit, GroundingData& groundingData, AtomLiteral& outLit);
    void hookupGroundedDependencies(const vector<HeadInfo>& newHeads, ConcreteBodyInfo* newBodyInfo, GroundingData& groundingData);

    void linkHeadToBody(const AtomLiteral& headLit, bool isChoice, BodyInfo* body);
    void addAtomDependency(const AtomLiteral& bodyLit, BodyInfo* body);
    
    // Solver that owns us
    ConstraintSolver& m_solver;

    struct HashAClause
    {
        static size_t hashValues(const vector<int>& values)
        {
            int out = 0;
            for (int value : values)
            {
                out = combineHashes(out, value);
            }
            return out;
        }
        
        size_t operator()(const tuple<AClause, ITopologyPtr>& aClause) const
        {
            if (auto signedClause = get_if<SignedClause>(&get<AClause>(aClause)))
            {
                return combineHashes(eastl::hash<VarID>()(signedClause->variable), hashValues(signedClause->values));
            }
            else
            {
                auto& graphClause = get<GraphRelationClause>(get<AClause>(aClause));
                return combineHashes(graphClause.variable->hash(), hashValues(graphClause.values));
            }            
        }
    };

    struct CompareAClause
    {
        bool operator()(const tuple<AClause, ITopologyPtr>& lhs, const tuple<AClause, ITopologyPtr>& rhs) const
        {
            if (get<AClause>(lhs).index() != get<AClause>(rhs).index())
            {
                return false;
            }

            if (get<ITopologyPtr>(lhs) != get<ITopologyPtr>(rhs))
            {
                return false;
            }

            auto& lhsLit = get<AClause>(lhs);
            auto& rhsLit = get<AClause>(rhs);

            return visit([&](auto&& typedLeft)
            {
                using Type = decay_t<decltype(typedLeft)>;
                if constexpr (is_same_v<Type, SignedClause>)
                {
                    return typedLeft == get<SignedClause>(rhsLit);
                }
                else
                {
                    return typedLeft.variable->equals(*get<GraphRelationClause>(rhsLit).variable) &&
                        typedLeft.values == get<GraphRelationClause>(rhsLit).values;
                }
            }, lhsLit);
        }
    };

    // Maps atoms to their corresponding boolean variable in the solver, and the literal they should be
    vector<unique_ptr<AtomInfo>> m_atoms;

    // Stored bodies.
    BodySet m_bodySet;
    vector<unique_ptr<BodyInfo>> m_bodies;
    hash_map<vector<AtomLiteral>, VarID, BodyHasher, BodyHasher> m_bodyVariables;

    // Whether any abstract heads or bodies exist.
    bool m_hasAbstract = false;

    vector<ConcreteAtomInfo*> m_atomsToPropagate;
    vector<ConcreteBodyInfo*> m_bodiesToPropagate;
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
    bool getForHead(const vector<int>& concreteHeadArgs, VarID& outVar);
    bool getForVertex(ITopology::VertexID vertex, bool allowCreation, VarID& outVar);
    const RuleDatabase::AbstractBodyInfo* getBodyInfo() const { return m_bodyInfo; }

    // Called when the RDB is destroyed, which happens before solving.
    void lockVariableCreation();

protected:
    RuleDatabase* m_rdb;
    AbstractAtomRelationInfoPtr m_headRelationInfo;
    const RuleDatabase::AbstractBodyInfo* m_bodyInfo;
    mutable hash_map<vector<int>, VarID, RuleDatabase::ArgumentHasher> m_bindMap;
    mutable vector<int> m_concrete;
};

// Given a specific head relation, maps to the corresponding body literal
class BoundBodyInstantiatorRelation : public IGraphRelation<VarID>
{
public:
    BoundBodyInstantiatorRelation(const wstring& name, const shared_ptr<AbstractBodyMapper>& mapper, const vector<GraphVertexRelationPtr>& headRelations);

    virtual bool getRelation(VertexID sourceVertex, VarID& out) const override;
    virtual size_t hash() const override;
    virtual bool equals(const IGraphRelation<VarID>& rhs) const override;
    virtual wstring toString() const override;

protected:    
    shared_ptr<AbstractBodyMapper> m_mapper;
    vector<GraphVertexRelationPtr> m_headRelations;
    wstring m_name;
    mutable vector<int> m_concrete;
};

// Maps a vertex to a body literal
class BodyInstantiatorRelation : public IGraphRelation<VarID>
{
public:
    BodyInstantiatorRelation(const shared_ptr<AbstractBodyMapper>& mapper, bool allowCreation);

    virtual bool getRelation(VertexID sourceVertex, VarID& out) const override;
    virtual size_t hash() const override;
    virtual bool equals(const IGraphRelation<VarID>& rhs) const override;
    virtual wstring toString() const override { return TEXT("BodyInstantiator"); }

protected:
    shared_ptr<AbstractBodyMapper> m_mapper;
    bool m_allowCreation;
};

// Excludes positive or negative facts from the relation
class FactGraphFilter : public IGraphRelation<bool>
{
    FactGraphFilter() {}
    
public:
    FactGraphFilter(const RuleDatabase::AbstractAtomInfo* atomInfo, const ValueSet& mask, const AbstractAtomRelationInfoPtr& relationInfo);
    FactGraphFilter(const RuleDatabase::AbstractBodyInfo* bodyInfo);

    virtual bool getRelation(VertexID sourceVertex, bool& out) const override;
    virtual size_t hash() const override;
    virtual bool equals(const IGraphRelation<bool>& rhs) const override;
    virtual wstring toString() const override { return TEXT("FactFilter"); }

    static FactGraphFilterPtr combine(const FactGraphFilterPtr& a, const FactGraphFilterPtr& b);
        
protected:
    ITopologyPtr m_topology;
    ValueSet m_filter;
};

} // namespace Vertexy
