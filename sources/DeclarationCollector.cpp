/*  Copyright (C) 2012-2014  László Nagy
    This file is part of Constantine.

    Constantine implements pseudo const analysis.

    Constantine is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Constantine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "DeclarationCollector.hpp"


namespace {

std::set<clang::CXXRecordDecl const *> AllBase(clang::CXXRecordDecl const * Record) {
    std::set<clang::CXXRecordDecl const *> Result;

    llvm::SmallVector<clang::CXXRecordDecl const *, 8> Queue;
    Queue.push_back(Record);

    while (! Queue.empty()) {
        auto const Current = Queue.pop_back_val();
        for (const auto & BaseIt : Current->bases()) {
            if (auto const * Record = BaseIt.getType()->getAs<clang::RecordType>()) {
                if (auto const * Base = clang::cast_or_null<clang::CXXRecordDecl>(Record->getDecl()->getDefinition())) {
                    Queue.push_back(Base);
                }
            }
        }
        Result.insert(Current);
    }
    return Result;
}

// Strip away parentheses and casts we don't care about.
clang::Expr const * StripExpr(clang::Expr const * E) {
    while (E) {
        if (auto const * const Paren = clang::dyn_cast<clang::ParenExpr const>(E)) {
            E = Paren->getSubExpr();
            continue;
        }
        if (auto const CE = clang::dyn_cast<clang::CastExpr const>(E)) {
            E = CE->getSubExpr();
            continue;
        }
        if (auto const UnOp = clang::dyn_cast<clang::UnaryOperator const>(E)) {
            E = UnOp->getSubExpr();
            continue;
        }
        if (auto const M = clang::dyn_cast<clang::MaterializeTemporaryExpr const>(E)) {
            E = M->GetTemporaryExpr();
            continue;
        }
        if (auto const ASE = clang::dyn_cast<clang::ArraySubscriptExpr const>(E)) {
            E = ASE->getBase();
            continue;
        }
        break;
    }
    return E;
}

std::set<clang::Expr const *> CollectRefereeExpr(clang::Expr const * const E) {
    std::set<clang::Expr const *> Result;

    std::set<clang::Expr const *> Works;
    Works.insert(E);

    while (! Works.empty()) {
        auto const Current = *(Works.begin());
        Works.erase(Works.begin());

        if (decltype(Current) const Stripped = StripExpr(Current)) {
            if (clang::dyn_cast<clang::DeclRefExpr const>(Stripped)) {
                Result.insert(Stripped);
            } else if (auto ME = clang::dyn_cast<clang::MemberExpr const>(Stripped)) {
                // Dig into member variable access to register the outer variable
                while (ME) {
                    if (decltype(ME) const Candidate = clang::dyn_cast<clang::MemberExpr const>(ME->getBase())) {
                        ME = Candidate;
                        continue;
                    }
                    break;
                }
                Result.insert(ME);
            } else if (auto const ACO = clang::dyn_cast<clang::AbstractConditionalOperator const>(Stripped)) {
                Works.insert(ACO->getTrueExpr());
                Works.insert(ACO->getFalseExpr());
            }
        }
    }
    return Result;
}

clang::DeclaratorDecl const * GetDeclarationFromExpr(clang::Expr const * const E) {
    clang::ValueDecl const * RefVal = nullptr;

    if (auto const DRE = clang::dyn_cast<clang::DeclRefExpr const>(E)) {
        RefVal = DRE->getDecl();
    } else if (auto const ME = clang::dyn_cast<clang::MemberExpr const>(E)) {
        RefVal = ME->getMemberDecl();
    }

    return (RefVal) ? clang::dyn_cast<clang::DeclaratorDecl const>(RefVal) : nullptr;
}

} // namespace anonymous


Variables GetVariablesFromContext(clang::DeclContext const * const F, bool const WithArgs) {
    Variables Result;
    for (auto const & It : F->decls()) {
        if (auto const D = clang::dyn_cast<clang::VarDecl const>(It)) {
            if (WithArgs || (!clang::dyn_cast<clang::ParmVarDecl const>(D))) {
                Result.insert(D);
            }
        }
    }
    return Result;
}

Variables GetVariablesFromRecord(clang::CXXRecordDecl const * const Record) {
    Variables Result;
    for (auto const & RecordIt : AllBase(Record)) {
        for (const auto & FieldIt : RecordIt->fields()) {
            Result.insert(FieldIt);
        }
    }
    return Result;
}

Methods GetMethodsFromRecord(clang::CXXRecordDecl const * const Record) {
    Methods Result;
    for (auto const & RecordIt : AllBase(Record)) {
        for (auto const & MethodIt : RecordIt->methods()) {
            Result.insert(MethodIt->getCanonicalDecl());
        }
    }
    return Result;
}

Variables GetReferedVariables(clang::DeclaratorDecl const * const D) {
    Variables Result;

    Variables Works;
    Works.insert(D);

    while (! Works.empty()) {
        // get the current element
        auto const Current = *(Works.begin());
        Works.erase(Works.begin());
        // current element goes into results
        if (Current) {
            Result.insert(Current);
        } else {
            continue;
        }
        // check is it reference or pointer type
        {
            auto const & T = Current->getType();
            if (! ((*T).isReferenceType() || (*T).isPointerType())) {
                continue;
            }
        }
        // check is it refer to a variable
        if (auto const Variable = clang::dyn_cast<clang::VarDecl const>(Current)) {
            // get the initialization expression
            for (auto && Expression: CollectRefereeExpr(Variable->getInit())) {
                Works.insert(GetDeclarationFromExpr(Expression));
            }
        }
    }
    return Result;
}

Variables GetMemberVariablesAndReferences(clang::CXXRecordDecl const * const Rec, clang::DeclContext const * const F) {
    Variables Members = GetVariablesFromRecord(Rec);
    Variables const & Locals = GetVariablesFromContext(F);
    for (auto const &Local : Locals) {
        Variables const &Refs = GetReferedVariables(Local);
        for (auto ReIt(Refs.begin()), ReEnd(Refs.end()); ReIt != ReEnd; ++ReIt) {
            if (Members.count(*ReIt)) {
                Members.insert(Refs.begin(), Refs.end());
                break;
            }
        }
    }
    return Members;
}
