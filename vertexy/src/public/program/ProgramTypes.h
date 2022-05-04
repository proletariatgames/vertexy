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
    const wchar_t* getName() const { return m_name; }

    bool operator==(const ProgramVariable& rhs) const
    {
        return m_uid == rhs.m_uid;
    }

private:
    const wchar_t* m_name;
    VariableUID m_uid = VariableUID(0);
};

enum class ESymbolType : uint8_t
{
    Integer = 0,
    ID,
    Formula,
    Abstract,
    External,
    Invalid
};

class ConstantFormula;
class ProgramSymbol;

class IExternalFormulaProvider
{
public:
    virtual ~IExternalFormulaProvider() {}
    // Return whether the given atom exists.
    virtual bool instantiate(const vector<ProgramSymbol>& args) const = 0;
    virtual size_t hash() const = 0;
};

using IExternalFormulaProviderPtr = shared_ptr<IExternalFormulaProvider>;


// Represents a constant value in a rule program: either an integer, a string ID, or a grounded formula call.
// Internally it is represented as a tagged pointer.
class ProgramSymbol
{
public:
    ProgramSymbol()
    {
        m_packed = encode(ESymbolType::Invalid, 0);
    }

    ProgramSymbol(const IGraphRelationPtr<int>& relation)
    {
        m_packed = encode(ESymbolType::Abstract, 0);
    }

    ProgramSymbol(int32_t constant)
    {
        m_packed = encode(ESymbolType::Integer, constant);
    }
    ProgramSymbol(const wchar_t* name)
    {
        m_packed = encode(ESymbolType::ID, reinterpret_cast<intptr_t>(name));
    }

    ProgramSymbol(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);
    ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);

    explicit ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& relation=nullptr);

    ~ProgramSymbol();

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
        vxy_sanity(isFormula());
        return reinterpret_cast<ConstantFormula*>(decode(m_packed));
    }

    bool isFormula() const
    {
        return getType() == ESymbolType::Formula || getType() == ESymbolType::External;
    }

    bool isNegated() const
    {
        vxy_sanity(isFormula());
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

    ProgramSymbol makeConcrete(int vertex) const;

    ProgramSymbol negatedFormula() const;

    ProgramSymbol absolute() const
    {
        return isNegated() ? negatedFormula() : *this;
    }

    const IGraphRelationPtr<int>& getAbstractRelation() const
    {
        vxy_assert(getType() == ESymbolType::Abstract);
        return *reinterpret_cast<const IGraphRelationPtr<int>*>(m_smartPtrBytes);
    }

    const IExternalFormulaProviderPtr& getExternalFormulaProvider() const
    {
        if (getType() == ESymbolType::Formula)
        {
            const static IExternalFormulaProviderPtr nullPtr = nullptr;
            return nullPtr;
        }

        vxy_assert(getType() == ESymbolType::External);
        return *reinterpret_cast<const IExternalFormulaProviderPtr*>(m_smartPtrBytes);
    }

    uint32_t hash() const
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

    bool operator==(const ProgramSymbol& rhs) const
    {
        if (this == &rhs) { return true; }
        if (rhs.getType() != getType()) { return false; }

        switch (getType())
        {
        case ESymbolType::Abstract:
            return getAbstractRelation()->equals(*rhs.getAbstractRelation());
        case ESymbolType::External:
            return getExternalFormulaProvider() == rhs.getExternalFormulaProvider();
        default:
            return m_packed == rhs.m_packed;
        }
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

    void setExternalProvider(const IExternalFormulaProviderPtr& provider);
    void setAbstractRelation(const IGraphRelationPtr<int>& relation);

    IExternalFormulaProviderPtr* getExternalFormulaProviderPtr();
    IGraphRelationPtr<int>* getAbstractRelationPtr();

    uint64_t m_packed;
    uint8_t m_smartPtrBytes[sizeof(std::shared_ptr<int>)];
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
        size_t hash;
        if (ConstantFormula* existing = getExisting(formula, name, args, hash))
        {
            return existing;
        }

        s_formulas.push_back(unique_ptr<ConstantFormula>(new ConstantFormula(formula, name, args)));
        s_lookup.insert(hash, nullptr, s_formulas.back().get());
        return s_formulas.back().get();
    }

    static ConstantFormula* get(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args)
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
    static ConstantFormula* getExisting(FormulaUID formula, const wchar_t* name, const vector<ProgramSymbol>& args, size_t& outHash)
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

inline ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* formulaName, const vector<ProgramSymbol>& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, formulaName, args);
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}

inline ProgramSymbol::ProgramSymbol(FormulaUID formula, const wchar_t* name, vector<ProgramSymbol>&& args, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    ConstantFormula* f = ConstantFormula::get(formula, name, move(args));
    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(f)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == f);
}


inline ProgramSymbol::ProgramSymbol(const ConstantFormula* formula, bool negated, const IExternalFormulaProviderPtr& provider)
{
    setExternalProvider(provider);

    m_packed = encode(
        provider != nullptr ? ESymbolType::External : ESymbolType::Formula,
        reinterpret_cast<intptr_t>(formula)
    ) | (negated ? 0 : 1);
    vxy_sanity(reinterpret_cast<ConstantFormula*>(decode(m_packed)) == formula);
}

inline ProgramSymbol::~ProgramSymbol()
{
    switch (getType())
    {
    case ESymbolType::Formula:
        {
            IExternalFormulaProviderPtr* provider = getExternalFormulaProviderPtr();
            (*provider).~IExternalFormulaProviderPtr();
        }
        break;
    case ESymbolType::External:
        {
            IGraphRelationPtr<int>* relation = getAbstractRelationPtr();
            (*relation).~IGraphRelationPtr<int>();
        }
        break;

    default:
        // No need to do anything
        break;
    }
}

inline ProgramSymbol ProgramSymbol::negatedFormula() const
{
    return ProgramSymbol(getFormula(), !isNegated(), getExternalFormulaProvider());
}

inline wstring ProgramSymbol::toString() const
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
        return {wstring::CtorSprintf(), TEXT("Abstract(%s)"), getAbstractRelation()->toString().c_str()};
    case ESymbolType::Invalid:
        return TEXT("<Invalid>");
    default:
        vxy_fail_msg("unexpected symbol type");
        return TEXT("<unexpected>");
    }
}

inline void ProgramSymbol::setExternalProvider(const IExternalFormulaProviderPtr& provider)
{
    new(m_smartPtrBytes) IExternalFormulaProviderPtr(provider);
}

inline void ProgramSymbol::setAbstractRelation(const IGraphRelationPtr<int>& relation)
{
    new(m_smartPtrBytes) IGraphRelationPtr<int>(relation);
}

inline IGraphRelationPtr<int>* ProgramSymbol::getAbstractRelationPtr()
{
    return reinterpret_cast<IGraphRelationPtr<int>*>(m_smartPtrBytes);
}

inline IExternalFormulaProviderPtr* ProgramSymbol::getExternalFormulaProviderPtr()
{
    return reinterpret_cast<IExternalFormulaProviderPtr*>(m_smartPtrBytes);
}

inline ProgramSymbol ProgramSymbol::makeConcrete(int vertex) const
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
                concreteArgs.push_back(arg.makeConcrete(vertex));
            }

            return ProgramSymbol(sig->uid, sig->name, move(concreteArgs), isNegated());
        }

    case ESymbolType::External:
        {
            auto sig = getFormula();

            vector<ProgramSymbol> concreteArgs;
            concreteArgs.reserve(sig->args.size());
            for (auto& arg : sig->args)
            {
                concreteArgs.push_back(arg.makeConcrete(vertex));
            }

            if (getExternalFormulaProvider()->instantiate(concreteArgs))
            {
                return isNegated()
                    ? ProgramSymbol()
                    : ProgramSymbol(sig->uid, sig->name, move(concreteArgs), false);
            }
            else
            {
                return isNegated()
                    ? ProgramSymbol(sig->uid, sig->name, move(concreteArgs), true)
                    : ProgramSymbol();
            }
        }

    case ESymbolType::Invalid:
    default:
        vxy_fail_msg("Unexpected symbol type");
        return {};
    }
}

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

struct CompilerAtom
{
    ProgramSymbol symbol;
    bool isFact;
};

using VariableMap = hash_map<ProgramVariable, shared_ptr<ProgramSymbol>>;

} // namespace Vertexy

namespace eastl
{

// Hashing for ProgramVariable
template<>
struct hash<Vertexy::ProgramVariable>
{
    uint32_t operator()(const Vertexy::ProgramVariable& var) const
    {
        return hash<int>()(var.getID());
    }
};

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

