// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramCompiler.h"
#include "rules/RuleDatabase.h"
#include "topology/DigraphTopology.h"
#include "topology/ITopology.h"
#include "topology/TopologySearch.h"

using namespace Vertexy;

hash_set<ConstantFormula*, ConstantFormula::Hash, ConstantFormula::Hash> ConstantFormula::s_lookup;
vector<unique_ptr<ConstantFormula>> ConstantFormula::s_formulas;

void ProgramCompiler::compile(ProgramInstance* instance)
{
    m_instance = instance;
    vector<RuleStatement*> nonFacts = extractFacts();

    m_components = createComponents(nonFacts);
    ground();
}

vector<RuleStatement*> ProgramCompiler::extractFacts()
{
    vector<RuleStatement*> nonFactStatements;
    nonFactStatements.reserve(m_instance->getRuleStatements().size());

    for (auto& stmt : m_instance->getRuleStatements())
    {
        if (!stmt->body.empty())
        {
            nonFactStatements.push_back(stmt.get());
        }
        // if this is a fact, add it to the atom database.
        else
        {
            vxy_assert_msg(stmt->head != nullptr, "empty rule?");
            vxy_sanity_msg(!stmt->bodyContains<VariableTerm>(), "cannot have a fact statement containing variables");

            bool isNormalRule = false;
            vector<ProgramSymbol> symbols = stmt->head->eval(isNormalRule);
            for (auto& symbol : symbols)
            {
                vxy_assert(!symbol.isNegated());
                addAtom(CompilerAtom{symbol, /*isFact=*/isNormalRule});

                if (!isNormalRule)
                {
                    auto found = m_createdAtomVars.find(symbol);
                    if (found == m_createdAtomVars.end())
                    {
                        found = m_createdAtomVars.insert({symbol, m_rdb.createAtom(symbol.toString().c_str())}).first;
                    }
                }
            }
        }
    }

    return nonFactStatements;
}

// Builds the set of components, where each component is a SCC of the dependency graph of positive literals.
// I.e., each graph edge points from a formula head to a formula body that contains that head. The strongly
// connected components are cyclical dependencies between rules, which need to be handled specially.
//
// OUTPUT: an array of components (set or rules), ordered by inverse topological sort. I.e., all statements in
// each component can be reified entirely by components later in the list.
vector<ProgramCompiler::Component> ProgramCompiler::createComponents(const vector<RuleStatement*>& stmts)
{
    m_depGraph = make_shared<DigraphTopology>();
    m_depGraph->reset(stmts.size());

    vector<Component> output;

    m_depGraphData.initialize(ITopology::adapt(m_depGraph));

    vector<vector<bool>> edgeIsPositive;
    edgeIsPositive.resize(m_depGraph->getNumVertices());

    // Build a graph, where each node is a Statement.
    // Create edges between Statements where a rule head points toward the bodies those heads appear in.
    for (int vertex = 0; vertex < m_depGraph->getNumVertices(); ++vertex)
    {
        auto& stmt = stmts[vertex];
        vxy_assert(!stmt->body.empty());

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
                if (vertex == otherVertex)
                {
                    continue;
                }

                auto& otherStmt = stmts[otherVertex];
                otherStmt->visitBody<FunctionTerm>([&](const FunctionTerm* bodyTerm)
                {
                    if (auto ext = dynamic_cast<const ExternalFunctionTerm*>(bodyTerm))
                    {
                        m_externals[ext->functionUID] = ext->provider;
                    }

                    if (headTerm->functionUID == bodyTerm->functionUID && !m_depGraph->hasEdge(vertex, otherVertex))
                    {
                        edgeIsPositive[vertex].push_back(!bodyTerm->negated);
                        m_depGraph->addEdge(vertex, otherVertex);
                        vxy_sanity(edgeIsPositive[vertex].size() == m_depGraph->getNumOutgoing(vertex));
                    }
                });
            }
        });
    }

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
            m_depGraphData.get(curOuterSCC[j]).outerSCCIndex = outerSCCIndex;
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

                if (m_depGraphData.get(destVertex).outerSCCIndex == outerSCCIndex && edgeIsPositive[vertex][edgeIdx])
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
        for (int j = posSCCs.size()-1, posSCCIndex = 0; j >= 0; --j, ++posSCCIndex)
        {
            for (int vertex : posSCCs[j])
            {
                m_depGraphData.get(vertex).innerSCCIndex = posSCCIndex;
            }
        }

        //
        // Write out rule statements and record any positive-recursive literals
        //

        for (int j = posSCCs.size()-1, posSCCIndex = 0; j >= 0; --j, ++posSCCIndex)
        {
            auto& posSCC = posSCCs[j];

            vector<DepGraphNodeData*> componentNodes;
            componentNodes.reserve(posSCC.size());

            for (int vertex : posSCC)
            {
                vxy_sanity(m_depGraphData.get(vertex).statement == stmts[vertex]);
                componentNodes.push_back(&m_depGraphData.get(vertex));
            }

            output.emplace_back(move(componentNodes), outerSCCIndex, posSCCIndex);
        }
    }

    return output;
}

void ProgramCompiler::ground()
{
    for (auto& c : m_components)
    {
        groundComponent(c);
    }
}

void ProgramCompiler::groundComponent(const Component& comp)
{
    // create rules out of this component (which may be self-recursive) until fixpoint.
    do
    {
        m_foundRecursion = false;
        for (DepGraphNodeData* stmtNode : comp.stmts)
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
        emit(stmtNode, varBindings);
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

void ProgramCompiler::emit(DepGraphNodeData* stmtNode, const VariableMap& varBindings)
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
        addAtom(CompilerAtom{headSym, areFacts});

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
        vector<AtomLiteral> ruleBodyLits;
        for (const ProgramSymbol& bodySym : bodyTerms)
        {
            ProgramSymbol absSym = bodySym.absolute();

            auto found = m_createdAtomVars.find(absSym);
            if (found == m_createdAtomVars.end())
            {
                found = m_createdAtomVars.insert({absSym, m_rdb.createAtom(absSym.toString().c_str())}).first;
            }
            ruleBodyLits.emplace_back(found->second, !bodySym.isNegated());
        }

        if (stmt->head != nullptr)
        {
            TRuleHead<AtomID> ruleHead = stmt->head->createHead(m_rdb, m_createdAtomVars);
            m_rdb.addRule(ruleHead, ruleBodyLits);
        }
        else
        {
            m_rdb.disallow(ruleBodyLits);
        }
        VERTEXY_LOG("Added rule %s", toString().c_str());
    }
    else
    {
        VERTEXY_LOG("Encoded fact %s", toString().c_str());
    }
}

void ProgramCompiler::addAtom(const CompilerAtom& atom)
{
    vxy_assert(atom.symbol.getType() == ESymbolType::Formula);
    auto domainIt = m_groundedAtoms.find(atom.symbol.getFormula()->uid);
    if (domainIt == m_groundedAtoms.end())
    {
        domainIt = m_groundedAtoms.insert({atom.symbol.getFormula()->uid, make_unique<AtomDomain>()}).first;
    }

    AtomDomain& domain = *domainIt->second;
    auto atomIt = domain.map.find(atom.symbol);
    if (atomIt == domain.map.end())
    {
        domain.map.insert({atom.symbol, domain.list.size()});
        domain.list.push_back(atom);
    }
    else
    {
        CompilerAtom& existing = domain.list[atomIt->second];
        existing.isFact = existing.isFact || atom.isFact;
    }
}
