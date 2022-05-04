// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramCompiler.h"

#include "ConstraintSolver.h"
#include "rules/RuleDatabase.h"
#include "topology/DigraphTopology.h"
#include "topology/ITopology.h"
#include "topology/TopologySearch.h"

using namespace Vertexy;
static constexpr bool LOG_RULE_INSTANTIATION = false;
static constexpr bool LOG_MATH_REWRITE = false;

struct VariableNameAllocator
{
    static const wchar_t* allocate()
    {
        storage.emplace_back(wstring::CtorSprintf(), TEXT("__M%d"), count++);
        return storage.back().c_str();
    }
    static void reset() { count = 1; }
    inline static int count = 1;
    inline static vector<wstring> storage = {};
};

hash_set<ConstantFormula*, ConstantFormula::Hash, ConstantFormula::Hash> ConstantFormula::s_lookup;
vector<unique_ptr<ConstantFormula>> ConstantFormula::s_formulas;

bool ProgramCompiler::compile(RuleDatabase& rdb, const vector<RuleStatement*>& statements, const BindMap& binders)
{
    ProgramCompiler compiler(rdb, binders);
    compiler.rewriteMath(statements);

    compiler.createDependencyGraph(statements);
    compiler.createComponents(statements);

    compiler.ground();
    return !compiler.hasFailure();
}

ProgramCompiler::ProgramCompiler(RuleDatabase& rdb, const BindMap& binders)
    : m_rdb(rdb)
    , m_binders(binders)
{
}

//
// Rewrite all internal math terms are outside of any functions, and on the right hand side of relational terms
// e.g.
//
// A(X+1) <<= B(X)
//  --> A(Y) <<= B(X) && Y == X+1
//
// A(Y) <<= B(X) && C(X+1==Y-1)
//  --> A(Y) <<= B(X) && C(Z==W) && Z == X+1 && W == Y-1
//
void ProgramCompiler::rewriteMath(const vector<RuleStatement*>& statements)
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

    struct TermHasher
    {
        size_t operator()(const BinaryOpTerm* term) const
        {
            return term->hash();
        }
        size_t operator()(const UBinaryOpTerm& term) const
        {
            return term->hash();
        }
    };

    for (auto& stmt : statements)
    {
        VariableNameAllocator::reset();

        hash_map<const BinaryOpTerm*, ProgramVariable, TermHasher, pointer_value_equality<BinaryOpTerm>> replacements;
        hash_map<ProgramVariable, UBinaryOpTerm> assignments;

        stmt->visit<Term>([&](const Term* term)
        {
            if (auto binOpTerm = dynamic_cast<const BinaryOpTerm*>(term))
            {
                if (getBinOpType(binOpTerm) == BinOpType::Math)
                {
                    auto insertionPoint = replacements.find(binOpTerm);
                    if (insertionPoint == replacements.end())
                    {
                        ProgramVariable newVar(VariableNameAllocator::allocate());

                        auto clone = UBinaryOpTerm(move(static_cast<BinaryOpTerm*>(binOpTerm->clone().detach())));

                        insertionPoint = replacements.insert({clone.get(), newVar}).first;
                        assignments[insertionPoint->second] = move(clone);
                    }
                }
            }
        });

        if (!replacements.empty())
        {
            wstring before = stmt->toString();

            stmt->replace<BinaryOpTerm>([&](const BinaryOpTerm* term) -> UTerm
            {
                auto found = replacements.find(term);
                if (found != replacements.end())
                {
                    return make_unique<VariableTerm>(found->second);
                }
                return nullptr;
            });

            for (auto& assignment : assignments)
            {
                auto lhs = make_unique<VariableTerm>(assignment.first);
                auto assignmentTerm = make_unique<BinaryOpTerm>(
                    EBinaryOperatorType::Equality,
                    move(lhs),
                    move(assignment.second)
                );
                stmt->body.push_back(move(assignmentTerm));
            }

            if constexpr (LOG_MATH_REWRITE)
            {
                VERTEXY_LOG("Rewrote:\n  %s\n  %s", before.c_str(), stmt->toString().c_str());
            }
        }
    }
}

// Create the dependency graph, where each graph edge points from a formula head to a formula body that contains that
// head. The strongly connected components are cyclical dependencies between rules, which need to be handled specially.
void ProgramCompiler::createDependencyGraph(const vector<RuleStatement*>& stmts)
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
        // vxy_assert(!stmt->body.empty());

        m_depGraphData.get(vertex).statement = stmt;
        m_depGraphData.get(vertex).vertex = vertex;

        // mark any rules that contain facts in the body as groundable
        stmt->visitBody<FunctionTerm>([&](const FunctionTerm* bodyTerm)
        {
            if (m_groundedAtoms.find(bodyTerm->functionUID) != m_groundedAtoms.end())
            {
                m_depGraphData.get(vertex).marked = true;
                return Term::EVisitResponse::Abort;
            }
            return Term::EVisitResponse::Continue;
        });

        stmt->visitHead<FunctionHeadTerm>([&](const FunctionHeadTerm* headTerm)
        {
            for (int otherVertex = 0; otherVertex < m_depGraph->getNumVertices(); ++otherVertex)
            {
                auto& otherStmt = stmts[otherVertex];
                otherStmt->visitBody<FunctionTerm>([&](const FunctionTerm* bodyTerm)
                {
                    if (auto ext = dynamic_cast<const ExternalFunctionTerm*>(bodyTerm))
                    {
                        m_externals[ext->functionUID] = ext->provider;
                    }

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
void ProgramCompiler::createComponents(const vector<RuleStatement*>& stmts)
{
    m_components.clear();

    // Grab all the outer SCCs. They will be in reverse topographical order.
    vector<vector<int>> outerSCCs;
    TopologySearchAlgorithm::findStronglyConnectedComponents(*m_depGraph.get(), [&](int, auto it)
    {
        outerSCCs.emplace_back();
        for (; it; ++it)
        {
            outerSCCs.back().push_back(*it);
        }
    });

    for (int i = outerSCCs.size()-1, outerSCCIndex = 0; i >= 0; i--, outerSCCIndex++)
    {
        auto& curOuterSCC = outerSCCs[i];
        for (int j = 0; j < curOuterSCC.size(); ++j)
        {
            m_depGraphData.get(curOuterSCC[j]).outerSCCIndex = outerSCCIndex;
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
                vxy_sanity(m_depGraphData.get(vertex).statement == stmts[vertex]);
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
        // create rules out of this component (which may be self-recursive) until fixpoint.
        do
        {
            m_foundRecursion = false;
            for (DepGraphNodeData* stmtNode : component.stmts)
            {
                if (stmtNode->marked || stmtNode->statement->body.empty())
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
    vector<pair<VariableUID, vector<int>>> boundBy;

    RuleStatement* statement = statementNode->statement;

    //
    // Build dependency graph of variables found in the body.
    // Literals that are non-negative FunctionTerms provide support; everything else relies on support.
    //
    hash_map<VariableUID, size_t> seen;
    for (auto& bodyLit : statement->body)
    {
        litNodes.emplace_back();
        litNodes.back().lit = bodyLit.get();
        litNodes.back().numDeps = 0;

        vector<tuple<VariableTerm*, bool/*canEstablish*/>> varTerms;

        bodyLit->collectVars(varTerms);
        for (auto varTerm : varTerms)
        {
            ProgramVariable var = get<0>(varTerm)->var;

            // create a VarNode if we haven't made one already.
            auto found = seen.find(var.getID());
            if (found == seen.end())
            {
                found = seen.insert({var.getID(), varNodes.size()}).first;
                varNodes.emplace_back();
                varNodes.back().variable = var;

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
            litNodes.back().vars.push_back(found->second);
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

    VariableMap bound;
    vector<UInstantiator> instantiators;

    // go through each literal in dependency order.
    while (!openLits.empty())
    {
        LitNode* litNode = openLits.back();
        openLits.pop_back();

        // for each variable in this literal that hasn't been bound yet, mark the first VariableTerm it appears in
        // as being the variable provider. This will create the shared ProgramSymbol that all occurrences
        // of this variable within this body will point to.
        litNode->lit->createVariableReps(bound);

        instantiators.push_back(litNode->lit->instantiate(*this));

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

    vxy_assert_msg(instantiators.size() == litNodes.size(), "could not instantiate. unsafe vars?");

    // now instantiate!
    instantiateRule(statementNode, bound, instantiators);
}

void ProgramCompiler::instantiateRule(DepGraphNodeData* stmtNode, const VariableMap& varBindings, const vector<UInstantiator>& nodes, int cur)
{
    if (cur == nodes.size())
    {
        exportStatement(stmtNode, varBindings);
    }
    else
    {
        auto& inst = nodes[cur];
        for (inst->first(); !inst->hitEnd(); inst->match())
        {
            instantiateRule(stmtNode, varBindings, nodes, cur+1);
        }
    }
}

void ProgramCompiler::exportStatement(DepGraphNodeData* stmtNode, const VariableMap& varBindings)
{
    vector<ProgramSymbol> bodyTerms;
    RuleStatement* stmt = stmtNode->statement;
    for (auto& bodyTerm : stmt->body)
    {
        ProgramSymbol bodySym = bodyTerm->eval();
        vxy_assert(bodySym.isValid());
        if (bodySym.getType() == ESymbolType::Formula)
        {
            auto fnTerm = dynamic_cast<FunctionTerm*>(bodyTerm.get());
            vxy_assert_msg(fnTerm != nullptr, "not a function, but got a function symbol?");
            vxy_assert(fnTerm->negated == bodySym.isNegated());

            if (fnTerm->negated && !fnTerm->recursive && !hasAtom(fnTerm->assignedAtom.symbol.negatedFormula()))
            {
                // can't possibly be true, so no need to include.
                continue;
            }

            // Only non-fact atoms need to be included in the rule body
            if (!fnTerm->assignedAtom.isFact)
            {
                // no need to include in the body.
                bodyTerms.push_back(fnTerm->assignedAtom.symbol);
            }
        }
        else
        {
            vxy_assert(bodySym.getType() != ESymbolType::Integer || bodySym.getInt() > 0);
            // All binary/unary operation terms should've already been resolved.
            continue;
        }
    }

    if (stmt->head == nullptr && bodyTerms.empty())
    {
        VERTEXY_LOG("Failed during grounding: disallow() is impossible to satisfy");
        m_failure = true;

        return;
    }

    //
    // Replace all variables occurring in heads with their assigned symbols
    //

    bool isNormalRule = true;
    vector<ProgramSymbol> headSymbols;
    if (stmt->head != nullptr)
    {
        vector<tuple<VariableTerm*, bool>> vars;
        stmt->head->collectVars(vars, false);

        for (const tuple<VariableTerm*, bool>& tuple : vars)
        {
            auto varTerm = get<VariableTerm*>(tuple);
            auto found = varBindings.find(varTerm->var);
            vxy_assert_msg(found != varBindings.end(), "variable appears in head but not body?");
            varTerm->sharedBoundRef = found->second;
        }

        headSymbols = stmt->head->eval(isNormalRule);
        vxy_assert(!isNormalRule || headSymbols.size() == 1);
    }

    //
    // All all the head symbols to the grounded database, and mark all the rules that contain these heads
    // in the body to be (re)grounded.
    //

    bool areFacts = isNormalRule && bodyTerms.empty();
    for (const ProgramSymbol& headSym : headSymbols)
    {
        if (addGroundedAtom(CompilerAtom{headSym, areFacts}))
        {
            int numEdges = m_depGraph->getNumOutgoing(stmtNode->vertex);
            for (int edgeIdx = 0; edgeIdx < numEdges; ++edgeIdx)
            {
                int destVertex;
                m_depGraph->getOutgoingDestination(stmtNode->vertex, edgeIdx, destVertex);

                DepGraphNodeData& destNode = m_depGraphData.get(destVertex);
                destNode.marked = true;

                // If this is part of the same component, we need to recurse
                if (destNode.outerSCCIndex == stmtNode->outerSCCIndex && destNode.innerSCCIndex == stmtNode->innerSCCIndex)
                {
                    m_foundRecursion = true;
                }
            }
        }
    }

    auto toString = [&]()
    {
        wstring out;
        if (stmt->head)
        {
            out.append(stmt->head->toString());
            out.append(TEXT(" "));
        }

        if (!bodyTerms.empty())
        {
            out.append(TEXT(" <- "));
            bool first = true;
            for (auto& bodyTerm : bodyTerms)
            {
                if (!first)
                {
                    out.append(TEXT(", "));
                }
                first = false;
                out.append(bodyTerm.toString());
            }
        }
        return out;
    };

    //
    // If this isn't a fact, add it to the solver's rule database.
    //

    if (!areFacts)
    {
        TRuleHead<AtomID> ruleHead(ERuleHeadType::Normal);
        if (stmt->head != nullptr)
        {
            ruleHead = stmt->head->createHead(*this);
        }

        vector<AtomLiteral> ruleBodyLits;
        for (const ProgramSymbol& bodySym : bodyTerms)
        {
            AtomLiteral lit = exportAtom(bodySym.absolute());
            if (bodySym.isNegated())
            {
                lit = lit.inverted();
            }
            ruleBodyLits.emplace_back(lit);
        }

        m_rdb.addRule(ruleHead, ruleBodyLits);

        if (LOG_RULE_INSTANTIATION)
        {
            VERTEXY_LOG("Added rule %s", toString().c_str());
            VERTEXY_LOG("    From %s", stmt->toString().c_str());
        }
    }
    else
    {
        if (LOG_RULE_INSTANTIATION)
        {
            VERTEXY_LOG("Encoded fact %s", toString().c_str());
            VERTEXY_LOG("    From %s", stmt->toString().c_str());
        }

        if (stmt->head != nullptr)
        {
            stmt->head->bindAsFacts(*this);
        }
    }
}

AtomLiteral ProgramCompiler::exportAtom(const ProgramSymbol& sym, bool forHead)
{
    auto foundBinder = m_binders.find(sym.getFormula()->uid);

    vxy_assert(!sym.isNegated());
    auto found = m_createdAtomVars.find(sym);
    if (found != m_createdAtomVars.end())
    {
        if (!forHead || found->second.sign())
        {
            return found->second;
        }
        else
        {
            // RDB needs to invert this AtomLiteral, so we need to update to reflect the inversion.
            vxy_assert(foundBinder != m_binders.end());

            SignedClause clause = foundBinder->second->call(m_rdb, sym.getFormula()->args);
            vxy_assert(clause.variable.isValid());

            Literal lit = clause.translateToLiteral(m_rdb);

            // create the ID and invert previously exported literals with that ID.
            AtomID newID = m_rdb.createHeadAtom(lit,sym.toString().c_str());
            found->second = AtomLiteral(newID, true);
            return found->second;
        }
    }

    //
    // See if there is a binder for this formula. If so, we want to call it to create the RDB atom with
    // a solver Literal assigned to it.
    //

    if (foundBinder != m_binders.end() && foundBinder->second != nullptr)
    {
        SignedClause clause = foundBinder->second->call(m_rdb, sym.getFormula()->args);
        if (clause.variable.isValid())
        {
            Literal lit = clause.translateToLiteral(m_rdb);
            AtomLiteral atomLiteral = forHead
                ? AtomLiteral(m_rdb.createHeadAtom(lit, sym.toString().c_str()), true)
                : m_rdb.createAtom(lit, sym.toString().c_str());

            m_createdAtomVars.insert({sym, atomLiteral});
            return atomLiteral;
        }
    }

    //
    // Create a normal anonymous RDB atom (may or may not be exported to the solver)
    //

    AtomLiteral atomLiteral = AtomLiteral(m_rdb.createAtom(sym.toString().c_str()), true);
    m_createdAtomVars.insert({sym, atomLiteral});
    return atomLiteral;
}

void ProgramCompiler::bindFactIfNeeded(const ProgramSymbol& sym)
{
    vxy_assert(!sym.isNegated());
    vxy_assert(m_createdAtomVars.find(sym) == m_createdAtomVars.end());
    if (auto found = m_binders.find(sym.getFormula()->uid); found != m_binders.end() && found->second != nullptr)
    {
        SignedClause clause = found->second->call(m_rdb, sym.getFormula()->args);
        if (clause.variable.isValid())
        {
            Literal lit = clause.translateToLiteral(m_rdb);
            if (!m_rdb.getSolver().getVariableDB()->constrainToValues(lit, nullptr))
            {
                m_failure = true;
            }
        }
    }
}

bool ProgramCompiler::addGroundedAtom(const CompilerAtom& atom)
{
    vxy_assert(atom.symbol.getType() == ESymbolType::Formula);
    auto domainIt = m_groundedAtoms.find(atom.symbol.getFormula()->uid);
    if (domainIt == m_groundedAtoms.end())
    {
        domainIt = m_groundedAtoms.insert({atom.symbol.getFormula()->uid, make_unique<AtomDomain>()}).first;
    }

    bool isNew = false;

    AtomDomain& domain = *domainIt->second;
    auto atomIt = domain.map.find(atom.symbol);
    if (atomIt == domain.map.end())
    {
        domain.map.insert({atom.symbol, domain.list.size()});
        domain.list.push_back(atom);

        isNew = true;
    }
    else
    {
        CompilerAtom& existing = domain.list[atomIt->second];
        existing.isFact = existing.isFact || atom.isFact;
    }

    return isNew;
}
