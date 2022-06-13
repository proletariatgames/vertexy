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
    return eval(temp, ProgramSymbol()).toString();
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

bool VariableTerm::containsAbstracts() const
{
    return sharedBoundRef != nullptr && sharedBoundRef->containsAbstract();
}

ProgramSymbol VariableTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    if (sharedBoundRef == nullptr)
    {
        return {};
    }
    
    auto found = overrideMap.find(sharedBoundRef.get());
    return found != overrideMap.end() ? found->second : *sharedBoundRef;
}

SymbolTerm::SymbolTerm(const ProgramSymbol& sym)
    : sym(sym)
{
}

UInstantiator SymbolTerm::instantiate(ProgramCompiler& compiler, const ITopologyPtr& topology)
{
    return make_unique<ConstInstantiator>(sym.getInt() > 0);
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
    if (boundVertex.isValid())
    {
        int constant = boundVertex.getInt();
        return ProgramSymbol(IdentityGraphRelation::get()->filter([constant](int v) { return v == constant; }));
    }
    
    return boundVertex.isValid() ? boundVertex : ProgramSymbol(IdentityGraphRelation::get());
}

FunctionTerm::FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, int domainSize, vector<ULiteralTerm>&& arguments, vector<UDomainTerm>&& domainTerms, bool negated, const IExternalFormulaProviderPtr& provider)
    : functionUID(functionUID)
    , functionName(functionName)
    , domainSize(domainSize)
    , arguments(move(arguments))
    , domainTerms(move(domainTerms))
    , provider(provider)
    , negated(negated)
{
    vxy_assert(domainSize >= 1);
}

void FunctionTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    for (auto& domainTerm : domainTerms)
    {
        // variables in domain masks never act as binders.
        domainTerm->collectVars(outVars, false);
    }
    
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

        for (auto& dterm : domainTerms)
        {
            if (!dterm->visit(visitor))
            {
                return false;
            }
        }
    }
    return true;
}

void FunctionTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
{
    for (auto& arg : arguments)
    {
        if (!maybeReplaceChild(arg, visitor))
        {
            arg->replace(visitor);
        }
    }

    for (auto& domainTerm : domainTerms)
    {
        if (!maybeReplaceChild(domainTerm, visitor))
        {
            domainTerm->replace(visitor);
        }
    }
}

ProgramSymbol FunctionTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    if (boundMask.isZero())
    {
        return {};
    }
    
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
    
    return ProgramSymbol(functionUID, functionName, resolvedArgs, boundMask, negated, provider);
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
    
    boundMask = cformula->mask;
    for (auto& domainTerm : domainTerms)
    {
        if (!domainTerm->match(boundMask, overrideMap, boundVertex))
        {
            return false;
        }
        domainTerm->eval(boundMask, overrideMap, boundVertex);
        if (boundMask.isZero())
        {
            return false;
        }
    }
    
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

bool FunctionTerm::containsAbstracts() const
{
    for (auto& arg : arguments)
    {
        if (arg->containsAbstracts())
        {
            return true;
        }
    }

    for (auto& dterm : domainTerms)
    {
        if (dterm->containsAbstracts())
        {
            return true;
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

    vector<UDomainTerm> clonedDomain;
    clonedDomain.reserve(domainTerms.size());
    for (auto& domainTerm : domainTerms)
    {
        auto cloned = static_cast<DomainTerm*>(domainTerm->clone().detach());
        clonedDomain.push_back(unique_ptr<DomainTerm>(move(cloned)));
    }
    
    return make_unique<FunctionTerm>(functionUID, functionName, domainSize, move(clonedArgs), move(clonedDomain), negated, provider);
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

    if (!domainTerms.empty())
    {
        out += TEXT("[");
        first = true;
        for (auto& domainTerm : domainTerms)
        {
            if (!first)
            {
                out += TEXT(" && ");
            }
            first = false;
            out += domainTerm->toString();
        }
        out += TEXT("]");
    }
    
    return out;
}

size_t FunctionTerm::hash() const
{
    size_t hash = eastl::hash<FormulaUID>()(functionUID);
    for (auto& arg : arguments)
    {
        hash = combineHashes(hash, arg->hash());
    }
    for (auto& domainTerm : domainTerms)
    {
        hash = combineHashes(hash, domainTerm->hash());
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

bool FunctionTerm::domainContainsAbstracts() const
{
    for (auto& domainTerm : domainTerms)
    {
        if (domainTerm->containsAbstracts())
        {
            return true;
        }
    }
    return false;
}

ValueSet FunctionTerm::getDomain(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ValueSet mask(domainSize, true);
    for (auto& domainTerm : domainTerms)
    {
        domainTerm->eval(mask, overrideMap, boundVertex);
    }
    return mask;
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

void UnaryOpTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
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
        case ESymbolType::PositiveInteger:
        case ESymbolType::NegativeInteger:
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

bool UnaryOpTerm::containsAbstracts() const
{
    return child->containsAbstracts();
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

void BinaryOpTerm::replace(const function<UTerm(Term*)>& visitor)
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
        resolvedLHS.isInteger() || resolvedLHS.isAbstract(),
        "can only apply binary operators on integer or abstract symbols"
    );
    vxy_assert_msg(
        resolvedRHS.isInteger() || resolvedRHS.isAbstract(),
        "can only apply binary operators on integer or abstract symbols"
    );

    if (resolvedLHS.isInteger() && resolvedRHS.isInteger())
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

bool BinaryOpTerm::containsAbstracts() const
{
    return lhs->containsAbstracts() || rhs->containsAbstracts();
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

LinearTerm::LinearTerm(ULiteralTerm&& varTerm, int offset, int multiplier)
    : childTerm(move(varTerm))
    , offset(move(offset))
    , multiplier(move(multiplier))
{
}

bool LinearTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        if (!childTerm->visit(visitor))
        {
            return false;
        }
    }
    return true;
}

void LinearTerm::replace(const function<UTerm(Term*)>& visitor)
{
    if (!maybeReplaceChild(childTerm, visitor))
    {
        childTerm->replace(visitor);
    }
}

void LinearTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    return childTerm->collectVars(outVars, canEstablish);
}

bool LinearTerm::match(const ProgramSymbol& sym, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex)
{
    if (sym.isInteger())
    {
        int shifted = sym.getInt() - offset;
        if ((shifted % multiplier) == 0)
        {
            return childTerm->match(ProgramSymbol(shifted/multiplier), overrideMap, boundVertex);
        }
    }
    return false;
}

ProgramSymbol LinearTerm::eval(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ProgramSymbol v = childTerm->eval(overrideMap, boundVertex);
    vxy_assert_msg(v.isInteger(), "Math operations on abstracts NYI");
    return v.getInt()*multiplier + offset;
}

bool LinearTerm::containsAbstracts() const
{
    return childTerm->containsAbstracts();
}

UTerm LinearTerm::clone() const
{
    auto clonedVarTerm = static_cast<VariableTerm*>(childTerm->clone().detach());
    return make_unique<LinearTerm>(UVariableTerm(move(clonedVarTerm)), offset, multiplier);
}

wstring LinearTerm::toString() const
{
    if (multiplier != 1)
    {
        return childTerm->toString() + TEXT("*") + to_wstring(multiplier) + TEXT(" + ") + to_wstring(offset);
    }
    else
    {
        return childTerm->toString() + TEXT(" + ") + to_wstring(offset);
    }
}

size_t LinearTerm::hash() const
{
    return combineHashes(combineHashes(childTerm->hash(), eastl::hash<int>()(multiplier)), eastl::hash<int>()(offset));
}

bool LinearTerm::operator==(const LiteralTerm& rhs) const
{
    if (this == &rhs) { return true; }
    if (auto rrhs = dynamic_cast<const LinearTerm*>(&rhs))
    {
        return rrhs->multiplier == multiplier && rrhs->offset == offset && *childTerm == *rrhs->childTerm;
    }
    return false;
}

ExplicitDomainTerm::ExplicitDomainTerm(ValueSet&& mask)
    : mask(move(mask))
{    
}

bool ExplicitDomainTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    return visitor(this) != EVisitResponse::Abort;
}

wstring ExplicitDomainTerm::toString() const
{
    return mask.toString();
}

unique_ptr<Term> ExplicitDomainTerm::clone() const
{
    return make_unique<ExplicitDomainTerm>(ValueSet(mask));
}

void ExplicitDomainTerm::eval(ValueSet& inOutMask, const AbstractOverrideMap&, const ProgramSymbol&) const
{
    inOutMask.intersect(mask);
}

bool ExplicitDomainTerm::match(const ValueSet& matchMask, AbstractOverrideMap&, ProgramSymbol&) const
{
    return mask.anyPossible(matchMask);
}

size_t ExplicitDomainTerm::hash() const
{
    return eastl::hash<ValueSet>()(mask);   
}

SubscriptDomainTerm::SubscriptDomainTerm(const FormulaDomainValueArray& array, ULiteralTerm&& inSubscriptTerm)
    : array(array)
    , subscriptTerm(move(inSubscriptTerm))
{    
}

bool SubscriptDomainTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        if (!subscriptTerm->visit(visitor))
        {
            return false;
        }
    }
    return true;
}

wstring SubscriptDomainTerm::toString() const
{
    wstring out;
    out.sprintf(TEXT("%s[%s]"), array.getName(), subscriptTerm->toString().c_str());
    return out;
}

unique_ptr<Term> SubscriptDomainTerm::clone() const
{
    auto clonedSubscript = static_cast<LiteralTerm*>(subscriptTerm->clone().detach());
    return make_unique<SubscriptDomainTerm>(array, ULiteralTerm(move(clonedSubscript)));
}

void SubscriptDomainTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
{
    if (!maybeReplaceChild(subscriptTerm, visitor))
    {
        subscriptTerm->replace(visitor);
    }
}

bool SubscriptDomainTerm::containsAbstracts() const
{
    return subscriptTerm->containsAbstracts();
}

void SubscriptDomainTerm::eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ProgramSymbol subscriptSym = subscriptTerm->eval(overrideMap, boundVertex);
    vxy_assert_msg(subscriptSym.isValid(), "Failed to evaluate subscript term");
    vxy_assert_msg(subscriptSym.isInteger(), "Subscript term did not evaluate to an integer");
    vxy_assert_msg(subscriptSym.getInt() >= 0 && subscriptSym.getInt() < array.getNumValues(), "Evaluated subscript out of range");

    ValueSet evaluatedMask(array.getDescriptor()->getDomainSize(), false);
    evaluatedMask[array.getFirstValueIndex() + subscriptSym.getInt()] = true;

    inOutMask.intersect(evaluatedMask);
}

bool SubscriptDomainTerm::match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const
{
    ProgramSymbol subscriptSym = subscriptTerm->eval(overrideMap, boundVertex);
    if (!subscriptSym.isInteger())
    {
        return false;
    }

    if (subscriptSym.getInt() < 0 || subscriptSym.getInt() >= array.getNumValues())
    {
        return false;
    }

    return mask[array.getFirstValueIndex() + subscriptSym.getInt()];
}

size_t SubscriptDomainTerm::hash() const
{
    return combineHashes(eastl::hash<const wchar_t*>()(array.getName()), subscriptTerm->hash());
}

UnionDomainTerm::UnionDomainTerm(UDomainTerm&& left, UDomainTerm&& right)
    : left(move(left))
    , right(move(right))
{    
}

bool UnionDomainTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    auto resp = visitor(this);
    if (resp == ETopologySearchResponse::Abort)
    {
        return false;
    }
    else if (resp != ETopologySearchResponse::Skip)
    {
        if (!left->visit(visitor))
        {
            return false;
        }
        if (!right->visit(visitor))
        {
            return false;
        }
    }
    return true;
}

wstring UnionDomainTerm::toString() const
{
    wstring out;
    out.sprintf(TEXT("%s | %s"), left->toString().c_str(), right->toString().c_str());
    return out;
}

unique_ptr<Term> UnionDomainTerm::clone() const
{
    auto clonedLeft = static_cast<DomainTerm*>(left->clone().detach());
    auto clonedRight = static_cast<DomainTerm*>(right->clone().detach());
    return make_unique<UnionDomainTerm>(UDomainTerm(move(clonedLeft)), UDomainTerm(move(clonedRight)));
}

void UnionDomainTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
{
    if (!maybeReplaceChild(left, visitor))
    {
        left->replace(visitor);
    }
    if (!maybeReplaceChild(right, visitor))
    {
        right->replace(visitor);
    }
}

bool UnionDomainTerm::containsAbstracts() const
{
    return left->containsAbstracts() || right->containsAbstracts();
}

void UnionDomainTerm::eval(ValueSet& inOutMask, const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ValueSet leftIntersection = inOutMask;
    left->eval(leftIntersection, overrideMap, boundVertex);    
    right->eval(inOutMask, overrideMap, boundVertex);
    inOutMask.include(leftIntersection); 
}

bool UnionDomainTerm::match(const ValueSet& mask, AbstractOverrideMap& overrideMap, ProgramSymbol& boundVertex) const
{
    bool leftMatched = left->match(mask, overrideMap, boundVertex);
    bool rightMatched = right->match(mask, overrideMap, boundVertex);

    if (leftMatched)
    {
        ValueSet leftMask = mask;    
        left->eval(leftMask, overrideMap, boundVertex);
        if (!leftMask.isZero())
        {
            return true;
        }
    }

    if (rightMatched)
    {
        ValueSet rightMask = mask;
        right->eval(rightMask, overrideMap, boundVertex);
        if (!rightMask.isZero())
        {
            return true;
        }
    }

    return false;
}

size_t UnionDomainTerm::hash() const
{
    return combineHashes(left->hash(), right->hash());
}

FunctionHeadTerm::FunctionHeadTerm(FormulaUID inUID, const wchar_t* inName, int inDomainSize, vector<ULiteralTerm>&& inArgs, vector<UDomainTerm>&& inDomainTerms)
    : functionUID(inUID)
    , functionName(inName)
    , domainSize(inDomainSize)
    , arguments(move(inArgs))
    , domainTerms(move(inDomainTerms))
{
    vxy_assert(domainSize >= 1);
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

    ValueSet mask = getDomain(overrideMap, boundVertex);
    if (mask.isZero())
    {
        vxy_fail_msg("Unexpected empty mask");
        return {};
    }
    return ProgramSymbol(functionUID, functionName, resolvedArgs, mask, false);  
}

ValueSet FunctionHeadTerm::getDomain(const AbstractOverrideMap& overrideMap, const ProgramSymbol& boundVertex) const
{
    ValueSet mask(domainSize, true);
    for (auto& domainTerm : domainTerms)
    {
        domainTerm->eval(mask, overrideMap, boundVertex);
    }
    return mask;
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
        for (auto& dterm : domainTerms)
        {
            if (!dterm->visit(visitor))
            {
                return false;
            }
        }
    }
    return true;
}

void FunctionHeadTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
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
    
    vector<UDomainTerm> clonedDomain;
    clonedDomain.reserve(domainTerms.size());
    for (auto& domainTerm : domainTerms)
    {
        auto cloned = static_cast<DomainTerm*>(domainTerm->clone().detach());
        clonedDomain.push_back(unique_ptr<DomainTerm>(move(cloned)));
    }    
    return make_unique<FunctionHeadTerm>(functionUID, functionName, domainSize, move(clonedArgs), move(clonedDomain));
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

void DisjunctionTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
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

void ChoiceTerm::replace(const function<unique_ptr<Term>(Term*)>& visitor)
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
