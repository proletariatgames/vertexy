﻿// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ProgramTypes.h"
#include <EASTL/hash_set.h>

namespace Vertexy
{

enum class ESymbolType : uint8_t
{
    PositiveInteger = 0,
    NegativeInteger,
    ID,
    Formula,
    External,
    Abstract,
    Invalid
};

class ConstantFormula;
class IExternalFormulaProvider;

using IExternalFormulaProviderPtr = shared_ptr<IExternalFormulaProvider>;

// Represents a constant value in a rule program: either an integer, a string ID, or a grounded formula call.
// Internally it is represented as a tagged pointer.
class ProgramSymbol
{
public:
    ProgramSymbol();
    ProgramSymbol(const ProgramSymbol& other);
    ProgramSymbol(ProgramSymbol&& other) noexcept;

    explicit ProgramSymbol(const GraphVertexRelationPtr& relation);
    
    ProgramSymbol(int32_t constant);
    ProgramSymbol(const wchar_t* name);

    ProgramSymbol(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, const ValueSet& mask, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);
    ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, const ValueSet& mask, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);
    ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);

    ProgramSymbol& operator=(const ProgramSymbol& rhs);
    ProgramSymbol& operator=(ProgramSymbol&& rhs) noexcept;

    ~ProgramSymbol();

    ESymbolType getType() const
    {
        return ESymbolType(m_packed >> 56);
    }

    int getInt() const
    {
        vxy_sanity(isInteger());
        return getType() == ESymbolType::PositiveInteger ? m_packed : -decode(m_packed);
    }

    const wchar_t* getID() const
    {
        vxy_sanity(getType() == ESymbolType::ID);
        return reinterpret_cast<const wchar_t*>(decode(m_packed));
    }

    const ConstantFormula* getFormula() const
    {
        vxy_sanity(isFormula());
        return reinterpret_cast<ConstantFormula*>(decode(m_packed));
    }

    bool isAbstract() const { return getType() == ESymbolType::Abstract; }
    bool isInteger() const { return getType() == ESymbolType::PositiveInteger || getType() == ESymbolType::NegativeInteger; }
    bool isID() const { return getType() == ESymbolType::ID; }
    bool isExternalFormula() const { return getType() == ESymbolType::External; }
    bool isNormalFormula() const { return getType() == ESymbolType::Formula; }
    bool isFormula() const { return isNormalFormula() || isExternalFormula(); }

    bool isPositive() const { return !isNegated(); }    
    bool isNegated() const;
    bool isValid() const { return getType() != ESymbolType::Invalid; }
    bool isInvalid() const { return !isValid(); }

    bool containsAbstract() const;
    ProgramSymbol makeConcrete(int vertex) const;

    ProgramSymbol negatedFormula() const;
    ProgramSymbol absolute() const;

    ProgramSymbol unmasked() const;
    ProgramSymbol withIncludedMask(const ValueSet& mask) const;

    const GraphVertexRelationPtr& getAbstractRelation() const;
    const IExternalFormulaProviderPtr& getExternalFormulaProvider() const;

    uint32_t hash() const;

    bool operator==(const ProgramSymbol& rhs) const;
    bool operator!=(const ProgramSymbol& rhs) const { return !operator==(rhs); }

    wstring toString() const;

private:
    void destroySmartPointer();

    static uint64_t encode(ESymbolType type, intptr_t ptr)
    {
        return (static_cast<uint64_t>(type) << 56) | ptr;
    }
    static intptr_t decode(uint64_t packed)
    {
        return packed & 0x00FFFFFFFFFFFFFEULL;
    }

    void setExternalProvider(const IExternalFormulaProviderPtr& provider);
    void setAbstractRelation(const GraphVertexRelationPtr& relation);

    IExternalFormulaProviderPtr* getExternalFormulaProviderPtr();
    GraphVertexRelationPtr* getAbstractRelationPtr();

    uint64_t m_packed;
    uint64_t m_smartPtrBytes[sizeof(shared_ptr<int>)/sizeof(uint64_t)] {};
};

// Represents a unique grounded formula call.
class ConstantFormula
{
    /*private*/ ConstantFormula(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args, const ValueSet& mask, size_t hash);

public:
    FormulaUID uid;
    wstring name;
    vector<ProgramSymbol> args;
    ValueSet mask;

    static const ConstantFormula* get(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, const ValueSet& mask);
    static const ConstantFormula* get(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, const ValueSet& mask);

    wstring toString() const;
    size_t hash() const { return m_hash; }

private:
    size_t m_hash;
    
    static const ConstantFormula* getExisting(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, const ValueSet& mask, size_t& outHash);
    static uint32_t makeHash(FormulaUID formula, const vector<ProgramSymbol>& args, const ValueSet& mask);

    struct Hash
    {
        bool operator()(const ConstantFormula* consA, const ConstantFormula* consB) const;
        uint32_t operator()(const ConstantFormula* cons) const;
    };

    static hash_set<ConstantFormula*, Hash, Hash> s_lookup;
    static vector<unique_ptr<ConstantFormula>> s_formulas;
};

struct CompilerAtom
{
    ProgramSymbol symbol;
    ValueSet facts;
};

} // namespace Vertexy

namespace eastl
{

// Hashing for ProgramSymbol
template<>
struct hash<Vertexy::ProgramSymbol>
{
    inline size_t operator()(const Vertexy::ProgramSymbol& lit) const
    {
        return lit.hash();
    }
};

} // namespace eastl