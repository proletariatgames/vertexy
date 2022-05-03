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
protected:
    struct DepGraphNodeData
    {
        RuleStatement* statement;
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

public:
    struct AtomDomain
    {
        FormulaUID uid;
        hash_map<ProgramSymbol, int> map;
        vector<CompilerAtom> list;
    };
    using UAtomDomain = unique_ptr<AtomDomain>;

    ProgramCompiler(RuleDatabase& rdb)
        : m_rdb(rdb)
    {
    }

    void compile(ProgramInstance* instance);

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

protected:
    void rewriteMath();
    void createDependencyGraph(const vector<URuleStatement>& stmts);
    vector<Component> createComponents(const vector<URuleStatement>& stmts);
    void ground();
    void groundRule(DepGraphNodeData* statementNode);
    void instantiateRule(DepGraphNodeData* stmtNode, const VariableMap& varBindings, const vector<UInstantiator>& nodes, int cur=0);

    void emit(DepGraphNodeData* stmtNode, const VariableMap& varBindings);
    bool addAtom(const CompilerAtom& atom);

    RuleDatabase& m_rdb;
    ProgramInstance* m_instance = nullptr;
    shared_ptr<DigraphTopology> m_depGraph;
    TTopologyVertexData<DepGraphNodeData> m_depGraphData;

    vector<vector<FunctionTerm*>> m_edges;
    vector<Component> m_components;

    hash_map<FormulaUID, UAtomDomain> m_groundedAtoms;

    hash_map<FormulaUID, IExternalFormulaProviderPtr> m_externals;
    hash_map<ProgramSymbol, AtomID> m_createdAtomVars;
    bool m_failure = false;
    bool m_foundRecursion = false;
};

};