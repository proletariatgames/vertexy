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

UInstantiator LiteralTerm::instantiate(ProgramCompiler& compiler)
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

bool LiteralTerm::match(const ProgramSymbol& sym, bool isFact)
{
    ProgramSymbol evalSym = eval();
    if (evalSym.isValid() && sym == evalSym)
    {
        assignedAtom = {sym, isFact};
        return true;
    }

    return false;
}

wstring LiteralTerm::toString() const
{
    return eval().toString();
}

VariableTerm::VariableTerm(ProgramVariable param)
    : var(param)
{
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

bool VariableTerm::match(const ProgramSymbol& sym, bool isFact)
{
    if (isBinder)
    {
        // if this is the term where the variable first appears in, we take on whatever symbol was handed to us.
        // The ProgramSymbol pointed to by sharedBoundRef is shared by all other VariableTerms for the same variable in the
        // rule's body.
        *sharedBoundRef = sym;
        assignedAtom = {sym, isFact};
        return true;
    }
    else
    {
        // Otherwise this is a variable that was already bound earlier, so we just check for equality.
        if (*sharedBoundRef == sym)
        {
            assignedAtom = {sym, isFact};
            return true;
        }
        return false;
    }
}

bool SymbolTerm::visit(const function<EVisitResponse(const Term*)>& visitor) const
{
    return visitor(this) != EVisitResponse::Abort;
}

UTerm SymbolTerm::clone() const
{
    return make_unique<SymbolTerm>(sym);
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

FunctionTerm::FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, vector<ULiteralTerm>&& arguments, bool negated)
    : functionUID(functionUID)
    , functionName(functionName)
    , arguments(move(arguments))
    , negated(negated)
{
}

void FunctionTerm::collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const
{
    return LiteralTerm::collectVars(outVars, canEstablish && !negated);
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

ProgramSymbol FunctionTerm::eval() const
{
    vector<ProgramSymbol> resolvedArgs;
    resolvedArgs.reserve(arguments.size());
    for (auto& arg : arguments)
    {
        ProgramSymbol argSym = arg->eval();
        if (argSym.isInvalid())
        {
            return {};
        }
        resolvedArgs.push_back(argSym);
    }
    return ProgramSymbol(functionUID, functionName, resolvedArgs, negated);
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
    return make_unique<FunctionTerm>(functionUID, functionName, move(clonedArgs), negated);
}

wstring FunctionTerm::toString() const
{
    wstring out = LiteralTerm::toString();
    if (negated)
    {
        out = TEXT("~") + out;
    }
    return out;
}

ExternalFunctionTerm::ExternalFunctionTerm(const shared_ptr<IExternalFormulaProvider>& provider, vector<ULiteralTerm>&& arguments, bool negated)
    : FunctionTerm(FormulaUID(-1), nullptr, move(arguments), negated)
    , provider(provider)
{
}

UTerm ExternalFunctionTerm::clone() const
{
    vector<ULiteralTerm> clonedArgs;
    clonedArgs.reserve(arguments.size());
    for (auto& arg : arguments)
    {
        auto cloned = static_cast<LiteralTerm*>(arg->clone().detach());
        clonedArgs.push_back(unique_ptr<LiteralTerm>(move(cloned)));
    }
    return make_unique<ExternalFunctionTerm>(provider, move(clonedArgs), negated);
}

UInstantiator FunctionTerm::instantiate(ProgramCompiler& compiler)
{
    return make_unique<FunctionInstantiator>(*this, compiler.getDomain(functionUID));
}

bool FunctionTerm::match(const ProgramSymbol& sym, bool isFact)
{
    if (sym.getType() != ESymbolType::Formula)
    {
        return false;
    }

    const ConstantFormula* cformula = sym.getFormula();
    vxy_assert(cformula->args.size() == arguments.size());

    for (int i = 0; i < arguments.size(); ++i)
    {
        if (!arguments[i]->match(cformula->args[i], isFact))
        {
            return false;
        }
    }

    assignedAtom = {sym, isFact};
    return true;
}

ProgramSymbol UnaryOpTerm::eval() const
{
    auto sym = child->eval();
    if (sym.isInvalid())
    {
        return {};
    }

    vxy_assert_msg(sym.getType() == ESymbolType::Integer, "expected integer term");

    switch (op)
    {
    case EUnaryOperatorType::Negate:
        return -sym.getInt();
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

ProgramSymbol BinaryOpTerm::eval() const
{
    ProgramSymbol resolvedLHS = lhs->eval();
    if (resolvedLHS.isInvalid())
    {
        return {};
    }
    vxy_assert_msg(resolvedLHS.getType() == ESymbolType::Integer, "can only apply binary operators on integer symbols");

    ProgramSymbol resolvedRHS = rhs->eval();
    if (resolvedRHS.isInvalid())
    {
        return {};
    }
    vxy_assert_msg(resolvedRHS.getType() == ESymbolType::Integer, "can only apply binary operators on integer symbols");

    IGraphRelationPtr<int> rel;
    if (resolvedLHS.getRelation() != nullptr && resolvedRHS.getRelation() != nullptr)
    {
        rel = make_shared<BinOpGraphRelation>(resolvedLHS.getRelation(), resolvedRHS.getRelation(), op);
    }
    switch (op)
    {
    case EBinaryOperatorType::Add:
        return ProgramSymbol(resolvedLHS.getInt() + resolvedRHS.getInt(), rel);
    case EBinaryOperatorType::Subtract:
        return ProgramSymbol(resolvedLHS.getInt() - resolvedRHS.getInt(), rel);
    case EBinaryOperatorType::Multiply:
        return ProgramSymbol(resolvedLHS.getInt() * resolvedRHS.getInt(), rel);
    case EBinaryOperatorType::Divide:
        return ProgramSymbol(resolvedLHS.getInt() / resolvedRHS.getInt(), rel);
    case EBinaryOperatorType::Equality:
        return ProgramSymbol(resolvedLHS.getInt() == resolvedRHS.getInt() ? 1 : 0, rel);
    case EBinaryOperatorType::Inequality:
        return ProgramSymbol(resolvedLHS.getInt() != resolvedRHS.getInt() ? 1 :0, rel);
    case EBinaryOperatorType::LessThan:
        return ProgramSymbol(resolvedLHS.getInt() < resolvedRHS.getInt() ? 1 : 0, rel);
    case EBinaryOperatorType::LessThanEq:
        return ProgramSymbol(resolvedLHS.getInt() <= resolvedRHS.getInt() ? 1 : 0, rel);
    case EBinaryOperatorType::GreaterThan:
        return ProgramSymbol(resolvedLHS.getInt() > resolvedRHS.getInt() ? 1 : 0, rel);
    case EBinaryOperatorType::GreaterThanEq:
        return ProgramSymbol(resolvedLHS.getInt() >= resolvedRHS.getInt() ? 1 : 0, rel);
    default:
        vxy_fail_msg("unrecognized operator");
        return ProgramSymbol();
    }
}

UTerm BinaryOpTerm::clone() const
{
    auto clonedLHS = static_cast<LiteralTerm*>(lhs->clone().detach());
    auto clonedRHS = static_cast<LiteralTerm*>(lhs->clone().detach());
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

UInstantiator BinaryOpTerm::instantiate(ProgramCompiler& compiler)
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

ProgramSymbol FunctionHeadTerm::evalSingle()
{
    vector<ProgramSymbol> resolvedArgs;
    for (auto& arg : arguments)
    {
        ProgramSymbol argSym = arg->eval();
        if (argSym.isInvalid())
        {
            vxy_fail_msg("Expected a valid argument for head term");
            return {};
        }
        resolvedArgs.push_back(argSym);
    }

    return { ProgramSymbol(functionUID, functionName, resolvedArgs, false) };
}

vector<ProgramSymbol> FunctionHeadTerm::eval(bool& isNormalRule)
{
    isNormalRule = true;
    return {evalSingle()};
}

AtomID FunctionHeadTerm::getOrCreateAtom(RuleDatabase& rdb, hash_map<ProgramSymbol, AtomID>& atomMap)
{
    ProgramSymbol symbol = evalSingle();
    if (!symbol.isValid())
    {
        return {};
    }

    vxy_assert(!symbol.isNegated());
    auto found = atomMap.find(symbol);
    if (found == atomMap.end())
    {
        found = atomMap.insert({symbol, rdb.createAtom(symbol.toString().c_str())}).first;
    }
    return found->second;
}

FunctionHeadTerm::FunctionHeadTerm(FormulaUID functionUID, const wchar_t* functionName, vector<ULiteralTerm>&& arguments)
    : functionUID(functionUID)
    , functionName(functionName)
    , arguments(move(arguments))
{
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

TRuleHead<AtomID> FunctionHeadTerm::createHead(RuleDatabase& rdb, hash_map<ProgramSymbol, AtomID>& atomMap)
{
    return TRuleHead(getOrCreateAtom(rdb, atomMap));
}

wstring FunctionHeadTerm::toString() const
{
    ProgramSymbol sym = const_cast<FunctionHeadTerm*>(this)->evalSingle();
    return sym.toString();
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

vector<ProgramSymbol> DisjunctionTerm::eval(bool& isNormalRule)
{
    isNormalRule = false;

    vector<ProgramSymbol> out;
    out.reserve(children.size());

    for (auto& child : children)
    {
        bool ignored;
        ProgramSymbol childSym = child->evalSingle();
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

        out.append(child->evalSingle().toString());
    }
    out.append(TEXT(")"));
    return out;
}

TRuleHead<AtomID> DisjunctionTerm::createHead(RuleDatabase& rdb, hash_map<ProgramSymbol, AtomID>& atomMap)
{
    vector<AtomID> headAtoms;
    headAtoms.reserve(children.size());
    for (auto& child : children)
    {
        headAtoms.push_back(child->getOrCreateAtom(rdb, atomMap));
    }
    return TRuleHead(headAtoms, ERuleHeadType::Disjunction);
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

vector<ProgramSymbol> ChoiceTerm::eval(bool& isNormalRule)
{
    isNormalRule = false;
    return {subTerm->evalSingle()};
}

wstring ChoiceTerm::toString() const
{
    wstring out;
    out.sprintf(TEXT("choice(%s)"), subTerm->evalSingle().toString().c_str());
    return out;
}

TRuleHead<AtomID> ChoiceTerm::createHead(RuleDatabase& rdb, hash_map<ProgramSymbol, AtomID>& atomMap)
{
    return TRuleHead(subTerm->getOrCreateAtom(rdb, atomMap), ERuleHeadType::Choice);
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
