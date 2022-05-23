// Copyright Proletariat, Inc. All Rights Reserved.
#include "program/ProgramAST.h"
#include "program/ProgramInstantiators.h"
#include "rules/RuleDatabase.h"

using namespace Vertexy;

void Term::forChildren(const function<void(const Term*)>& visitor) const
{
    visit([&](const Term* term)
    {
        if (term != this)
        {
            visitor(term);
            return EVisitResponse::Skip;
        }
        return EVisitResponse::Continue;
    });
}

void Term::visit(const function<void(const Term*)>& visitor) const
{
    visit([&](const Term* term)
    {
        visitor(term);
        return EVisitResponse::Continue;
    });
}

void Term::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    forChildren([&](const Term* child)
    {
        child->collectVars(outVars, canEstablish);
    });
}

UInstantiator LiteralTerm::instantiate(ProgramCompiler&, const ITopologyPtr&)
{
    vxy_fail_msg("instantiate called on unexpected term");
    return nullptr;
}

// For each variable occurring in the literal, if it isn't in the set bound of bound vars yet, create the
// shared ProgramSymbol for it. Later occurrences will take a reference to the same ProgramSymbol.
bool LiteralTerm::createVariableReps(VariableMap& bound)
{
    vector<tuple<VariableTerm*, bool>> vars;
    collectVars(vars);

    bool foundNewBindings = false;
    for (auto& tuple : vars)
    {
        auto varTerm = get<VariableTerm*>(tuple);
        auto found = bound.find(varTerm->var);
        if (found == bound.end())
        {
            // Mark this term as being the variable that should match any symbols passed to it.
            // Later variables in the dependency chain will be matched against the matched symbol.
            varTerm->isBinder = true;
            varTerm->sharedBoundRef = make_shared<ProgramSymbol>();
            bound.insert({varTerm->var, varTerm->sharedBoundRef});
        }
        else
        {
            varTerm->sharedBoundRef = found->second;
        }
        foundNewBindings = true;
    }
    return foundNewBindings;
}

bool LiteralTerm::match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    ProgramSymbol evalSym = eval(overrideMap, boundVertex);
    return evalSym.isValid() && sym == evalSym;
}

wstring LiteralTerm::toString() const
{
    static AbstractOverrideMap temp;
    return eval(temp, ProgramSymbol(IdentityGraphRelation::get())).toString();
}

VariableTerm::VariableTerm(ProgramVariable param)
    : var(param)
{
}

wstring VariableTerm::toString() const
{
    if (sharedBoundRef != nullptr && sharedBoundRef->isValid())
    {
        return sharedBoundRef->toString();
    }
    else
    {
        return var.getName();
    }
}

bool VariableTerm::operator==(const LiteralTerm& rhs) const
{
    if (auto vrhs = dynamic_cast<const VariableTerm*>(&rhs))
    {
        return vrhs->var == var;
    }
    return false;
}

bool VariableTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    return visitor(this) != EVisitResponse::Abort;
}

UTerm VariableTerm::clone() const
{
    return make_unique<VariableTerm>(var);
}

void VariableTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    outVars.push_back(make_pair(const_cast<VariableTerm*>(this), canEstablish));
}

bool VariableTerm::match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (isBinder)
    {
        // if this is the term where the variable first appears in, we take on whatever symbol was handed to us.
        // The ProgramSymbol pointed to by sharedBoundRef is shared by all other VariableTerms for the same variable in the
        // rule's body.
        *sharedBoundRef = sym;
        return true;
    }
    // Otherwise this is a variable that was already bound earlier. Check for equality.
    else if (*sharedBoundRef == sym)
    {
        return true;
    }
    else if (sharedBoundRef->isAbstract())
    {
        // We can unify an abstract with a concrete symbol, by creating a relation that will only
        // be satisfied if the abstract is resolved to the concrete symbol.
        if (sym.isInteger())
        {
            auto found = overrideMap.find(sharedBoundRef.get());
            if (found != overrideMap.end())
            {
                return found->second == sym;
            }
            else
            {
                overrideMap.insert({sharedBoundRef.get(), sym});
            }
            return true;
        }
        else if (sym.isAbstract())
        {
            // if (sharedBoundRef->isAbstract())
            // {
            //     auto rel = TManyToOneGraphRelation<int>::combine(sharedBoundRef->getAbstractRelation(), sym.getAbstractRelation());
            //     overrideMap.insert({sharedBoundRef.get(), ProgramSymbol(rel)});
            // }
            // else
            // {
            //     vxy_assert(sharedBoundRef->isVertex());
            //     overrideMap.insert({sharedBoundRef.get(), sym});
            // }
            return true;
        }
    }
    return false;
}

ProgramSymbol VariableTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    auto found = overrideMap.find(sharedBoundRef.get());
    return found != overrideMap.end() ? found->second : *sharedBoundRef;
}

SymbolTerm::SymbolTerm(const ProgramSymbol& sym)
    : sym(sym)
{
}

bool SymbolTerm::operator==(const LiteralTerm& rhs) const
{
    if (auto srhs = dynamic_cast<const SymbolTerm*>(&rhs))
    {
        return srhs->sym == sym;
    }
    return false;
}

bool SymbolTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    return visitor(this) != EVisitResponse::Abort;
}

UTerm SymbolTerm::clone() const
{
    return make_unique<SymbolTerm>(sym);
}

VertexTerm::VertexTerm()
{
}

bool VertexTerm::operator==(const LiteralTerm& rhs) const
{
    return dynamic_cast<const VertexTerm*>(&rhs) != nullptr;
}

bool VertexTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    return visitor(this) != EVisitResponse::Abort;
}

UTerm VertexTerm::clone() const
{
    return make_unique<VertexTerm>();
}

bool VertexTerm::match(const ProgramSymbol& sym, AbstractOverrideMap&, ProgramSymbol& boundVertex)
{
    if (sym.isAbstract() && sym.getAbstractRelation()->equals(*IdentityGraphRelation::get()))
    {
        return true;
    }
    
    if (boundVertex.isValid())
    {
        return sym == boundVertex;
    }
    else if (sym.isInteger())
    {
        boundVertex = sym;
        return true;
    }

    return false;
}

ProgramSymbol VertexTerm::eval(const AbstractOverrideMap&, const ProgramSymbol& boundVertex) const
{
    return boundVertex.isValid() ? boundVertex : ProgramSymbol(IdentityGraphRelation::get());
}

FunctionTerm::FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, vector<ULiteralTerm>&& arguments, bool negated, const IExternalFormulaProviderPtr& provider)
    : functionUID(functionUID)
    , functionName(functionName)
    , arguments(move(arguments))
    , provider(provider)
    , negated(negated)
{
}

void FunctionTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    if (provider != nullptr)
    {
        for (int i = 0; i < arguments.size(); ++i)
        {
            arguments[i]->collectVars(outVars, canEstablish && !negated && provider->canInstantiate(i));
        }
    }
    else
    {
        return LiteralTerm::collectVars(outVars, canEstablish && !negated);
    }
}

bool FunctionTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        for (auto& arg : arguments)
        {
            if (!arg->visit(visitor))
            {
                return false;
            }
        }
    }
    return true;
}

void FunctionTerm::replace(const function<unique_ptr<Term>(const Term*)> visitor)
{
    for (auto& arg : arguments)
    {
        if (!maybeReplaceChild(arg, visitor))
        {
            arg->replace(visitor);
        }
    }
}

ProgramSymbol FunctionTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    vector<ProgramSymbol> resolvedArgs;
    resolvedArgs.reserve(arguments.size());
    for (auto& arg : arguments)
    {
        ProgramSymbol argSym = arg->eval(overrideMap, boundVertex);
        if (argSym.isInvalid())
        {
            return {};
        }
        resolvedArgs.push_back(argSym);
    }

    return ProgramSymbol(functionUID, functionName, resolvedArgs, negated, provider);
}

UInstantiator FunctionTerm::instantiate(ProgramCompiler& compiler, const ITopologyPtr& topology)
{
    if (provider != nullptr)
    {
        return make_unique<ExternalFunctionInstantiator>(*this);
    }
    else
    {
        return make_unique<FunctionInstantiator>(*this, compiler.getDomain(functionUID), topology);
    }
}

bool FunctionTerm::match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    vxy_assert(provider == nullptr); // should be handled by ExternalFunctionInstantiator instead

    if (sym.getType() != ESymbolType::Formula)
    {
        return false;
    }

    const ConstantFormula* cformula = sym.getFormula();
    vxy_assert(cformula->args.size() == arguments.size());

    for (int i = 0; i < arguments.size(); ++i)
    {
        if (!arguments[i]->match(cformula->args[i], overrideMap, boundVertex))
        {
            return false;
        }
    }

    return true;
}

bool FunctionTerm::hasAbstractArgument() const
{
    for (auto& arg : arguments)
    {
        if (auto symTerm = dynamic_cast<const SymbolTerm*>(arg.get()))
        {
            if (symTerm->sym.isAbstract())
            {
                return true;
            }
        }
    }
    return false;
}

UTerm FunctionTerm::clone() const
{
    vector<ULiteralTerm> clonedArgs;
    clonedArgs.reserve(arguments.size());
    for (auto& arg : arguments)
    {
        auto cloned = static_cast<LiteralTerm*>(arg->clone().detach());
        clonedArgs.push_back(unique_ptr<LiteralTerm>(move(cloned)));
    }
    return make_unique<FunctionTerm>(functionUID, functionName, move(clonedArgs), negated, provider);
}

wstring FunctionTerm::toString() const
{
    wstring out;
    if (negated)
    {
        out += TEXT("~");
    }

    out += functionName;
    out += TEXT("(");

    bool first = true;
    for (auto& arg : arguments)
    {
        if (!first)
        {
            out += TEXT(", ");
        }
        first = false;
        out.append(arg->toString());
    }

    out += TEXT(")");
    return out;
}

size_t FunctionTerm::hash() const
{
    size_t hash = eastl::hash<FormulaUID>()(functionUID);
    for (auto& arg : arguments)
    {
        hash = combineHashes(hash, arg->hash());
    }
    return hash;
}

bool FunctionTerm::operator==(const LiteralTerm& rhs) const
{
    if (auto frhs = dynamic_cast<const FunctionTerm*>(&rhs))
    {
        if (frhs->functionUID != functionUID)
        {
            return false;
        }
        vxy_sanity(frhs->arguments.size() == arguments.size());
        for (int i = 0; i < arguments.size(); ++i)
        {
            if ((*arguments[i]) != (*frhs->arguments[i]))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

UnaryOpTerm::UnaryOpTerm(EUnaryOperatorType op, ULiteralTerm&& child)
    : op(op)
    , child(move(child))
{
}

bool UnaryOpTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == EVisitResponse::Abort)
    {
        return false;
    }
    else if (resp == EVisitResponse::Skip)
    {
        return true;
    }
    else
    {
        return child->visit(visitor);
    }
}

void UnaryOpTerm::replace(const function<unique_ptr<Term>(const Term*)> visitor)
{
    if (!maybeReplaceChild(child, visitor))
    {
        child->replace(visitor);
    }
}

UTerm UnaryOpTerm::clone() const
{
    auto clonedChild = static_cast<LiteralTerm*>(child->clone().detach());
    return make_unique<UnaryOpTerm>(op, ULiteralTerm(move(clonedChild)));
}

ProgramSymbol UnaryOpTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    auto sym = child->eval(overrideMap, boundVertex);
    if (sym.isInvalid())
    {
        return {};
    }

    switch (op)
    {
    case EUnaryOperatorType::Negate:
        switch (sym.getType())
        {
        case ESymbolType::Integer:
            return ProgramSymbol(-sym.getInt());
        case ESymbolType::Abstract:
            return ProgramSymbol(make_shared<NegateGraphRelation>(sym.getAbstractRelation()));
        default:
            vxy_fail_msg("Unexpected symbol type");
        }
    default:
        vxy_fail_msg("Unexpected unary operator");
    }

    return {};
}

wstring UnaryOpTerm::toString() const
{
    wstring inner = child->toString();
    switch (op)
    {
    case EUnaryOperatorType::Negate:
        return TEXT("-") + inner;
    default:
        vxy_fail_msg("unexpected operator type");
        return inner;
    }
}

size_t UnaryOpTerm::hash() const
{
    size_t hash = eastl::hash<EUnaryOperatorType>()(op);
    return combineHashes(hash, child->hash());
}

bool UnaryOpTerm::operator==(const LiteralTerm& rhs) const
{
    if (auto urhs = dynamic_cast<const UnaryOpTerm*>(&rhs))
    {
        if (urhs->op == op && (*child) == (*urhs->child))
        {
            return true;
        }
    }
    return false;
}

BinaryOpTerm::BinaryOpTerm(EBinaryOperatorType op, ULiteralTerm&& lhs, ULiteralTerm&& rhs): op(op)
    , lhs(move(lhs))
    , rhs(move(rhs))
{
}

bool BinaryOpTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == EVisitResponse::Abort)
    {
        return false;
    }
    else if (resp != EVisitResponse::Skip)
    {
        if (!lhs->visit(visitor))
        {
            return false;
        }
        if (!rhs->visit(visitor))
        {
            return false;
        }
    }
    return true;
}

void BinaryOpTerm::replace(const function<UTerm(const Term*)> visitor)
{
    if (!maybeReplaceChild(lhs, visitor))
    {
        lhs->replace(visitor);
    }
    if (!maybeReplaceChild(rhs, visitor))
    {
        rhs->replace(visitor);
    }
}

void BinaryOpTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    // only left hand side of assignments can serve as establishment for variables
    lhs->collectVars(outVars, canEstablish && op == EBinaryOperatorType::Equality);
    rhs->collectVars(outVars, false);
}

ProgramSymbol BinaryOpTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ProgramSymbol resolvedLHS = lhs->eval(overrideMap, boundVertex);
    if (resolvedLHS.isInvalid())
    {
        return {};
    }

    ProgramSymbol resolvedRHS = rhs->eval(overrideMap, boundVertex);
    if (resolvedRHS.isInvalid())
    {
        return {};
    }
    vxy_assert_msg(
        resolvedLHS.getType() == ESymbolType::Integer || resolvedLHS.getType() == ESymbolType::Abstract,
        "can only apply binary operators on integer or abstract symbols"
    );
    vxy_assert_msg(
        resolvedRHS.getType() == ESymbolType::Integer || resolvedRHS.getType() == ESymbolType::Abstract,
        "can only apply binary operators on integer or abstract symbols"
    );

    if (resolvedLHS.getType() == ESymbolType::Integer && resolvedRHS.getType() == ESymbolType::Integer)
    {
        switch (op)
        {
        case EBinaryOperatorType::Add:
            return ProgramSymbol(resolvedLHS.getInt() + resolvedRHS.getInt());
        case EBinaryOperatorType::Subtract:
            return ProgramSymbol(resolvedLHS.getInt() - resolvedRHS.getInt());
        case EBinaryOperatorType::Multiply:
            return ProgramSymbol(resolvedLHS.getInt() * resolvedRHS.getInt());
        case EBinaryOperatorType::Divide:
            return ProgramSymbol(resolvedLHS.getInt() / resolvedRHS.getInt());
        case EBinaryOperatorType::Equality:
            return ProgramSymbol(resolvedLHS.getInt() == resolvedRHS.getInt() ? 1 : 0);
        case EBinaryOperatorType::Inequality:
            return ProgramSymbol(resolvedLHS.getInt() != resolvedRHS.getInt() ? 1 :0);
        case EBinaryOperatorType::LessThan:
            return ProgramSymbol(resolvedLHS.getInt() < resolvedRHS.getInt() ? 1 : 0);
        case EBinaryOperatorType::LessThanEq:
            return ProgramSymbol(resolvedLHS.getInt() <= resolvedRHS.getInt() ? 1 : 0);
        case EBinaryOperatorType::GreaterThan:
            return ProgramSymbol(resolvedLHS.getInt() > resolvedRHS.getInt() ? 1 : 0);
        case EBinaryOperatorType::GreaterThanEq:
            return ProgramSymbol(resolvedLHS.getInt() >= resolvedRHS.getInt() ? 1 : 0);
        default:
            vxy_fail_msg("unrecognized operator");
            return ProgramSymbol();
        }
    }
    else
    {
        auto leftRel = resolvedLHS.getType() == ESymbolType::Abstract
            ? resolvedLHS.getAbstractRelation()
            : make_shared<ConstantGraphRelation<int>>(resolvedLHS.getInt());

        auto rightRel = resolvedRHS.getType() == ESymbolType::Abstract
            ? resolvedRHS.getAbstractRelation()
            : make_shared<ConstantGraphRelation<int>>(resolvedRHS.getInt());

        if (op == EBinaryOperatorType::Equality && leftRel->equals(*rightRel))
        {
            return ProgramSymbol(1);            
        }        
        else if (op == EBinaryOperatorType::Inequality && leftRel->equals(*rightRel))
        {
            return ProgramSymbol(0);
        }

        return ProgramSymbol(make_shared<BinOpGraphRelation>(leftRel, rightRel, op));
    }
}

UTerm BinaryOpTerm::clone() const
{
    auto clonedLHS = static_cast<LiteralTerm*>(lhs->clone().detach());
    auto clonedRHS = static_cast<LiteralTerm*>(rhs->clone().detach());
    return make_unique<BinaryOpTerm>(op, ULiteralTerm(move(clonedLHS)), ULiteralTerm(move(clonedRHS)));
}

wstring BinaryOpTerm::toString() const
{
    wstring slhs = lhs->toString();
    wstring srhs = rhs->toString();

    switch (op)
    {
    case EBinaryOperatorType::Add:
        return {wstring::CtorSprintf(), TEXT("%s + %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::Subtract:
        return {wstring::CtorSprintf(), TEXT("%s - %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::Multiply:
        return {wstring::CtorSprintf(), TEXT("%s * %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::Divide:
        return {wstring::CtorSprintf(), TEXT("%s / %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::Equality:
        return {wstring::CtorSprintf(), TEXT("%s == %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::Inequality:
        return {wstring::CtorSprintf(), TEXT("%s != %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::LessThan:
        return {wstring::CtorSprintf(), TEXT("%s < %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::LessThanEq:
        return {wstring::CtorSprintf(), TEXT("%s <= %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::GreaterThan:
        return {wstring::CtorSprintf(), TEXT("%s > %s"), slhs.c_str(), srhs.c_str()};
    case EBinaryOperatorType::GreaterThanEq:
        return {wstring::CtorSprintf(), TEXT("%s >= %s"), slhs.c_str(), srhs.c_str()};
    default:
        vxy_fail_msg("unexpected binary operator");
        return {wstring::CtorSprintf(), TEXT("%s ?? %s"), slhs.c_str(), srhs.c_str()};
    }
}

UInstantiator BinaryOpTerm::instantiate(ProgramCompiler& compiler, const ITopologyPtr&)
{
    if (op == EBinaryOperatorType::Equality)
    {
        return make_unique<EqualityInstantiator>(*this, compiler);
    }
    else
    {
        return make_unique<RelationInstantiator>(*this, compiler);
    }
}

size_t BinaryOpTerm::hash() const
{
    size_t hash = eastl::hash<EBinaryOperatorType>()(op);
    hash = combineHashes(hash, lhs->hash());
    hash = combineHashes(hash, rhs->hash());
    return hash;
}

bool BinaryOpTerm::operator==(const LiteralTerm& term) const
{
    if (auto brhs = dynamic_cast<const BinaryOpTerm*>(&term))
    {
        return brhs->op == op && (*brhs->lhs) == *lhs && (*brhs->rhs) == *rhs;
    }
    return false;
}

FunctionHeadTerm::FunctionHeadTerm(FormulaUID inUID, const wchar_t* inName, vector<ULiteralTerm>&& inArgs)
    : functionUID(inUID)
    , functionName(inName)
    , arguments(move(inArgs))
{
}

ProgramSymbol FunctionHeadTerm::evalSingle(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    vector<ProgramSymbol> resolvedArgs;
    for (auto& arg : arguments)
    {
        ProgramSymbol argSym = arg->eval(overrideMap, boundVertex);
        if (argSym.isInvalid())
        {
            vxy_fail_msg("Expected a valid argument for head term");
            return {};
        }
        resolvedArgs.push_back(argSym);
    }

    return ProgramSymbol(functionUID, functionName, resolvedArgs, false);  
}

vector<ProgramSymbol> FunctionHeadTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule)
{
    isNormalRule = true;
    return {evalSingle(overrideMap, boundVertex)};
}

void FunctionHeadTerm::bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology)
{
    auto evaluated = evalSingle(overrideMap, boundVertex);
    vxy_assert(evaluated.isValid());
    compiler.bindFactIfNeeded(evaluated, topology);
}

bool FunctionHeadTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        for (auto& arg : arguments)
        {
            if (!arg->visit(visitor))
            {
                return false;
            }
        }
    }
    return true;
}

void FunctionHeadTerm::replace(const function<unique_ptr<Term>(const Term*)> visitor)
{
    for (auto& arg : arguments)
    {
        if (!maybeReplaceChild(arg, visitor))
        {
            arg->replace(visitor);
        }
    }
}

UTerm FunctionHeadTerm::clone() const
{
    vector<ULiteralTerm> clonedArgs;
    clonedArgs.reserve(arguments.size());
    for (auto& arg : arguments)
    {
        auto cloned = static_cast<LiteralTerm*>(arg->clone().detach());
        clonedArgs.push_back(unique_ptr<LiteralTerm>(move(cloned)));
    }
    return make_unique<FunctionHeadTerm>(functionUID, functionName, move(clonedArgs));
}

wstring FunctionHeadTerm::toString() const
{
    wstring out = functionName;
    out += TEXT("(");

    bool first = true;
    for (auto& arg : arguments)
    {
        if (!first)
        {
            out.append(TEXT(", "));
        }
        first = false;
        out.append(arg->toString());
    }
    out += TEXT(")");
    return out;
}

DisjunctionTerm::DisjunctionTerm(vector<UFunctionHeadTerm>&& children)
    : children(move(children))
{
}

bool DisjunctionTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        for (auto& child : children)
        {
            if (!child->visit(visitor))
            {
                return false;
            }
        }
    }
    return true;
}

void DisjunctionTerm::replace(const function<unique_ptr<Term>(const Term*)> visitor)
{
    for (auto& child : children)
    {
        if (!maybeReplaceChild(child, visitor))
        {
            child->replace(visitor);
        }
    }
}

UTerm DisjunctionTerm::clone() const
{
    vector<UFunctionHeadTerm> clonedChildren;
    clonedChildren.reserve(children.size());
    for (auto& child : children)
    {
        auto cloned = static_cast<FunctionHeadTerm*>(child->clone().detach());
        clonedChildren.push_back(UFunctionHeadTerm(move(cloned)));
    }
    return make_unique<DisjunctionTerm>(move(clonedChildren));
}

vector<ProgramSymbol> DisjunctionTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule)
{
    isNormalRule = false;

    vector<ProgramSymbol> out;
    out.reserve(children.size());

    for (auto& child : children)
    {
        auto childSym = child->evalSingle(overrideMap, boundVertex);
        if (!childSym.isValid())
        {
            return {};
        }
        else
        {
            out.push_back(childSym);
        }
    }
    return out;
}

wstring DisjunctionTerm::toString() const
{
    wstring out = TEXT("(");
    bool first = true;
    for (auto& child : children)
    {
        if (!first)
        {
            out.append(TEXT(" | "));
        }
        first = false;

        out.append(child->toString());
    }
    out.append(TEXT(")"));
    return out;
}

void DisjunctionTerm::bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology)
{
    for (auto& child : children)
    {
        child->bindAsFacts(compiler, overrideMap, boundVertex, topology);
    }
}

ChoiceTerm::ChoiceTerm(UFunctionHeadTerm&& term)
    : subTerm(move(term))
{
}

bool ChoiceTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == EVisitResponse::Abort)
    {
        return false;
    }
    else if (resp != EVisitResponse::Skip)
    {
        if (!subTerm->visit(visitor))
        {
            return false;
        }
    }
    return true;
}

void ChoiceTerm::replace(const function<unique_ptr<Term>(const Term*)> visitor)
{
    if (!maybeReplaceChild(subTerm, visitor))
    {
        subTerm->replace(visitor);
    }
}

UTerm ChoiceTerm::clone() const
{
    auto cloned = static_cast<FunctionHeadTerm*>(subTerm->clone().detach());
    return make_unique<ChoiceTerm>(UFunctionHeadTerm(move(cloned)));
}

vector<ProgramSymbol> ChoiceTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, bool& isNormalRule)
{
    isNormalRule = false;
    return {subTerm->evalSingle(overrideMap, boundVertex)};
}

wstring ChoiceTerm::toString() const
{
    wstring out;
    out.sprintf(TEXT("choice(%s)"), subTerm->toString().c_str());
    return out;
}

void ChoiceTerm::bindAsFacts(ProgramCompiler& compiler, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex, const ITopologyPtr& topology)
{
    subTerm->bindAsFacts(compiler, overrideMap, boundVertex, topology);
}

RuleStatement::RuleStatement(UHeadTerm&& head, vector<ULiteralTerm>&& body)
    : head(move(head))
    , body(move(body))
{
}

URuleStatement RuleStatement::clone() const
{
    HeadTerm* newHead = static_cast<HeadTerm*>(head != nullptr ? head->clone().detach() : nullptr);
    vector<ULiteralTerm> newBody;
    newBody.reserve(body.size());
    for (auto& bodyTerm : body)
    {
        auto newBodyTerm = static_cast<LiteralTerm*>(bodyTerm->clone().detach());
        newBody.emplace_back(move(newBodyTerm));
    }
    return make_unique<RuleStatement>(UHeadTerm(move(newHead)), move(newBody));
}

wstring RuleStatement::toString() const
{
    wstring out;
    if (head)
    {
        out.append(head->toString());
        out.append(TEXT(" "));
    }
    out.append(TEXT(" <- "));

    bool first = true;
    for (auto& bodyTerm : body)
    {
        if (!first)
        {
            out.append(TEXT(", "));
        }
        first = false;
        out.append(bodyTerm->toString());
    }

    return out;
}
