// Copyright Proletariat, Inc. All Rights Reserved.

#pragma once

#include "ProgramTypes.h"
#include "rules/RuleTypes.h"

namespace Vertexy
{

class RuleDatabase;
class ProgramCompiler;

class Instantiator;
using UInstantiator = unique_ptr<Instantiator>;

class VariableTerm;

class Term
{
public:
    using EVisitResponse = ETopologySearchResponse;

    virtual ~Term() {}

    void forChildren(const function<void(const Term*)>& visitor) const;
    void visit(const function<void(const Term*)>& visitor) const;

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const = 0;
    virtual void collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish = true) const;
    virtual wstring toString() const = 0;

    template<typename T>
    bool contains()
    {
        bool contained = false;
        visit([&](const Term* t)
        {
           if (t != this && dynamic_cast<const T*>(t))
           {
               contained = true;
               return EVisitResponse::Abort;
           }
            return EVisitResponse::Continue;
        });
        return contained;
    }

    virtual unique_ptr<Term> clone() const = 0;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) = 0;

protected:
    template<typename T>
    bool maybeReplaceChild(unique_ptr<T>& child, const function<unique_ptr<Term>(const Term*)>& visitor)
    {
        auto t = visitor(child.get());
        if (t != nullptr)
        {
            T* downcast = static_cast<T*>(t.detach());
            child.reset(downcast);

            return true;
        }
        return false;
    }
};

using UTerm = unique_ptr<Term>;

class LiteralTerm : public Term
{
public:
    virtual ProgramSymbol eval() const = 0;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, const ITopologyPtr& topology);
    virtual bool match(const ProgramSymbol& sym, bool& isFact);
    virtual size_t hash() const = 0;
    virtual bool operator==(const LiteralTerm& rhs) const = 0;
    bool operator !=(const LiteralTerm& rhs) const { return !operator==(rhs); }

    bool createVariableReps(VariableMap& bound);

    virtual wstring toString() const override;

    CompilerAtom assignedAtom;
};

using ULiteralTerm = unique_ptr<LiteralTerm>;

class VariableTerm : public LiteralTerm
{
public:
    VariableTerm(ProgramVariable param);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override {}
    virtual UTerm clone() const override;
    virtual void collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish = true) const override;
    virtual bool match(const ProgramSymbol& sym, bool& isFact) override;
    virtual size_t hash() const override
    {
        return eastl::hash<ProgramVariable>()(var);
    }
    virtual ProgramSymbol eval() const override;
    virtual wstring toString() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    ProgramVariable var;
    bool isBinder = false;
    shared_ptr<ProgramSymbol> sharedBoundRef;
    ProgramSymbol abstractToConst;
};

using UVariableTerm = unique_ptr<VariableTerm>;

class SymbolTerm : public LiteralTerm
{
public:
    SymbolTerm(const ProgramSymbol& sym);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override {}
    virtual UTerm clone() const override;
    virtual ProgramSymbol eval() const override  { return sym; }
    virtual bool operator==(const LiteralTerm& rhs) const override;
    virtual size_t hash() const override
    {
        return eastl::hash<ProgramSymbol>()(sym);
    }

    ProgramSymbol sym;
};

class VertexTerm : public LiteralTerm
{
public:
    VertexTerm();

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override {}
    virtual UTerm clone() const override;
    virtual bool match(const ProgramSymbol& sym, bool& isFact) override;
    virtual ProgramSymbol eval() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;
    virtual size_t hash() const override { return 0; }
};

class UnaryOpTerm : public LiteralTerm
{
public:
    UnaryOpTerm(EUnaryOperatorType op, ULiteralTerm&& child);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override;
    virtual UTerm clone() const override;
    virtual ProgramSymbol eval() const override;
    virtual wstring toString() const override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    EUnaryOperatorType op;
    ULiteralTerm child;
};

class BinaryOpTerm : public LiteralTerm
{
public:
    BinaryOpTerm(EBinaryOperatorType op, ULiteralTerm&& lhs, ULiteralTerm&& rhs);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<UTerm(const Term*)> visitor) override;
    virtual void collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish) const override;
    virtual ProgramSymbol eval() const override;
    virtual UTerm clone() const override;
    virtual wstring toString() const override;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, const ITopologyPtr& topology) override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    EBinaryOperatorType op;
    ULiteralTerm lhs;
    ULiteralTerm rhs;
};

using UBinaryOpTerm = unique_ptr<BinaryOpTerm>;

class FunctionTerm : public LiteralTerm
{
public:
    FunctionTerm(FormulaUID functionUID, const wchar_t* functionName, vector<ULiteralTerm>&& arguments, bool negated, const IExternalFormulaProviderPtr& provider);

    virtual void collectVars(vector<tuple<VariableTerm*, bool>>& outVars, bool canEstablish = true) const override;
    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override;
    virtual ProgramSymbol eval() const override;
    virtual UTerm clone() const override;
    virtual UInstantiator instantiate(ProgramCompiler& compiler, const ITopologyPtr& topology) override;
    virtual bool match(const ProgramSymbol& sym, bool& isFact) override;
    virtual wstring toString() const override;
    virtual size_t hash() const override;
    virtual bool operator==(const LiteralTerm& rhs) const override;

    bool hasAbstractArgument() const;

    FormulaUID functionUID;
    const wchar_t* functionName;
    vector<ULiteralTerm> arguments;
    IExternalFormulaProviderPtr provider;
    bool negated;
    bool recursive = false;
};

using UFunctionTerm = unique_ptr<FunctionTerm>;

class HeadTerm : public Term
{
public:
    virtual void bindAsFacts(ProgramCompiler& compiler, const ITopologyPtr& topology) = 0;
    virtual vector<ProgramSymbol> eval(bool& isNormalRule) = 0;
    virtual ERuleHeadType getHeadType() const = 0;
};

using UHeadTerm = unique_ptr<HeadTerm>;

class FunctionHeadTerm : public HeadTerm
{
public:
    FunctionHeadTerm(FormulaUID inUID, const wchar_t* inName, vector<ULiteralTerm>&& arguments);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override;
    virtual UTerm clone() const override;

    virtual void bindAsFacts(ProgramCompiler& compiler, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Normal; }

    ProgramSymbol evalSingle() const;

    FormulaUID functionUID;
    const wchar_t* functionName;
    vector<ULiteralTerm> arguments;
};

using UFunctionHeadTerm = unique_ptr<FunctionHeadTerm>;

class DisjunctionTerm : public HeadTerm
{
public:
    DisjunctionTerm(vector<UFunctionHeadTerm>&& children);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override;
    virtual UTerm clone() const override;

    virtual void bindAsFacts(ProgramCompiler& compiler, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Disjunction; }

    vector<UFunctionHeadTerm> children;
};

class ChoiceTerm : public HeadTerm
{
public:
    ChoiceTerm(UFunctionHeadTerm&& term);

    virtual bool visit(const function<EVisitResponse(const Term*)>& visitor) const override;
    virtual void replace(const function<unique_ptr<Term>(const Term*)> visitor) override;
    virtual UTerm clone() const override;
    virtual void bindAsFacts(ProgramCompiler& compiler, const ITopologyPtr& topology) override;
    virtual vector<ProgramSymbol> eval(bool& isNormalRule) override;
    virtual wstring toString() const override;
    virtual ERuleHeadType getHeadType() const override { return ERuleHeadType::Choice; }

    UFunctionHeadTerm subTerm;
};

class RuleStatement
{
public:
    RuleStatement(UHeadTerm&& head, vector<ULiteralTerm>&& body);
    RuleStatement(UHeadTerm&& head) : head(move(head)) {}

    URuleStatement clone() const;

    template<typename T=Term>
    void replace(const function<UTerm(const T* visitor)>& visitor)
    {
        replaceInHead(visitor);
        replaceInBody(visitor);
    }

    wstring toString() const;

    template<typename T=Term>
    void replaceInHead(const function<UTerm(const T* visitor)>& visitor)
    {
        if (auto ht = dynamic_cast<const T*>(head.get()))
        {
            auto newHead = visitor(ht);
            if (newHead != nullptr)
            {
                head = UHeadTerm(move(static_cast<HeadTerm*>(newHead.detach())));
                return;
            }
        }

        if (head != nullptr) head->replace([&](const Term* term)
        {
           if (auto t = dynamic_cast<const T*>(term))
           {
               return visitor(t);
           }
            return UTerm(nullptr);
        });
    }

    template<typename T=Term>
    void replaceInBody(const function<UTerm(const T* visitor)>& visitor)
    {
        for (auto&& bodyTerm : body)
        {
            if (auto bt = dynamic_cast<const T*>(bodyTerm.get()))
            {
                auto newBodyTerm = visitor(bt);
                if (newBodyTerm != nullptr)
                {
                   bodyTerm = ULiteralTerm(move(static_cast<LiteralTerm*>(newBodyTerm.detach())));
                   continue;
                }
            }

            bodyTerm->replace([&](const Term* term)
            {
                if (auto t = dynamic_cast<const T*>(term))
                {
                    return visitor(t);
                }
                return UTerm(nullptr);
            });
        }
    }

    template<typename T=Term>
    bool visit(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        if (!visitHead<T>(visitor))
        {
            return false;
        }
        return visitBody<T>(visitor);
    }

    template<typename T=Term>
    void visit(const function<void(const T*)>& visitor) const
    {
        visitHead<T>(visitor);
        visitBody<T>(visitor);
    }

    template<typename T=Term>
    bool visitHead(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        if (head == nullptr)
        {
            return true;
        }

        return head->visit([&](const Term* term)
        {
           if (auto f = dynamic_cast<const T*>(term))
           {
               return visitor(f);
            }
            return Term::EVisitResponse::Continue;
        });
    }

    template<typename T=Term>
    void visitHead(const function<void(const T*)>& visitor) const
    {
        if (head != nullptr) head->visit([&](const Term* term)
        {
            if (auto f = dynamic_cast<const T*>(term))
            {
                visitor(f);
            }
        });
    }

    template<typename T>
    bool visitBody(const function<Term::EVisitResponse(const T*)>& visitor) const
    {
        for (auto& bodyTerm : body)
        {
            bool ret = bodyTerm->visit([&](const Term* term)
            {
                if (auto f = dynamic_cast<const T*>(term))
                {
                    return visitor(f);
                }
                return Term::EVisitResponse::Continue;
            });

            if (!ret)
            {
                return false;
            }
        }
        return true;
    }

    template<typename T>
    void visitBody(const function<void(const T*)>& visitor) const
    {
        for (auto& bodyTerm : body)
        {
            bodyTerm->visit([&](const Term* term)
            {
                if (auto f = dynamic_cast<const T*>(term))
                {
                    visitor(f);
                }
            });
        }
    }

    template<typename T>
    bool headContains() const
    {
        return head->contains<T>();
    }

    template<typename T>
    bool bodyContains() const
    {
        for (auto& bodyTerm : body)
        {
            if (bodyTerm->contains<T>())
            {
                return true;
            }
        }
        return false;
    }

    template<typename T>
    bool contains() const
    {
        return headContains<T>() || !bodyContains<T>();
    }

    UHeadTerm head;
    vector<ULiteralTerm> body;
};

};