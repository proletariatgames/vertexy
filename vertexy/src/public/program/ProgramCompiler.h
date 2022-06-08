// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "Program.h"
#include "program/ProgramAST.h"
#include "program/ExternalFormula.h"
#include "rules/RuleTypes.h"
#include "ConstraintSolver.h"
#include "rules/RuleDatabase.h"

namespace Vertexy
{

class RuleDatabase;
class DigraphTopology;

//
// Translates a formula with a set of arguments to its associated atom (and the solver's literal associated with that atom).
//
class FormulaMapper
{
public:
    enum CreationType
    {
        NeverCreate,
        AlwaysCreate,
        CreateIfBound
    };
    
    FormulaMapper(RuleDatabase& rdb, FormulaUID formulaUID, const wchar_t* formulaName, int domainSize, const ITopologyPtr& topology, BindCaller* binder);
    Literal getLiteral(const vector<ProgramSymbol>& concreteArgs, const ValueSet& mask, CreationType creationType) const;
    FormulaUID getFormulaUID() const { return m_formulaUID; }
    
    void setAtomID(AtomID id) { m_atomId = id; }
    AtomID getAtomID() const { return m_atomId; }

    bool hasBinder() const { return m_binder != nullptr; }

    void lockVariableCreation();
    
private:
    struct ArgumentHasher
    {
        size_t operator()(const vector<ProgramSymbol>& concreteArgs) const
        {
            size_t hash = 0;
            for (auto& arg : concreteArgs)
            {
                combineHashes(hash, arg.hash());
            }
            return hash;
        }
    };

    AtomID m_atomId;
    RuleDatabase* m_rdb;
    ConstraintSolver& m_solver;
    FormulaUID m_formulaUID;
    const wchar_t* m_formulaName;
    int m_domainSize;
    ITopologyPtr m_topology;
    BindCaller* m_binder = nullptr;

    using BindMap = hash_map<vector<ProgramSymbol>, VarID, ArgumentHasher>; 
    mutable BindMap m_bindMap;
};
using FormulaMapperPtr = shared_ptr<FormulaMapper>;

class AbstractAtomLiteralRelation : public IAtomGraphRelation
{
public:
    void setAtomID(AtomID atomID) { m_atomID = atomID; }
    AtomID getAtomID() const { return m_atomID; }
    void setRelationInfo(const AbstractAtomRelationInfoPtr& info) { m_relationInfo = info; }
    const AbstractAtomRelationInfoPtr& getRelationInfo() const { return m_relationInfo; }

    virtual bool needsInstantiation() const override { return false; }    
    virtual bool instantiateNecessary(int vertex, const ValueSet& atomMask, Literal& outLiteral) const override { return false; }
    virtual void lockVariableCreation() const override {}
    
protected:
    AbstractAtomRelationInfoPtr m_relationInfo;
    AtomID m_atomID;
};

using AbstractMapperRelationPtr = shared_ptr<AbstractAtomLiteralRelation>;


class FormulaGraphRelation : public AbstractAtomLiteralRelation
{
public:
    FormulaGraphRelation(const FormulaMapperPtr& bindMapper, const ProgramSymbol& symbol, bool headTerm);

    virtual bool getRelation(VertexID sourceVertex, Literal& out) const override;
    virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
    virtual size_t hash() const override;
    virtual wstring toString() const override;

    virtual bool needsInstantiation() const override;
    virtual bool instantiateNecessary(int vertex, const ValueSet& atomMask, Literal& outLiteral) const override;
    virtual void lockVariableCreation() const override;

private:
    bool makeConcrete(int vertex, vector<ProgramSymbol>& outConcrete) const;
    
    FormulaMapperPtr m_formulaMapper;
    ProgramSymbol m_symbol;
    bool m_isHeadTerm;

    mutable vector<ProgramSymbol> m_concrete;
};

//
// Translates the vertex->vertex relation created by an external formula into a vertex->Literal relation,
// where the returned literal is always true if the relation exists, and always false if the relation
// does not exist.
//
class ExternalFormulaGraphRelation : public AbstractAtomLiteralRelation
{
public:
    ExternalFormulaGraphRelation(const ProgramSymbol& symbol, const Literal& trueValue);

    virtual bool getRelation(VertexID sourceVertex, Literal& out) const override;
    virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
    virtual size_t hash() const override;
    virtual wstring toString() const override;

protected:
    ProgramSymbol m_symbol;
    Literal m_trueValue;
};

class HasRelationGraphRelation : public AbstractAtomLiteralRelation
{
public:
    HasRelationGraphRelation(const IGraphRelationPtr<VertexID>& relation, const Literal& trueValue);

    virtual bool getRelation(VertexID sourceVertex, Literal& out) const override;
    virtual bool equals(const IGraphRelation<Literal>& rhs) const override;
    virtual size_t hash() const override;
    virtual wstring toString() const override;

protected:
    IGraphRelationPtr<VertexID> m_relation;
    Literal m_trueValue;
};

//
// Responsible for removing all variables from rule statements, replacing them with all potential combinations.
// Outputs finalized rules to the RuleDatabase.
//
class ProgramCompiler
{
public:
    using BindMap = hash_map<FormulaUID, unique_ptr<BindCaller>>;

    struct AtomDomain
    {
        FormulaUID uid = FormulaUID(-1);

        bool containsAbstract = false;
        bool isExternal = false;
        ITopologyPtr abstractTopology = nullptr;

        hash_map<ProgramSymbol, int> map;
        vector<CompilerAtom> list;
    };
    using UAtomDomain = unique_ptr<AtomDomain>;

    struct RelationalRuleStatement
    {
        RuleStatement* statement = nullptr;
        ITopologyPtr topology;
    };

    ProgramCompiler(RuleDatabase& rdb, const BindMap& binders);
    ProgramCompiler(const ProgramCompiler& rhs) = delete;
    ProgramCompiler(ProgramCompiler&& rhs) noexcept = default;

    ProgramCompiler& operator=(const ProgramCompiler& rhs) = delete;

    static bool compile(RuleDatabase& rdb, const vector<RelationalRuleStatement>& statements, const BindMap& binders);

    const AtomDomain& getDomain(FormulaUID formula) const
    {
        static AtomDomain empty;
        auto found = m_groundedAtoms.find(formula);
        if (found != m_groundedAtoms.end())
        {
            return *found->second;
        }
        return empty;
    }

    bool hasAtom(const ProgramSymbol& sym) const
    {
        vxy_assert(!sym.isNegated());
        if (sym.isExternalFormula())
        {
            if (!sym.containsAbstract())
            {
                return sym.getExternalFormulaProvider()->eval(sym.getFormula()->args);
            }
            return true;
        }
        
        auto found = m_groundedAtoms.find(sym.getFormula()->uid);
        if (found != m_groundedAtoms.end())
        {
            return found->second->map.find(sym) != found->second->map.end();
        }
        return false;
    }
    
    void bindFactIfNeeded(const ProgramSymbol& sym, const ITopologyPtr& topology);

    bool hasFailure() const { return m_failure; }

    ConstraintSolver& getSolver() const { return m_rdb.getSolver(); }

protected:
    struct DepGraphNodeData
    {
        const RelationalRuleStatement* stmt = nullptr;
        bool marked = false;
        int vertex = 0;
        int outerSCCIndex = -1;
        int innerSCCIndex = -1;
    };

    struct Component
    {
        Component(vector<DepGraphNodeData*>&& stmts, int outerSCC, int innerSCC)
            : stmts(move(stmts))
            , outerSCC(outerSCC)
            , innerSCC(innerSCC)
        {
        }

        vector<DepGraphNodeData*> stmts;
        int outerSCC;
        int innerSCC;
    };

    struct VarNode
    {
        ProgramVariable variable;
        vector<int> provides;
        vector<int> boundBy;
        bool bound = false;
    };

    struct LitNode
    {
        vector<int> provides;
        vector<int> vars;
        int numDeps = 0;
        LiteralTerm* lit=nullptr;
    };

    struct GroundedRule
    {
        ERuleHeadType headType;
        vector<ProgramSymbol> heads;
        vector<ProgramSymbol> bodyLits;
        ITopologyPtr topology;
    };

    struct ExportMap
    {
        hash_map<ProgramSymbol, AtomID> concreteExports;
        hash_map<tuple<ProgramSymbol,bool/*ForHead*/>, AbstractMapperRelationPtr> abstractExports;
    };
    using UExportMap = unique_ptr<ExportMap>;

    void rewriteMath(const vector<RelationalRuleStatement>& statements);
    void createDependencyGraph(const vector<RelationalRuleStatement>& stmts);
    void createComponents(const vector<RelationalRuleStatement>& stmts);

    using AbstractOverrideMap = LiteralTerm::AbstractOverrideMap;
    
    void ground();
    void groundRule(DepGraphNodeData* statementNode);
    void instantiateRule(DepGraphNodeData* stmtNode, const VariableMap& varBindings, const vector<UInstantiator>& nodes, AbstractOverrideMap& parentMap, ProgramSymbol& parentBoundVertex, int cur=0);

    void addGroundedRule(const DepGraphNodeData* stmtNode, const RuleStatement* stmt, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const VariableMap& varBindings);
    bool addGroundedAtom(const CompilerAtom& atom, const ITopologyPtr& topology);

    void transformRules();
    void transformRule(GroundedRule&& rule);
    void transformChoice(GroundedRule&& rule);
    void transformDisjunction(GroundedRule&& rule);
    bool addTransformedRule(GroundedRule&& rule);
    
    void exportRules();
    AtomLiteral exportAtom(const ProgramSymbol& sym, const ITopologyPtr& topology, bool forHead);

    bool shouldExportAsAbstract(const GroundedRule& rule, bool& outContainsAbstracts) const;
    
    RuleDatabase& m_rdb;
    const BindMap& m_binders;

    shared_ptr<DigraphTopology> m_depGraph;
    TTopologyVertexData<DepGraphNodeData> m_depGraphData;

    vector<vector<FunctionTerm*>> m_edges;
    vector<Component> m_components;

    vector<GroundedRule> m_groundedRules;

    hash_map<FormulaUID, UAtomDomain> m_groundedAtoms;
    hash_map<FormulaUID, UExportMap> m_exportedLits;
    hash_map<FormulaUID, FormulaMapperPtr> m_exportedFormulas;
    hash_map<FormulaUID, FormulaUID> m_choiceFormulas;

    bool m_failure = false;
    bool m_foundRecursion = false;
};

};