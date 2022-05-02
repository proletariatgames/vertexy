// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once
#include "ConstraintTypes.h"
#include "topology/GraphRelations.h"

namespace Vertexy
{

class RuleStatement;
using URuleStatement = unique_ptr<RuleStatement>;

// unique ID to identify a ProgramVariable
enum VariableUID : int32_t { };
// unique ID to identify a named Formula
enum FormulaUID : int32_t { };

// Represents an ungrounded variable within a rule program
class ProgramVariable
{
    friend struct ProgramVariableHasher;
public:
    ProgramVariable(const wchar_t* name=nullptr);
    VariableUID getID() const { return m_uid; }
private:
    const wchar_t* m_name;
    VariableUID m_uid = VariableUID(0);
};

enum class ESymbolType : uint8_t
{
    Integer = 0,
    ID,
    Formula,
    Invalid
};

class ConstantFormula;

// Represents a constant value in a rule program: either an integer, a string ID, or a grounded formula call.
// Internally it is represented as a tagged pointer.
class ProgramSymbol
{
public:
    ProgramSymbol()
    {
        m_packed = encode(ESymbolType::Invalid, 0);
    }

    ProgramSymbol(int32_t constant, const IGraphRelationPtr<int>& relation=nullptr)
        : m_relation(relation)
    {
        m_packed = encode(ESymbolType::Integer, constant);
    }
    ProgramSymbol(const wchar_t* name)
    {
        m_packed = encode(ESymbolType::ID, reinterpret_cast<intptr_t>(name));
    }

    ProgramSymbol(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, bool negated);

    ESymbolType getType() const
    {
        return ESymbolType(m_packed >> 56);
    }

    int getInt() const
    {
        vxy_sanity(getType() == ESymbolType::Integer);
        return m_packed;
    }

    const wchar_t* getID() const
    {
        vxy_sanity(getType() == ESymbolType::ID);
        return reinterpret_cast<const wchar_t*>(decode(m_packed));
    }

    const ConstantFormula* getFormula() const
    {
        vxy_sanity(getType() == ESymbolType::Formula);
        return reinterpret_cast<ConstantFormula*>(decode(m_packed));
    }

    bool isNegated() const
    {
        vxy_sanity(getType() == ESymbolType::Formula);
        return (m_packed & 1) == 0;
    }

    bool isValid() const
    {
        return getType() != ESymbolType::Invalid;
    }

    bool isInvalid() const
    {
        return !isValid();
    }

    ProgramSymbol negatedFormula() const;

    ProgramSymbol absolute() const
    {
        return (getType() == ESymbolType::Formula && isNegated())
        ? negatedFormula()
        : *this;
    }

    const IGraphRelationPtr<int>& getRelation() const
    {
        return m_relation;
    }

    uint32_t hash() const
    {
        return eastl::hash<uint64_t>()(m_packed);
    }

    bool operator==(const ProgramSymbol& rhs) const
    {
        if (this == &rhs) { return true; }
        // TODO: handle relation equality?
        return (m_packed == rhs.m_packed && m_relation == rhs.m_relation);
    }
    bool operator!=(const ProgramSymbol& rhs) const
    {
        return !operator==(rhs);
    }

    wstring toString() const;

private:
    static uint64_t encode(ESymbolType type, intptr_t ptr)
    {
        return (static_cast<uint64_t>(type) << 56) | ptr;
    }
    static intptr_t decode(uint64_t packed)
    {
        return packed & 0x00FFFFFFFFFFFFFEULL;
    }

    uint64_t m_packed;
    IGraphRelationPtr<int> m_relation;
};

// Represents a unique grounded formula call.
class ConstantFormula
{
    /*private*/ ConstantFormula(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args)
        : uid(formula)
        , name(formulaName)
        , args(args)
    {
    }

public:
    FormulaUID uid;
    const wchar_t* name;
    vector<ProgramSymbol> args;

    static ConstantFormula* get(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args)
    {
        ConstantFormula temp(formula, name, args);

        uint32_t hash = makeHash(formula, args);
        auto it = s_lookup.find_by_hash(&temp, hash);
        if (it != s_lookup.end())
        {
            return *it;
        }

        s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, args)));
        s_lookup.insert(hash, nullptr, s_formulas.back().get());
        return s_formulas.back().get();
    }

    wstring toString() const
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

private:
    static uint32_t makeHash(FormulaUID formula, const vector<ProgramSymbol>& args)
    {
        uint32_t out = hash<int>()(formula);
        for (auto& arg : args)
        {
            out ^= arg.hash();
        }
        return out;
    }

    struct Hash
    {
        bool operator()(const ConstantFormula* consA, const ConstantFormula* consB) const
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

        uint32_t operator()(const ConstantFormula* cons) const
        {
            uint32_t out = hash<int>()(cons->uid);
            for (auto& arg : cons->args)
            {
                out ^= arg.hash();
            }
            return out;
        }
    };

    static hash_set<ConstantFormula*, Hash, Hash> s_lookup;
    static vector<unique_ptr<ConstantFormula>> s_formulas;
};

inline ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args, bool negated)
{
    ConstantFormula* f = ConstantFormula::get(formula, formulaName, args);
    m_packed = encode(ESymbolType::Formula, reinterpret_cast<intptr_t>(f)) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

inline ProgramSymbol ProgramSymbol::negatedFormula() const
{
    const ConstantFormula* f = getFormula();
    return ProgramSymbol(f->uid, f->name, f->args, !isNegated());
}

inline wstring ProgramSymbol::toString() const
{
    switch (getType())
    {
    case ESymbolType::Formula:
        return isNegated() ? (TEXT("~") + getFormula()->toString()) : getFormula()->toString();
    case ESymbolType::Integer:
        return {wstring::CtorSprintf(), TEXT("%d"), getInt()};
    case ESymbolType::ID:
        return getID();
    case ESymbolType::Invalid:
        return TEXT("<Invalid>");
    default:
        vxy_fail_msg("unexpected symbol type");
        return TEXT("<unexpected>");
    }
}

class IExternalFormulaProvider
{
public:
    virtual ~IExternalFormulaProvider() {}
    // create the positive instantiations of this formula, given the supplied arguments.
    // at least one argument will be invalid: this should be treated as a wildcard.
    virtual vector<ProgramSymbol> instantiate(const vector<ProgramSymbol>& args) const = 0;
};

using IExternalFormulaProviderPtr = shared_ptr<IExternalFormulaProvider>;

// Base class for instantiating all values of a particular literal in a program rule
class Instantiator
{
public:
    virtual ~Instantiator() {}
    // Find the first match/reset to first match
    virtual void first() = 0;
    // Find the next match
    virtual void match() = 0;
    // Whether we've run out of matches
    virtual bool hitEnd() const = 0;
};

struct ProgramVariableHasher
{
    uint32_t operator()(const ProgramVariable& var) const
    {
        return hash<int>()(var.m_uid);
    }

    bool operator()(const ProgramVariable& lhs, const ProgramVariable& rhs) const
    {
        return lhs.m_uid == rhs.m_uid;
    }
};

struct CompilerAtom
{
    ProgramSymbol symbol;
    bool isFact;
};

using VariableMap = hash_map<ProgramVariable, shared_ptr<ProgramSymbol>, ProgramVariableHasher, ProgramVariableHasher>;

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

// Hashing for CompilerAtom
template<>
struct hash<Vertexy::CompilerAtom>
{
    inline size_t operator()(const Vertexy::CompilerAtom& atom) const
    {
        return atom.symbol.hash();
    }
};

}

