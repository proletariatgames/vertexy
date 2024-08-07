﻿// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramCompiler.h"

#include "ConstraintSolver.h"
#include "program/ProgramDSL.h"
#include "rules/RuleDatabase.h"
#include "topology/DigraphTopology.h"
#include "topology/ITopology.h"
#include "topology/TopologySearch.h"

using namespace Vertexy;
static constexpr bool LOG_RULE_INSTANTIATION = false;
static constexpr bool LOG_RULE_EXPORTS = false;
static constexpr bool LOG_MATH_REWRITE = false;
static constexpr bool LOG_VAR_INSTANTIATION = false;

hash_set<ConstantFormula*, ConstantFormula::Hash, ConstantFormula::Hash> ConstantFormula::s_lookup;
vector<unique_ptr<ConstantFormula>> ConstantFormula::s_formulas;

bool ProgramCompiler::compile(RuleDatabase& rdb, const vector<RelationalRuleStatement>& statements, const BindMap& binders)
{
    ProgramCompiler compiler(rdb, binders);
    compiler.rewriteMath(statements);

    compiler.createDependencyGraph(statements);
    compiler.createComponents(statements);

    compiler.ground();
    if (compiler.hasFailure())
    {
        return false;
    }

    compiler.transformRules();
    compiler.exportRules();
    
    return !compiler.hasFailure();
}

ProgramCompiler::ProgramCompiler(RuleDatabase& rdb, const BindMap& binders)
    : m_rdb(rdb)
    , m_binders(binders)
{
}

//
// Rewrite all internal math terms to put them outside of any functions, and on the right hand side of relational terms
// e.g.
//
// A(X+1) <<= B(X)
//  --> A(__M0) <<= B(X) && __M0 == X+1
//
// A(Y) <<= B(X) && C(X+1 == Y-1)
//  --> A(Y) <<= B(X) && C(__M0 == __M1) && __M0 == X+1 && __M1 == Y-1
//
void ProgramCompiler::rewriteMath(const vector<RelationalRuleStatement>& statements)
{
    enum class BinOpType
    {
        Math,
        Relational,
        Equality
    };

    auto getBinOpType = [](const BinaryOpTerm* term)
    {
        switch (term->op)
        {
        case EBinaryOperatorType::Add:
        case EBinaryOperatorType::Subtract:
        case EBinaryOperatorType::Divide:
        case EBinaryOperatorType::Multiply:
            return BinOpType::Math;

        case EBinaryOperatorType::Equality:
            return BinOpType::Equality;

        case EBinaryOperatorType::Inequality:
        case EBinaryOperatorType::LessThan:
        case EBinaryOperatorType::LessThanEq:
        case EBinaryOperatorType::GreaterThan:
        case EBinaryOperatorType::GreaterThanEq:
            return BinOpType::Relational;
        default:
            vxy_fail_msg("unexpected binary operator");
            return BinOpType::Math;
        }
    };

    for (auto& stmt : statements)
    {
        hash_map<const BinaryOpTerm*, ProgramWildcard, pointer_value_hash<BinaryOpTerm, call_hash>, pointer_value_equality> replacements;
        hash_map<ProgramWildcard, UBinaryOpTerm> assignments;

        stmt.statement->replace<BinaryOpTerm>([&](BinaryOpTerm* opTerm) -> UTerm
        {
            if (getBinOpType(opTerm) != BinOpType::Math)
            {
                return nullptr;
            }
            
            ProgramSymbol eff = opTerm->eval(AbstractOverrideMap{}, {});
            if (eff.isValid())
            {
                return make_unique<SymbolTerm>(ProgramSymbol(eff));
            }

            ProgramSymbol left = opTerm->lhs->eval(AbstractOverrideMap{}, {});
            ProgramSymbol right = opTerm->rhs->eval(AbstractOverrideMap{}, {});

            if (left.isInteger())
            {
                switch (opTerm->op)
                {
                case EBinaryOperatorType::Add:
                    return make_unique<LinearTerm>(move(opTerm->rhs), left.getInt(), 1);
                case EBinaryOperatorType::Subtract:
                    return make_unique<LinearTerm>(move(opTerm->rhs), -left.getInt(), 1);
                case EBinaryOperatorType::Multiply:
                    return make_unique<LinearTerm>(move(opTerm->rhs), 0, left.getInt());
                }
            }
            else if (right.isInteger())
            {
                switch (opTerm->op)
                {
                case EBinaryOperatorType::Add:
                    return make_unique<LinearTerm>(move(opTerm->lhs), right.getInt(), 1);
                case EBinaryOperatorType::Subtract:
                    return make_unique<LinearTerm>(move(opTerm->lhs), -right.getInt(), 1);
                case EBinaryOperatorType::Multiply:
                    return make_unique<LinearTerm>(move(opTerm->lhs), 0, right.getInt());
                }
            }
            
            return nullptr; 
        });

        int synthVariableCount = 0;
        stmt.statement->visit<Term>([&](const Term* term)
        {
            if (auto binOpTerm = dynamic_cast<const BinaryOpTerm*>(term))
            {
                if (getBinOpType(binOpTerm) == BinOpType::Math)
                {
                    auto insertionPoint = replacements.find(binOpTerm);
                    if (insertionPoint == replacements.end())
                    {
                        wstring name{wstring::CtorSprintf(), TEXT("__M%d"), synthVariableCount++};
                        ProgramWildcard newVar(name.c_str());

                        auto clone = UBinaryOpTerm(move(static_cast<BinaryOpTerm*>(binOpTerm->clone().detach())));

                        insertionPoint = replacements.insert({clone.get(), newVar}).first;
                        assignments[insertionPoint->second] = move(clone);
                    }
                }
            }
        });

        if (!replacements.empty())
        {
            wstring before = stmt.statement->toString();

            stmt.statement->replace<BinaryOpTerm>([&](const BinaryOpTerm* term) -> UTerm
            {
                auto found = replacements.find(term);
                if (found != replacements.end())
                {
                    return make_unique<WildcardTerm>(found->second);
                }
                return nullptr;
            });

            for (auto& assignment : assignments)
            {
                auto lhs = make_unique<WildcardTerm>(assignment.first);
                auto assignmentTerm = make_unique<BinaryOpTerm>(
                    EBinaryOperatorType::Equality,
                    move(lhs),
                    move(assignment.second)
                );
                stmt.statement->body.push_back(move(assignmentTerm));
            }

            if constexpr (LOG_MATH_REWRITE)
            {
                VERTEXY_LOG("Rewrote:\n  %s\n  %s", before.c_str(), stmt.statement->toString().c_str());
            }
        }
    }
}

// Create the dependency graph, where each graph edge points from a formula head to a formula body that contains that
// head. The strongly connected components are cyclical dependencies between rules, which need to be handled specially.
void ProgramCompiler::createDependencyGraph(const vector<RelationalRuleStatement>& stmts)
{
    m_depGraph = make_shared<DigraphTopology>();
    m_depGraph->reset(stmts.size());

    m_depGraphData.initialize(ITopology::adapt(m_depGraph));

    m_edges.clear();
    m_edges.resize(m_depGraph->getNumVertices());

    // Build a graph, where each node is a Statement.
    // Create edges between Statements where a rule head points toward the bodies those heads appear in.
    for (int vertex = 0; vertex < m_depGraph->getNumVertices(); ++vertex)
    {
        auto& stmt = stmts[vertex];

        m_depGraphData.get(vertex).stmt = &stmt;
        m_depGraphData.get(vertex).vertex = vertex;

        stmt.statement->visitHead<FunctionHeadTerm>([&](const FunctionHeadTerm* headTerm)
        {
            for (int otherVertex = 0; otherVertex < m_depGraph->getNumVertices(); ++otherVertex)
            {
                auto& otherStmt = stmts[otherVertex];
                otherStmt.statement->visitBody<FunctionTerm>([&](const FunctionTerm* bodyTerm)
                {
                    if (headTerm->functionUID == bodyTerm->functionUID && !m_depGraph->hasEdge(vertex, otherVertex))
                    {
                        m_edges[vertex].push_back(const_cast<FunctionTerm*>(bodyTerm));
                        m_depGraph->addEdge(vertex, otherVertex);
                        vxy_sanity(m_edges[vertex].size() == m_depGraph->getNumOutgoing(vertex));
                    }
                });
            }
        });
    }
}

// Builds the set of components, where each component is a SCC of the dependency graph of positive literals.
//
// OUTPUT: an array of components (set or rules), ordered by inverse topological sort. I.e., all statements in
// each component can be reified entirely by components later in the list.
void ProgramCompiler::createComponents(const vector<RelationalRuleStatement>& stmts)
{
    m_components.clear();

    const int abstractSourceVertex = stmts.size();

    // Grab all the outer SCCs. They will be in reverse topographical order.
    vector<vector<int>> outerSCCs;
    TopologySearchAlgorithm::findStronglyConnectedComponents(*m_depGraph, [&](int, auto it)
    {
        outerSCCs.emplace_back();
        for (; it; ++it)
        {
            outerSCCs.back().push_back(*it);
        }

        // skip the imaginary "abstract source" statement
        if (outerSCCs.back()[0] == abstractSourceVertex)
        {
            vxy_assert(outerSCCs.back().size() == 1);
            outerSCCs.pop_back();
        }
    });

    for (int i = outerSCCs.size()-1, outerSCCIndex = 0; i >= 0; i--, outerSCCIndex++)
    {
        auto& curOuterSCC = outerSCCs[i];
        for (int j : curOuterSCC)
        {
            m_depGraphData.get(j).outerSCCIndex = outerSCCIndex;
        }
    }

    vector<int> statementToSCC;

    // Visit each outer SCC in topographical order (each SCC only depends on previously processed SCCs)
    for (int i = outerSCCs.size()-1, outerSCCIndex = 0; i >= 0; i--, outerSCCIndex++)
    {
        DigraphTopology positiveGraph;

        auto& curOuterSCC = outerSCCs[i];

        statementToSCC.resize(m_depGraph->getNumVertices());
        for (int j = 0; j < curOuterSCC.size(); ++j)
        {
            statementToSCC[curOuterSCC[j]] = j;
            vxy_assert(curOuterSCC[j] != abstractSourceVertex);
        }

        positiveGraph.reset(curOuterSCC.size());

        //
        // Build the graph within this SCC of only positive dependencies.
        //

        for (int vertex : curOuterSCC)
        {
            // for each literal depending on this statement..
            for (int edgeIdx = 0; edgeIdx < m_depGraph->getNumOutgoing(vertex); ++edgeIdx)
            {
                int destVertex;
                m_depGraph->getOutgoingDestination(vertex, edgeIdx, destVertex);

                if (destVertex == vertex)
                {
                    continue;
                }

                if (m_depGraphData.get(destVertex).outerSCCIndex == outerSCCIndex && !m_edges[vertex][edgeIdx]->negated)
                {
                    positiveGraph.addEdge(statementToSCC[vertex], statementToSCC[destVertex]);
                }
            }
        }

        //
        // From the positive dependency graph of this SCC, determine the inner SCCs.
        //

        vector<vector<int>> posSCCs;
        TopologySearchAlgorithm::findStronglyConnectedComponents(positiveGraph, [&](int, auto itPos)
        {
            posSCCs.push_back();
            for (; itPos; ++itPos)
            {
                posSCCs.back().push_back(curOuterSCC[*itPos]);
            }
        });

        // Assign the inner SCC index for each statement of each positive SCC
        // Go backward, since this is in reverse topographical order
        for (int j = posSCCs.size()-1, innerSCCIndex = 0; j >= 0; --j, ++innerSCCIndex)
        {
            for (int vertex : posSCCs[j])
            {
                m_depGraphData.get(vertex).innerSCCIndex = innerSCCIndex;
            }
        }

        //
        // Write out rule statements and mark any recursive literals
        //

        for (int j = posSCCs.size()-1, innerSCCIndex = 0; j >= 0; --j, ++innerSCCIndex)
        {
            auto& posSCC = posSCCs[j];

            vector<DepGraphNodeData*> componentNodes;
            componentNodes.reserve(posSCC.size());

            for (int vertex : posSCC)
            {
                vxy_sanity(m_depGraphData.get(vertex).stmt == &stmts[vertex]);
                componentNodes.push_back(&m_depGraphData.get(vertex));

                // If any literals in the head of this statement appear in earlier components,
                // then mark those literals as recursive.
                int numDeps = m_depGraph->getNumOutgoing(vertex);
                for (int edgeIdx = 0; edgeIdx < numDeps; ++edgeIdx)
                {
                    int depVertex;
                    m_depGraph->getOutgoingDestination(vertex, edgeIdx, depVertex);

                    auto& depNode = m_depGraphData.get(depVertex);
                    if (depNode.outerSCCIndex < outerSCCIndex ||
                        (depNode.outerSCCIndex == outerSCCIndex && depNode.innerSCCIndex <= innerSCCIndex))
                    {
                        m_edges[vertex][edgeIdx]->recursive = true;
                    }
                }
            }

            m_components.emplace_back(move(componentNodes), outerSCCIndex, innerSCCIndex);
        }
    }
}

void ProgramCompiler::ground()
{
    for (auto& component : m_components)
    {
        for (DepGraphNodeData* stmtNode : component.stmts)
        {
            stmtNode->marked = true;
        }

        // create rules out of this component (which may be self-recursive) until fixpoint.
        do
        {
            m_foundRecursion = false;
            for (DepGraphNodeData* stmtNode : component.stmts)
            {
                if (stmtNode->marked)
                {
                    stmtNode->marked = false;
                    groundRule(stmtNode);
                }
            }
        }
        while (m_foundRecursion);
    }
}

void ProgramCompiler::groundRule(DepGraphNodeData* statementNode)
{
    vector<LitNode> litNodes;
    vector<VarNode> varNodes;
    vector<pair<WildcardUID, vector<int>>> boundBy;

    const RelationalRuleStatement* stmt = statementNode->stmt;

    //
    // Build dependency graph of variables found in the body.
    // Literals that are non-negative FunctionTerms provide support; everything else relies on support.
    //
    hash_map<WildcardUID, size_t> seen;
    for (auto& bodyLit : stmt->statement->body)
    {
        litNodes.emplace_back();
        litNodes.back().lit = bodyLit.get();
        litNodes.back().numDeps = 0;

        vector<tuple<WildcardTerm*, bool/*canEstablish*/>> varTerms;

        bodyLit->collectWildcards(varTerms);
        for (auto varTerm : varTerms)
        {
            ProgramWildcard var = get<0>(varTerm)->wildcard;

            // create a VarNode if we haven't made one already.
            auto found = seen.find(var.getID());
            if (found == seen.end())
            {
                found = seen.insert({var.getID(), varNodes.size()}).first;
                varNodes.emplace_back();
                varNodes.back().wildcard = var;

                boundBy.emplace_back(var.getID(), vector<int>{});
            }

            // can this term provide the variable?
            if (get<1>(varTerm))
            {
                // edge from literal -> variable
                litNodes.back().provides.push_back(found->second);
            }
            else
            {
                // this term needs the variable from somewhere else in the body.
                // edge from variable -> literal
                varNodes[found->second].provides.push_back(litNodes.size()-1);
                litNodes.back().numDeps++;
            }
            // add index of varNode to list of all variables in this lit.
            litNodes.back().wildcards.push_back(found->second);
        }
    }

    // Start with the literals that have no variable dependencies.
    vector<LitNode*> openLits;
    for (auto& litNode : litNodes)
    {
        if (litNode.numDeps == 0)
        {
            openLits.push_back(&litNode);
        }
    }

    WildcardMap bound;
    vector<UInstantiator> instantiators;

    //
    // Terms will bind variables in the following precedence:
    // 1) Constant terms are always bound first
    // 2) Terms that contain a Vertex literal are next.
    // 3) External terms that can bind any unbound variables are next.
    // 4) Terms that contain a bound variable are next.
    // 5) Finally, precedence follows right-to-left order.
    // 
    auto shouldTakePrecedence = [&](const LitNode* testLit, const LitNode* checkLit)
    {
        auto testLitSymTerm = dynamic_cast<const SymbolTerm*>(testLit->lit);
        auto checkLitSymTerm = dynamic_cast<const SymbolTerm*>(checkLit->lit);
        if (testLitSymTerm && !checkLitSymTerm)
        {
            return true;
        }
        else if (!testLitSymTerm && checkLitSymTerm)
        {
            return false;
        }
        
        bool testHasVertex = testLit->lit->contains<VertexTerm>();
        bool checkHasVertex = checkLit->lit->contains<VertexTerm>();
        if (testHasVertex && !checkHasVertex)
        {
            return true;
        }
        else if (!testHasVertex && checkHasVertex)
        {
            return false;
        }
        
        auto testLitFnTerm = dynamic_cast<const FunctionTerm*>(testLit->lit);
        auto checkLitFnTerm = dynamic_cast<const FunctionTerm*>(checkLit->lit);
        
        bool testIsExternal = testLitFnTerm != nullptr && testLitFnTerm->provider != nullptr;
        bool checkIsExternal = checkLitFnTerm != nullptr && checkLitFnTerm->provider != nullptr;
        if (testIsExternal && !checkIsExternal)
        {
            return true;
        }
        else if (!testIsExternal && checkIsExternal)
        {
            return false;
        }

        auto isBound = [&](auto&& varTerm) { return bound.find(varTerm->wildcard) != bound.end(); };
        bool testAllUnbound = !testLit->lit->contains<WildcardTerm>(isBound);
        bool checkAllUnbound = !checkLit->lit->contains<WildcardTerm>(isBound);
        if (!testAllUnbound && checkAllUnbound)
        {
            return true;
        }
        else if (testAllUnbound && !checkAllUnbound)
        {
            return false;
        }

        return false;
    };

    bool canBeAbstract = stmt->statement->head == nullptr || stmt->statement->headContains<VertexTerm>();
    
    // go through each literal in dependency order.
    while (!openLits.empty())
    {
        // Ensure precedence is satisfied.
        for (auto it = openLits.begin(), itEnd = openLits.end()-1; it != itEnd; ++it)
        {
            if (shouldTakePrecedence(*it, openLits.back()))
            {
                swap(*it, openLits.back());
            }
        }
        
        LitNode* litNode = openLits.back();
        openLits.pop_back();

        // for each variable in this literal that hasn't been bound yet, mark the first VariableTerm it appears in
        // as being the variable provider. This will create the shared ProgramSymbol that all occurrences
        // of this variable within this body will point to.
        litNode->lit->createWildcardReps(bound);

        instantiators.push_back(litNode->lit->instantiate(*this, canBeAbstract, statementNode->stmt->topology));

        // reduce the dependency count of literals waiting on variables to be provided.
        // if there are no more dependencies, add them to the open list.
        for (int varIndex : litNode->provides)
        {
            VarNode& varNode = varNodes[varIndex];
            if (!varNode.bound)
            {
                varNode.bound = true;
                for (int dep : varNode.provides)
                {
                    vxy_assert(litNodes[dep].numDeps > 0);
                    litNodes[dep].numDeps--;
                    if (litNodes[dep].numDeps == 0)
                    {
                        openLits.push_back(&litNodes[dep]);
                    }
                }
            }
        }
    }

    // Assign all variables/vertices appearing in head symbols to their shared binders. 
    if (stmt->statement->head != nullptr)
    {
        vector<tuple<WildcardTerm*, bool>> vars;
        stmt->statement->head->collectWildcards(vars, false);

        for (const tuple<WildcardTerm*, bool>& tuple : vars)
        {
            auto varTerm = get<WildcardTerm*>(tuple);
            auto found = bound.find(varTerm->wildcard);
            vxy_assert_msg(found != bound.end(), "variable appears in head but not body?");
            varTerm->sharedBoundRef = found->second;
        }
    }

    vxy_assert_msg(instantiators.size() == litNodes.size(), "could not instantiate. unsafe vars?");

    // now instantiate!
    if (LOG_RULE_INSTANTIATION)
    {
        VERTEXY_LOG("Instantiating %s", statementNode->stmt->statement->toString().c_str());
    }

    AbstractOverrideMap overrideMap;
    ProgramSymbol boundVertex;
    instantiateRule(statementNode, bound, instantiators, overrideMap, boundVertex);
}

void ProgramCompiler::instantiateRule(DepGraphNodeData* stmtNode, const WildcardMap& varBindings, const vector<UInstantiator>& nodes, AbstractOverrideMap& parentMap, ProgramSymbol& parentBoundVertex, int cur)
{
    if (cur == nodes.size())
    {
        addGroundedRule(stmtNode, stmtNode->stmt->statement, parentMap, parentBoundVertex, varBindings);
    }
    else
    {
        auto& inst = nodes[cur];
        AbstractOverrideMap thisMap = parentMap;
        ProgramSymbol boundVertex = parentBoundVertex;
        for (inst->first(thisMap, boundVertex); !inst->hitEnd(); inst->match(thisMap, boundVertex))
        {
            instantiateRule(stmtNode, varBindings, nodes, thisMap, boundVertex, cur+1);
            thisMap = parentMap;
            boundVertex = parentBoundVertex;
        }
    }
}

void ProgramCompiler::addGroundedRule(const DepGraphNodeData* stmtNode, const RuleStatement* stmt, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const WildcardMap& varBindings)
{
    if (stmt->head != nullptr && stmt->head->mustBeConcrete(overrideMap, boundVertex))
    {
        vxy_assert(!boundVertex.isValid());
        for (int vertex = 0, numVerts = stmtNode->stmt->topology->getNumVertices(); vertex < numVerts; ++vertex)
        {
            addGroundedRule(stmtNode, stmt, overrideMap, ProgramSymbol(vertex), varBindings);
        }
        return;
    }
    
    vector<ProgramSymbol> bodyTerms;
    for (auto& bodyTerm : stmt->body)
    {
        ProgramSymbol bodySym = bodyTerm->eval(overrideMap, boundVertex);
        if (!bodySym.isValid())
        {
            return;
        }
        
        if (bodySym.isFormula())
        {
            auto fnTerm = dynamic_cast<FunctionTerm*>(bodyTerm.get());
            vxy_assert_msg(fnTerm != nullptr, "not a function, but got a function symbol?");
            vxy_assert(fnTerm->negated == bodySym.isNegated());

            if (fnTerm->negated && !fnTerm->recursive && !bodySym.containsAbstract() &&
                !hasAtom(bodySym.negatedFormula()))
            {
                // can't possibly be true, so no need to include.
                continue;
            }

            // Only non-fact atoms need to be included in the rule body
            if (!fnTerm->assignedToFact)
            {
                bodyTerms.push_back(bodySym);
            }

            // Ensure external formula terms hold. We need to do this now because it might've originally
            // been bound to an abstract that got narrowed later on in the matching process.
            if (bodySym.isExternalFormula() && !bodySym.containsAbstract())
            {
                if (bodySym.isNegated() == bodySym.getExternalFormulaProvider()->eval(bodySym.getFormula()->args))
                {
                    return;
                }
            }

            if (bodySym.containsAbstract())
            {
                if (bodySym.isExternalFormula())
                {
                    // Add this to the grounded database; we'll need it when exporting rules.
                    addGroundedAtom(CompilerAtom{bodySym.absolute(), ValueSet(bodySym.getFormula()->mask.size(),false)}, stmtNode->stmt->topology);
                }

                auto& domain = m_groundedAtoms[bodySym.getFormula()->uid]; 
                vxy_assert(domain->abstractTopology == nullptr || domain->abstractTopology == stmtNode->stmt->topology);
                domain->abstractTopology = stmtNode->stmt->topology; 
                domain->containsAbstract = true;
            }
        }
        else
        {
            vxy_assert(bodySym.isAbstract() || bodySym.isInteger());
            vxy_assert(!bodySym.isInteger() || bodySym.getInt() > 0);
            if (bodySym.isAbstract())
            {
                // TODO: Equality terms between two identical abstracts could be discarded here.
                bodyTerms.push_back(bodySym);
            }
        }
    }

    if (stmt->head == nullptr && bodyTerms.empty())
    {
        VERTEXY_LOG("Failed during grounding: disallow() is impossible to satisfy: %s", stmt->toString().c_str());
        m_failure = true;

        return;
    }

    //
    // Remove any heads that are already established as facts.
    //

    bool isNormalRule = true;
    
    vector<ProgramSymbol> headSymbols;
    if (stmt->head != nullptr)
    {
        headSymbols = stmt->head->eval(overrideMap, boundVertex, isNormalRule);
        vxy_assert(!isNormalRule || headSymbols.size() == 1);

        int j = 0;
        for (int i = 0; i < headSymbols.size(); ++i)
        {
            if (isAtomFact(headSymbols[i]))
            {
                // if one of the atoms in the disjunction is true, that means the rest cannot be true.
                if (stmt->head->getHeadType() == ERuleHeadType::Disjunction)
                {
                    return;
                }
                // Otherwise, if this is already a fact, no need to include.
                continue;
            }
            headSymbols[j++] = headSymbols[i];
        }
        headSymbols.resize(j);
        
        if (headSymbols.empty())
        {
            // If all heads are facts, no need to include this statement.
            return;
        }
    }

    //
    // Add all the head symbols to the grounded database, and mark all the rules that contain these heads
    // in the body to be (re)grounded.
    //

    bool areFacts = isNormalRule && bodyTerms.empty();
    for (const auto& headSym : headSymbols)
    {
        auto& headSymMask = headSym.getFormula()->mask;
        auto factMask = areFacts ? headSymMask : ValueSet(headSymMask.size(), false);
        if (addGroundedAtom(CompilerAtom{headSym, factMask}, stmtNode->stmt->topology))
        {
            int numEdges = m_depGraph->getNumOutgoing(stmtNode->vertex);
            for (int edgeIdx = 0; edgeIdx < numEdges; ++edgeIdx)
            {
                int destVertex;
                m_depGraph->getOutgoingDestination(stmtNode->vertex, edgeIdx, destVertex);

                DepGraphNodeData& destNode = m_depGraphData.get(destVertex);
                destNode.marked = true;

                // If this is part of the same component we need to re-process the component, because
                // new potential heads have been discovered.
                // TODO: Currently we reprocess every atom in the domain, instead of only new atoms, causing
                // duplicate clauses to be generated.
                if (destNode.outerSCCIndex == stmtNode->outerSCCIndex && destNode.innerSCCIndex == stmtNode->innerSCCIndex)
                {
                    m_foundRecursion = true;
                }
            }
        }
    }
    
    if (LOG_RULE_INSTANTIATION)
    {
        auto toString = [&]()
        {
            wstring out;
            for (int i = 0; i < headSymbols.size(); ++i)
            {
                if (i > 0) out.append(TEXT(", "));
                out.append(headSymbols[i].toString());
            }
        
            if (!bodyTerms.empty())
            {
                out.append(TEXT(" <- "));
        
                for (int i = 0; i < bodyTerms.size(); ++i)
                {
                    auto& bodyTerm = bodyTerms[i];
                    if (i > 0)
                    {
                        out.append(TEXT(", "));
                    }
                    out.append(bodyTerm.toString());
                }
            }
            return out;
        };
        VERTEXY_LOG("  Grounding %s", stmt->toString().c_str());
        VERTEXY_LOG("    :: %s", toString().c_str());
    }

    GroundedRule newRule{
        stmt->head != nullptr ? stmt->head->getHeadType() : ERuleHeadType::Normal,
        move(headSymbols),
        move(bodyTerms),
        stmtNode->stmt->topology
    };
    m_groundedRules.push_back(move(newRule));

    if (areFacts && stmt->head != nullptr)
    {
        stmt->head->bindAsFacts(*this, overrideMap, boundVertex, stmtNode->stmt->topology);
    }
}

bool ProgramCompiler::isAtomFact(const ProgramSymbol& sym) const
{
    auto foundDomain = m_groundedAtoms.find(sym.getFormula()->uid);
    if (foundDomain == m_groundedAtoms.end())
    {
        return false;
    }
    auto& domain = foundDomain->second;
    auto found = domain->map.find(sym);
    if (found != domain->map.end())
    {
        auto& facts = domain->list[found->second].facts;
        if (!facts.isZero() && facts.isSubsetOf(sym.getFormula()->mask))
        {
            return true;
        }
    }
    return false;
}

void ProgramCompiler::exportRules()
{
    //
    // Create AtomIDs for each abstract symbol, and each grounded atom.
    // 
    for (auto& domainEntry : m_groundedAtoms)
    {
        FormulaUID formulaUID = domainEntry.first;
        AtomDomain* domain = domainEntry.second.get();
        const wchar_t* formulaName = domain->list[0].symbol.getFormula()->name.c_str();
        const int domainSize = domain->list[0].symbol.getFormula()->mask.size();

        vxy_sanity(m_exportedLits.find(formulaUID) == m_exportedLits.end());
        auto exportMapIt = m_exportedLits.insert({formulaUID, make_unique<ExportMap>()}).first;
        
        auto foundBinder = m_binders.find(formulaUID);
        if (domain->containsAbstract)
        {
            vxy_sanity(m_exportedFormulas.find(formulaUID) == m_exportedFormulas.end());
            auto mapper = make_unique<FormulaMapper>(
                m_rdb,
                formulaUID,
                formulaName,
                domainSize,
                domain->abstractTopology,
                foundBinder != m_binders.end() ? foundBinder->second.get() : nullptr
            );
           
            vxy_assert(domain->abstractTopology != nullptr);
            AtomID atomID = m_rdb.createAbstractAtom(domain->abstractTopology, formulaName, domainSize, domain->isExternal);
            mapper->setAtomID(atomID);

            m_exportedFormulas.insert({formulaUID, move(mapper)});
        }
        else
        {
            UExportMap& exportMap = exportMapIt->second;
            for (auto& atom : domain->list)
            {
                vxy_assert(!atom.symbol.isNegated());
                auto unmaskedSym = atom.symbol.unmasked();

                SignedClause equivalence;
                if (foundBinder != m_binders.end())
                {
                    equivalence.variable = foundBinder->second->call(m_rdb, unmaskedSym.getFormula()->args);
                    equivalence.values = foundBinder->second->getDomainMapping();
                }
                
                AtomID atomID = m_rdb.createAtom(unmaskedSym.toString().c_str(), domainSize, equivalence);
                AtomLiteral atomLit(atomID, true, atom.symbol.getFormula()->mask);                                            
                exportMap->concreteExports.insert({unmaskedSym, atomID});
            }
        }
    }

    //
    // Export the rules
    //

    auto toString = [&](const GroundedRule& rule)
    {
        wstring out;
        if (!rule.heads.empty())
        {
            out.append(rule.heads[0].toString());
        }
        
        if (!rule.body.empty())
        {
            out.append(TEXT(" <- "));
        
            for (int i = 0; i < rule.body.size(); ++i)
            {
                auto& bodyTerm = rule.body[i];
                if (i > 0)
                {
                    out.append(TEXT(", "));
                }
                out.append(bodyTerm.toString());
            }
        }
        return out;
    };

    for (auto& rule : m_groundedRules)
    {
        vxy_assert(rule.headType != ERuleHeadType::Disjunction);
        vxy_assert(rule.heads.size() <= 1);
        
        bool containsAbstracts;
        if (shouldExportAsAbstract(rule, containsAbstracts) || !containsAbstracts)
        {
            vector<AtomLiteral> headLiterals;
            headLiterals.reserve(rule.heads.size());

            AtomLiteral headLiteral;
            if (!rule.heads.empty())
            {
                vxy_assert(rule.heads.size() == 1); // multi-head rules should've already been transformed by now

                const ProgramSymbol& headSym = rule.heads[0];
                vxy_assert(headSym.isFormula());
                vxy_assert(headSym.isPositive());

                headLiteral = exportAtom(headSym, rule.topology, true);
            }
            
            vector<AtomLiteral> exportedBody;
            exportedBody.reserve(rule.body.size());
            for (auto& bodySym : rule.body)
            {
                if (bodySym.isExternalFormula() && !bodySym.containsAbstract())
                {
                    continue;
                }
                exportedBody.push_back(exportAtom(bodySym, rule.topology, false));
            }

            if (LOG_RULE_EXPORTS)
            {
                VERTEXY_LOG("Exporting %s", toString(rule).c_str());
            }
            m_rdb.addRule(headLiteral, rule.headType == ERuleHeadType::Choice, exportedBody, rule.topology);
        }
        else
        {
            vxy_fail_msg("NYI");
        }
    }
}

bool ProgramCompiler::shouldExportAsAbstract(const GroundedRule& rule, bool& outContainsAbstracts) const
{
    outContainsAbstracts = false;
    vxy_assert(rule.headType != ERuleHeadType::Disjunction);
    vxy_assert(rule.heads.size() <= 1);

    if (!rule.heads.empty())
    {
        for (auto& arg : rule.heads[0].getFormula()->args)
        {
            if (arg.containsAbstract())
            {
                outContainsAbstracts = true;
                break;
            }
        }

        if (outContainsAbstracts)
        {
            return true;
        }
    }

    // Head is empty or contains no abstracts
    for (auto& bodyLit : rule.body)
    {
        if (bodyLit.containsAbstract())
        {
            outContainsAbstracts = true;
            return true;
        }
    }

    // Neither head nor body contain abstracts
    return false;
}

AtomLiteral ProgramCompiler::exportAtom(const ProgramSymbol& symbol, const ITopologyPtr& topology, bool forHead)
{
    // Abstract symbols are for relation/equality terms
    if (symbol.isAbstract())
    {
        // TODO: hash/reuse these?
        auto relationInfo = make_shared<AbstractAtomRelationInfo>();
        relationInfo->filterRelation = make_shared<HasRelationGraphRelation>(symbol.getAbstractRelation());

        AtomID abstractID = m_rdb.createAbstractAtom(topology, relationInfo->filterRelation->toString().c_str(), 1, true);
        return AtomLiteral(abstractID, symbol.isPositive(), ValueSet(1, true), relationInfo);
    }

    // Handle concrete symbols
    auto domainIt = m_groundedAtoms.find(symbol.getFormula()->uid);
    if (domainIt != m_groundedAtoms.end() && !domainIt->second->containsAbstract)
    {
        vxy_sanity(!symbol.containsAbstract());
        
        AtomID atomID = m_exportedLits[symbol.getFormula()->uid]->concreteExports[symbol.absolute().unmasked()];
        vxy_assert(atomID.isValid());
        return AtomLiteral(atomID, symbol.isPositive(), symbol.getFormula()->mask);
    }

    vxy_sanity(symbol.isFormula());

    if (symbol.isExternalFormula())
    {
        vxy_sanity(symbol.containsAbstract());
        if (m_exportedLits.find(symbol.getFormula()->uid) == m_exportedLits.end())
        {
            m_exportedLits[symbol.getFormula()->uid] = make_unique<ExportMap>();
        }
    }

    ProgramSymbol absoluteUnmaskedSym = symbol.absolute().unmasked();
    
    // See if we already created a literal for this abstract formula term...
    auto& exportMap = m_exportedLits[symbol.getFormula()->uid]->abstractExports;
    auto foundExport = exportMap.find(make_tuple(absoluteUnmaskedSym, forHead));
    if (foundExport != exportMap.end())
    {
        return AtomLiteral(foundExport->second->getAtomID(), symbol.isPositive(), symbol.getFormula()->mask, foundExport->second->getRelationInfo());
    }

    //
    // Create a new literal for this abstract formula term.
    //
    
    auto relationInfo = make_shared<AbstractAtomRelationInfo>();
    relationInfo->argumentRelations.resize(symbol.getFormula()->args.size(), nullptr);
    for (int i = 0; i < symbol.getFormula()->args.size(); ++i)
    {
        auto& arg = symbol.getFormula()->args[i];
        if (arg.isAbstract())
        {
            relationInfo->argumentRelations[i] = arg.getAbstractRelation();
        }
        else
        {
            int constant = arg.getInt();
            relationInfo->argumentRelations[i] = make_shared<ConstantGraphRelation<int>>(constant);
        }
    }

    AbstractMapperRelationPtr litRelation;
    FormulaMapperPtr& formulaMapper = m_exportedFormulas[symbol.getFormula()->uid];
    if (symbol.isExternalFormula())
    {
        litRelation = make_shared<ExternalFormulaGraphRelation>(absoluteUnmaskedSym, SignedClause(m_rdb.getSolver().getTrue().variable, vector{1}));
    }
    else
    {
        vxy_assert(symbol.isNormalFormula());
        litRelation = make_shared<FormulaGraphRelation>(formulaMapper, absoluteUnmaskedSym, forHead);
    }
    relationInfo->literalRelation = litRelation;

    litRelation->setAtomID(formulaMapper->getAtomID());
    litRelation->setRelationInfo(relationInfo);
    exportMap[make_tuple(absoluteUnmaskedSym, forHead)] = litRelation;
    
    return AtomLiteral(formulaMapper->getAtomID(), symbol.isPositive(), symbol.getFormula()->mask, relationInfo);
}

void ProgramCompiler::bindFactIfNeeded(const ProgramSymbol& sym, const ITopologyPtr& topology)
{
    vxy_assert(!sym.isNegated());
    if (auto found = m_binders.find(sym.getFormula()->uid); found != m_binders.end() && found->second != nullptr)
    {
        if (!sym.containsAbstract())
        {
            VarID boundVar = found->second->call(m_rdb, sym.getFormula()->args);
            if (boundVar.isValid())
            {
                auto& formulaMask = sym.getFormula()->mask;
                auto& varDomain = m_rdb.getSolver().getDomain(boundVar);
                auto& domainMapping = found->second->getDomainMapping();

                Literal lit(boundVar, ValueSet(varDomain.getDomainSize(), false));
                for (auto it = formulaMask.beginSetBits(), itEnd = formulaMask.endSetBits(); it != itEnd; ++it)
                {
                    lit.values[ varDomain.getIndexForValue(domainMapping[*it]) ] = true;
                }
                
                if (!m_rdb.getSolver().getVariableDB()->constrainToValues(lit, nullptr))
                {
                    m_failure = true;
                }
            }
        }
        else
        {
            // Abstract atoms need to constrain every (relevant) vertices' corresponding variable.
            for (int vertex = 0; vertex < topology->getNumVertices(); ++vertex)
            {
                ProgramSymbol concreteSym = sym.makeConcrete(vertex);
                if (concreteSym.isValid())
                {
                    VarID boundVar = found->second->call(m_rdb, concreteSym.getFormula()->args);
                    if (boundVar.isValid())
                    {
                        auto& formulaMask = sym.getFormula()->mask;
                        auto& varDomain = m_rdb.getSolver().getDomain(boundVar);
                        auto& domainMapping = found->second->getDomainMapping();

                        Literal lit(boundVar, ValueSet(varDomain.getDomainSize(), false));
                        for (auto it = formulaMask.beginSetBits(), itEnd = formulaMask.endSetBits(); it != itEnd; ++it)
                        {
                            lit.values[ varDomain.getIndexForValue(domainMapping[*it]) ] = true;
                        }
                       
                        if (!m_rdb.getSolver().getVariableDB()->constrainToValues(lit, nullptr))
                        {
                            m_failure = true;
                        }
                    }
                }
            }
        }
    }
}

ConstraintSolver& ProgramCompiler::getSolver() const
{
    return m_rdb.getSolver();
}

bool ProgramCompiler::addGroundedAtom(const CompilerAtom& atom, const ITopologyPtr& topology)
{
    vxy_assert(atom.symbol.isFormula());
    vxy_assert(!atom.symbol.isNegated());
    
    auto domainIt = m_groundedAtoms.find(atom.symbol.getFormula()->uid);
    if (domainIt == m_groundedAtoms.end())
    {
        domainIt = m_groundedAtoms.insert({atom.symbol.getFormula()->uid, make_unique<AtomDomain>()}).first;
        domainIt->second->uid = atom.symbol.getFormula()->uid;
    }

    bool isNew = false;

    AtomDomain& domain = *domainIt->second;

    auto unmaskedSym = atom.symbol.unmasked();
    
    auto atomIt = domain.map.find(unmaskedSym);
    if (atomIt == domain.map.end())
    {
        domain.map.insert({unmaskedSym, domain.list.size()});
        domain.list.push_back(atom);

        if (unmaskedSym.containsAbstract())
        {
            if (!domain.containsAbstract)
            {
                domain.containsAbstract = true;
                domain.abstractTopology = topology;
            }
            else
            {
                vxy_assert_msg(domain.abstractTopology == topology,
                    "Mixed topologies in a formula definition: %s: not currently supported", 
                    unmaskedSym.getFormula()->name.c_str()
                );
            }
        }
        
        if (!domain.isExternal && unmaskedSym.isExternalFormula())
        {
            vxy_assert_msg(domain.list.size() == 1,
                    "Mixture of external and non-external atoms for formula %s", 
                    unmaskedSym.getFormula()->name.c_str()
            );
            domain.isExternal = true;
        }

        isNew = true;
    }
    else
    {
        CompilerAtom& existing = domain.list[atomIt->second];
        existing.symbol = existing.symbol.withIncludedMask(atom.symbol.getFormula()->mask);
        existing.facts.include(atom.facts);
    }

    return isNew;
}

void ProgramCompiler::transformRules()
{
    vector<GroundedRule> originalRules;
    swap(originalRules, m_groundedRules);

    for (auto& origRule : originalRules)
    {
        transformRule(move(origRule));        
    }
}

void ProgramCompiler::transformRule(GroundedRule&& rule)
{
    if (rule.headType == ERuleHeadType::Choice)
    {
        transformChoice(move(rule));
    }
    else if (rule.headType == ERuleHeadType::Disjunction)
    {
        transformDisjunction(move(rule));
    }
    else
    {
        vxy_assert(rule.headType == ERuleHeadType::Normal);
        vxy_assert(rule.heads.size() <= 1);
        addTransformedRule(move(rule));
    }
}

void ProgramCompiler::transformChoice(GroundedRule&& rule)
{
    vxy_assert(rule.headType == ERuleHeadType::Choice);

    // head choice "H1 .. \/ Hn" becomes
    // H1.choice() <- <body>
    // ...
    // Hn.choice() <- <body>
    
    for (const auto& headSym : rule.heads)
    {
        vxy_assert(headSym.isNormalFormula());
        
        addTransformedRule(GroundedRule{
            ERuleHeadType::Choice,
            vector{headSym},
            rule.body,
            rule.topology
        });
    }
}

void ProgramCompiler::transformDisjunction(GroundedRule&& rule)
{
    vxy_assert(rule.headType == ERuleHeadType::Disjunction);
    if (rule.heads.size() <= 1)
    {
        addTransformedRule(GroundedRule{ERuleHeadType::Normal, rule.heads, rule.body, rule.topology});
    }
    else
    {
        // For each head:
        // Hi <- <body> /\ {not Hn | n != i}
        for (int i = 0; i < rule.heads.size(); ++i)
        {
            vector<ProgramSymbol> extBody = rule.body;
            for (int j = 0; j < rule.heads.size(); ++j)
            {
                if (i == j) continue;
                extBody.push_back(rule.heads[j].negatedFormula());
            }
            addTransformedRule(GroundedRule{ERuleHeadType::Normal, vector{rule.heads[i]}, extBody, rule.topology});
        }
    }    
}

bool ProgramCompiler::addTransformedRule(GroundedRule&& rule)
{
    vxy_assert(rule.headType != ERuleHeadType::Disjunction);
    vxy_assert(rule.heads.size() <= 1);

    if (!rule.heads.empty())
    {
        ValueSet factMask(rule.heads[0].getFormula()->mask.size(), false);
        addGroundedAtom(CompilerAtom{rule.heads[0], factMask}, rule.topology);
    }

    // remove duplicates
    // silently discard rule if it is self-contradicting (p and -p)
    vector<ProgramSymbol> newBody = rule.body;
    for (auto it = newBody.begin(); it != newBody.end(); ++it)
    {
        const ProgramSymbol& cur = *it;

        if (cur.isFormula())
        {
            ProgramSymbol inversed = cur.negatedFormula();
            if (contains(it+1, newBody.end(), inversed))
            {
                // body contains an atom and its inverse == impossible to satisfy, no need to add rule.
                return false;
            }
        }
        
        // remove duplicates of the same atom
        auto next = it+1;
        while (true)
        {
            next = find(next, newBody.end(), cur);
            if (next == newBody.end())
            {
                break;
            }
            next = newBody.erase_unsorted(next);
        }
    }

    m_groundedRules.push_back(GroundedRule{
        rule.headType,
        move(rule.heads),
        move(newBody),
        rule.topology
    });
    return true;
}

FormulaMapper::FormulaMapper(RuleDatabase& rdb, FormulaUID formulaUID, const wchar_t* formulaName, int domainSize, const ITopologyPtr& topology, BindCaller* binder)
    : m_rdb(&rdb)
    , m_solver(m_rdb->getSolver())
    , m_formulaUID(formulaUID)
    , m_formulaName(formulaName)
    , m_domainSize(domainSize)
    , m_topology(topology)
    , m_binder(binder)
{
}

void FormulaMapper::lockVariableCreation()
{
    m_rdb = nullptr;
}

VarID FormulaMapper::getVariableForArguments(const vector<ProgramSymbol>& concrete, CreationType creationType) const
{
    size_t hashCode = ArgumentHasher()(concrete);
    auto found = m_bindMap.find_by_hash(concrete, hashCode);
    if (found != m_bindMap.end())
    {
        return found->second;
    }
    
    if (m_rdb == nullptr || creationType == NeverCreate)
    {
        return {};
    }
    
    if (m_binder != nullptr)
    {
        VarID var = m_binder->call(*m_rdb, concrete);
        vxy_assert(var.isValid());

        m_bindMap.insert(hashCode, nullptr, {concrete, var});
        return var;
    }

    if (creationType == CreateIfBound)
    {
        return {};
    }
    
    wstring name = m_formulaName;
    name.append(TEXT("("));
    for (int i = 0; i < concrete.size(); ++i)
    {
        if (i > 0)
        {
            name.append(TEXT(", "));
        }
        name.append(m_topology->vertexIndexToString(concrete[i].getInt()));
    }
    name.append(TEXT(")"));

    auto newVar = m_rdb->getSolver().makeVariable(name, SolverVariableDomain(0, m_domainSize));
    if (LOG_VAR_INSTANTIATION)
    {
        VERTEXY_LOG("  Created %s", name.c_str());
    }

    m_bindMap.insert(hashCode, nullptr, {concrete, newVar});
    return newVar;
}

void FormulaMapper::getDomainMapping(vector<int>& outMapping) const
{
    if (m_binder != nullptr)
    {
        outMapping = m_binder->getDomainMapping();
    }
    else
    {
        outMapping.clear();
        for (int i = 1; i <= m_domainSize; ++i)
        {
            outMapping.push_back(i);
        }
    }
}

FormulaGraphRelation::FormulaGraphRelation(const FormulaMapperPtr& bindMapper, const ProgramSymbol& symbol, bool headTerm)
    : m_formulaMapper(bindMapper)
    , m_symbol(symbol)
    , m_isHeadTerm(headTerm)
{
    vxy_assert(symbol.isNormalFormula());
    vxy_assert(symbol.isPositive());
    vxy_assert(m_formulaMapper->getFormulaUID() == symbol.getFormula()->uid);
}

void FormulaGraphRelation::getDomainMapping(vector<int>& outMapping) const
{
    m_formulaMapper->getDomainMapping(outMapping);
}

bool FormulaGraphRelation::getRelation(VertexID sourceVertex, VarID& out) const
{
    if (!makeConcrete(sourceVertex, m_concrete))
    {
        return false;
    }

    out = m_formulaMapper->getVariableForArguments(m_concrete, m_isHeadTerm ? FormulaMapper::AlwaysCreate : FormulaMapper::NeverCreate);
    return out.isValid();
}

bool FormulaGraphRelation::equals(const IGraphRelation<VarID>& rhs) const
{
    if (auto rrhs = dynamic_cast<const FormulaGraphRelation*>(&rhs))
    {
        return rrhs->m_formulaMapper == m_formulaMapper && rrhs->m_symbol == m_symbol;
    }
    return false;
}

size_t FormulaGraphRelation::hash() const
{
    return m_symbol.hash();
}

wstring FormulaGraphRelation::toString() const
{
    wstring out = TEXT("F:");
    out.append(m_symbol.toString());
    return out;
}

bool FormulaGraphRelation::instantiateNecessary(int vertex, VarID& outVar) const
{
    if (!makeConcrete(vertex, m_concrete))
    {
        return false;
    }

    outVar = m_formulaMapper->getVariableForArguments(m_concrete, FormulaMapper::CreateIfBound);
    return outVar.isValid();
}

void FormulaGraphRelation::lockVariableCreation() const
{
    m_formulaMapper->lockVariableCreation();
}

bool FormulaGraphRelation::makeConcrete(int vertex, vector<ProgramSymbol>& outConcrete) const
{
    auto formula = m_symbol.getFormula();
    const int numArgs = formula->args.size();
    m_concrete.resize(numArgs);
    
    for (int i = 0; i < numArgs; ++i)
    {
        outConcrete[i] = formula->args[i].makeConcrete(vertex);
        if (!outConcrete[i].isValid())
        {
            return false;
        }
    }
    return true;
}

ExternalFormulaGraphRelation::ExternalFormulaGraphRelation(const ProgramSymbol& symbol, const SignedClause& trueValue)
    : m_symbol(symbol)
    , m_trueValue(trueValue)
{
    vxy_assert(m_symbol.isExternalFormula());
    vxy_assert(!m_symbol.isNegated());
}

void ExternalFormulaGraphRelation::getDomainMapping(vector<int>& outMapping) const
{
    outMapping.clear();
    outMapping.push_back(1);
}

bool ExternalFormulaGraphRelation::getRelation(VertexID sourceVertex, VarID& out) const
{
    out = m_trueValue.variable;

    ProgramSymbol concrete = m_symbol.makeConcrete(sourceVertex);
    return concrete.isValid();
}

bool ExternalFormulaGraphRelation::equals(const IGraphRelation<VarID>& rhs) const
{
    if (auto rrhs = dynamic_cast<const ExternalFormulaGraphRelation*>(&rhs))
    {
        return rrhs->m_symbol == m_symbol && rrhs->m_trueValue == m_trueValue;
    }
    return false;
}

size_t ExternalFormulaGraphRelation::hash() const
{
    return m_symbol.hash();
}

wstring ExternalFormulaGraphRelation::toString() const
{
    wstring out;
    out.sprintf(TEXT("external:%s"), m_symbol.toString().c_str());
    return out;
}

HasRelationGraphRelation::HasRelationGraphRelation(const IGraphRelationPtr<VertexID>& relation)
    : m_relation(relation)
{
}

bool HasRelationGraphRelation::getRelation(VertexID sourceVertex, bool& out) const
{
    int ignored;
    out = m_relation->getRelation(sourceVertex, ignored);
    return true;
}

bool HasRelationGraphRelation::equals(const IGraphRelation<bool>& rhs) const
{
    if (auto rrhs = dynamic_cast<const HasRelationGraphRelation*>(&rhs))
    {
        return m_relation->equals(*rrhs->m_relation);
    }
    return false;
}

size_t HasRelationGraphRelation::hash() const
{
    return m_relation->hash();
}

wstring HasRelationGraphRelation::toString() const
{
    wstring out = TEXT("HasRelation(");
    out.append(m_relation->toString());
    out.append(TEXT(")"));
    return out;
}
