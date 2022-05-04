// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ConstraintTypes.h"
#include "Program.h"
#include "program/ProgramAST.h"
#include "rules/RuleTypes.h"

namespace Vertexy
{

class RuleDatabase;
class DigraphTopology;

class ProgramCompiler
{
public:
    using BindMap = hash_map<FormulaUID, BindCaller*>;

    struct AtomDomain
    {
        FormulaUID uid;
        hash_map<ProgramSymbol, int> map;
        vector<CompilerAtom> list;
    };
    using UAtomDomain = unique_ptr<AtomDomain>;

    struct RelationalRuleStatement
    {
        RuleStatement* statement;
        ITopologyPtr topology;
    };

    ProgramCompiler(RuleDatabase& rdb, const BindMap& binders);

    static bool compile(RuleDatabase& rdb, const vector<RelationalRuleStatement>& statements, const BindMap& binders);

    const AtomDomain& getDomain(FormulaUID formula) const
    {
        static AtomDomain empty;
        auto found = m_groundedAtoms.find(formula);
        if (found != m_groundedAtoms.end())
        {
            return *found->second.get();
        }
        return empty;
    }

    bool hasAtom(const ProgramSymbol& sym) const
    {
        vxy_assert(!sym.isNegated());
        auto found = m_groundedAtoms.find(sym.getFormula()->uid);
        if (found != m_groundedAtoms.end())
        {
            return found->second->map.find(sym) != found->second->map.end();
        }
        return false;
    }

    AtomLiteral exportAtom(const ProgramSymbol& sym, bool forHead=false);
    AnyGraphLiteralType exportGraphAtom(const ITopologyPtr& topology, const ProgramSymbol& sym, bool forHead=false);

    void bindFactIfNeeded(const ProgramSymbol& sym);

    bool hasFailure() const { return m_failure; }

protected:
    struct DepGraphNodeData
    {
        const RelationalRuleStatement* stmt;
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
        // vector<int> depends;
        vector<int> vars;
        int numDeps = 0;
        LiteralTerm* lit;
    };

    void rewriteMath(const vector<RelationalRuleStatement>& stmts);
    void createDependencyGraph(const vector<RelationalRuleStatement>& stmts);
    void createComponents(const vector<RelationalRuleStatement>& stmts);

    void ground();
    void groundRule(DepGraphNodeData* statementNode);
    void instantiateRule(DepGraphNodeData* stmtNode, const VariableMap& varBindings, const vector<UInstantiator>& nodes, int cur=0);

    void expandAndExportStatement(const DepGraphNodeData* stmtNode, const VariableMap& varBindings);
    void exportStatement(const DepGraphNodeData* stmtNode, const RuleStatement* statement, const VariableMap& varBindings);
    bool addGroundedAtom(const CompilerAtom& atom);

    RuleDatabase& m_rdb;
    const BindMap& m_binders;

    shared_ptr<DigraphTopology> m_depGraph;
    TTopologyVertexData<DepGraphNodeData> m_depGraphData;

    vector<vector<FunctionTerm*>> m_edges;
    vector<Component> m_components;

    hash_map<FormulaUID, UAtomDomain> m_groundedAtoms;

    hash_map<ProgramSymbol, AtomLiteral> m_createdAtomVars;
    hash_map<ProgramSymbol, BoundGraphAtomLiteral> m_createdGraphAtomVars;

    bool m_failure = false;
    bool m_foundRecursion = false;
};

};