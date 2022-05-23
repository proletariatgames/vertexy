﻿// Copyright Proletariat, Inc. All Rights Reserved.

#include "program/ProgramSymbol.h"
#include "program/ExternalFormula.h"

using namespace Vertexy;

ProgramSymbol::ProgramSymbol()
{
    m_packed = encode(ESymbolType::Invalid, 0);
}

ProgramSymbol::ProgramSymbol(const GraphVertexRelationPtr& relation)
{
    m_packed = encode(ESymbolType::Abstract, 0);
    setAbstractRelation(relation);
}

ProgramSymbol::ProgramSymbol(int32_t constant)
{
    m_packed = encode(ESymbolType::Integer, constant);
}

ProgramSymbol::ProgramSymbol(const wchar_t* name)
{
    m_packed = encode(ESymbolType::ID, reinterpret_cast<intptr_t>(name));
}

const GraphVertexRelationPtr& ProgramSymbol::getAbstractRelation() const
{
    vxy_assert(isAbstract());
    return *reinterpret_cast<const GraphVertexRelationPtr*>(m_smartPtrBytes);
}

const IExternalFormulaProviderPtr& ProgramSymbol::getExternalFormulaProvider() const
{
    if (getType() == ESymbolType::Formula)
    {
        const static IExternalFormulaProviderPtr nullPtr = nullptr;
        return nullPtr;
    }

    vxy_assert(getType() == ESymbolType::External);
    return *reinterpret_cast<const IExternalFormulaProviderPtr*>(m_smartPtrBytes);
}

uint32_t ProgramSymbol::hash() const
{
    switch (getType())
    {
    case ESymbolType::Abstract:
        return getAbstractRelation()->hash();
    case ESymbolType::External:
        return combineHashes(eastl::hash<uint64_t>()(m_packed), getExternalFormulaProvider()->hash());
    default:
        return eastl::hash<uint64_t>()(m_packed);
    }
}

bool ProgramSymbol::operator==(const ProgramSymbol& rhs) const
{
    if (this == &rhs) { return true; }
    if (rhs.getType() != getType()) { return false; }

    switch (getType())
    {
    case ESymbolType::Abstract:
        return getAbstractRelation()->equals(*rhs.getAbstractRelation());
    case ESymbolType::External:
        return m_packed == rhs.m_packed && getExternalFormulaProvider() == rhs.getExternalFormulaProvider();
    default:
        return m_packed == rhs.m_packed;
    }
}


ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, formulaName, args);
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, name, move(args));
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

ProgramSymbol::ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(formula)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == formula);
}

ProgramSymbol::ProgramSymbol(const ProgramSymbol& other)
    : m_packed(other.m_packed)
{
    if (other.isExternalFormula())
    {
        setExternalProvider(other.getExternalFormulaProvider());
    }
    else if (other.isAbstract())
    {
        setAbstractRelation(other.getAbstractRelation());
    }
}

ProgramSymbol::ProgramSymbol(ProgramSymbol&& other) noexcept
    : m_packed(other.m_packed)
{
    if (other.isExternalFormula())
    {
        setExternalProvider(move(*other.getExternalFormulaProviderPtr()));
    }
    else if (other.isAbstract())
    {
        setAbstractRelation(move(*other.getAbstractRelationPtr()));
    }
}

ProgramSymbol::~ProgramSymbol()
{
    destroySmartPointer();
}

void ProgramSymbol::destroySmartPointer()
{
    switch (getType())
    {
    case ESymbolType::External:
        {
            IExternalFormulaProviderPtr* provider = getExternalFormulaProviderPtr();
            (*provider).~IExternalFormulaProviderPtr();
        }
        break;
    case ESymbolType::Abstract:
        {
            GraphVertexRelationPtr* relation = getAbstractRelationPtr();
            (*relation).~GraphVertexRelationPtr();
        }
        break;

    default:
        // No need to do anything
        break;
    }
}

ProgramSymbol& ProgramSymbol::operator=(const ProgramSymbol& rhs)
{
    if (&rhs == this)
    {
        return *this;
    }
    
    destroySmartPointer();

    m_packed = rhs.m_packed;
    if (isExternalFormula())
    {
        setExternalProvider(rhs.getExternalFormulaProvider());
    }
    else if (isAbstract())
    {
        setAbstractRelation(rhs.getAbstractRelation());
    }
    return *this;
}

ProgramSymbol& ProgramSymbol::operator=(ProgramSymbol&& rhs) noexcept
{
    if (&rhs == this)
    {
        return *this;
    }

    destroySmartPointer();

    m_packed = rhs.m_packed;
    if (isExternalFormula())
    {
        setExternalProvider(move(*rhs.getExternalFormulaProviderPtr()));
    }
    else if (isAbstract())
    {
        setAbstractRelation(move(*rhs.getAbstractRelationPtr()));
    }
    return *this;
}

ProgramSymbol ProgramSymbol::negatedFormula() const
{
    return ProgramSymbol(getFormula(), !isNegated(), getExternalFormulaProvider());
}

ProgramSymbol ProgramSymbol::absolute() const
{
    return isNegated() ? negatedFormula() : *this;
}

bool ProgramSymbol::isNegated() const
{
    return isFormula() ? ((m_packed & 1) == 0) : false;
}

bool ProgramSymbol::containsAbstract() const
{
    switch (getType())
    {
    case ESymbolType::Abstract:
        return true;

    case ESymbolType::Formula:
    case ESymbolType::External:
        for (auto& arg : getFormula()->args)
        {
            if (arg.containsAbstract())
            {
                return true;
            }
        }
        return false;

    case ESymbolType::Integer:
    case ESymbolType::ID:
    case ESymbolType::Invalid:
        return false;

    default:
        vxy_fail_msg("unexpected symbol type");
        return false;
    }
}

ProgramSymbol ProgramSymbol::makeConcrete(int vertex) const
{
    switch (getType())
    {
    case ESymbolType::Integer:
    case ESymbolType::ID:
        return *this;

    case ESymbolType::Abstract:
        {
            int destVertex;
            if (!getAbstractRelation()->getRelation(vertex, destVertex))
            {
                return {};
            }
            return ProgramSymbol(destVertex);
        }

    case ESymbolType::Formula:
        {
            auto sig = getFormula();

            vector<ProgramSymbol> concreteArgs;
            concreteArgs.reserve(sig->args.size());
            for (auto& arg : sig->args)
            {
                ProgramSymbol concreteArg = arg.makeConcrete(vertex);
                if (!concreteArg.isValid())
                {
                    return {};
                }
                concreteArgs.push_back(move(concreteArg));
            }

            return ProgramSymbol(sig->uid, sig->name.c_str(), move(concreteArgs), isNegated());
        }

    case ESymbolType::External:
        {
            auto sig = getFormula();
            vector<ProgramSymbol> concreteArgs;
            concreteArgs.reserve(sig->args.size());
            for (auto& arg : sig->args)
            {
                ProgramSymbol concreteArg = arg.makeConcrete(vertex);
                if (!concreteArg.isValid())
                {
                    return {};
                }
                concreteArgs.push_back(move(concreteArg));
            }

            bool valid = getExternalFormulaProvider()->eval(concreteArgs);
            if (valid == isNegated())
            {
                return {};
            }

            return ProgramSymbol(sig->uid, sig->name.c_str(), move(concreteArgs), isNegated());
        }
    case ESymbolType::Invalid:
    default:
        vxy_fail_msg("Unexpected symbol type");
        return {};
    }
}

wstring ProgramSymbol::toString() const
{
    switch (getType())
    {
    case ESymbolType::Formula:
    case ESymbolType::External:
        return isNegated() ? (TEXT("~") + getFormula()->toString()) : getFormula()->toString();
    case ESymbolType::Integer:
        return {wstring::CtorSprintf(), TEXT("%d"), getInt()};
    case ESymbolType::ID:
        return getID();
    case ESymbolType::Abstract:
        return {wstring::CtorSprintf(), TEXT("$(%s)"), getAbstractRelation()->toString().c_str()};
    case ESymbolType::Invalid:
        return TEXT("<Invalid>");
    default:
        vxy_fail_msg("unexpected symbol type");
        return TEXT("<unexpected>");
    }
}

void ProgramSymbol::setExternalProvider(const IExternalFormulaProviderPtr& provider)
{
    new(m_smartPtrBytes) IExternalFormulaProviderPtr(provider);
}

void ProgramSymbol::setAbstractRelation(const GraphVertexRelationPtr& relation)
{
    new(m_smartPtrBytes) GraphVertexRelationPtr(relation);
}

GraphVertexRelationPtr* ProgramSymbol::getAbstractRelationPtr()
{
    return reinterpret_cast<GraphVertexRelationPtr*>(m_smartPtrBytes);
}

IExternalFormulaProviderPtr* ProgramSymbol::getExternalFormulaProviderPtr()
{
    return reinterpret_cast<IExternalFormulaProviderPtr*>(m_smartPtrBytes);
}

ConstantFormula::ConstantFormula(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args): uid(formula)
    , name(formulaName)
    , args(args)
{
}

ConstantFormula* ConstantFormula::get(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args)
{
    size_t hash;
    if (ConstantFormula* existing = getExisting(formula, name, args, hash))
    {
        return existing;
    }

    s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, args)));
    s_lookup.insert(hash, nullptr, s_formulas.back().get());
    return s_formulas.back().get();
}

ConstantFormula* ConstantFormula::get(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args)
{
    size_t hash;
    if (ConstantFormula* existing = getExisting(formula, name, args, hash))
    {
        return existing;
    }

    s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, move(args))));
    s_lookup.insert(hash, nullptr, s_formulas.back().get());
    return s_formulas.back().get();
}

wstring ConstantFormula::toString() const
{
    wstring out = name;
    out.append(TEXT("("));

    bool first = true;
    for (auto& arg : args)
    {
        if (!first)
        {
            out.append(TEXT(", "));
        }
        first = false;
        out.append(arg.toString());
    }

    out.append(TEXT(")"));
    return out;
}

ConstantFormula* ConstantFormula::getExisting(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, size_t& outHash)
{
    outHash = makeHash(formula, args);
    auto range = s_lookup.find_range_by_hash(outHash);
    for (auto it = range.first; it != range.second; ++it)
    {
        if ((*it)->uid == formula)
        {
            vxy_assert((*it)->args.size() == args.size());
            bool match = true;
            for (int i = 0; i < args.size(); ++i)
            {
                if ((*it)->args[i] != args[i])
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                return *it;
            }
        }
    }
    return nullptr;
}

uint32_t ConstantFormula::makeHash(FormulaUID formula, const vector<ProgramSymbol>& args)
{
    uint32_t out = hash<int>()(formula);
    for (auto& arg : args)
    {
        out ^= arg.hash();
    }
    return out;
}

bool ConstantFormula::Hash::operator()(const ConstantFormula* consA, const ConstantFormula* consB) const
{
    if (consA == consB)
    {
        return true;
    }
    if (consA->uid != consB->uid || consA->args.size() != consB->args.size())
    {
        return false;
    }
    for (int i = 0; i < consA->args.size(); ++i)
    {
        if (consA->args[i] != consB->args[i])
        {
            return false;
        }
    }
    return true;
}

uint32_t ConstantFormula::Hash::operator()(const ConstantFormula* cons) const
{
    uint32_t out = hash<int>()(cons->uid);
    for (auto& arg : cons->args)
    {
        out ^= arg.hash();
    }
    return out;
}