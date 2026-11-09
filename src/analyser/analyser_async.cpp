// async functions as state machines.
//
//     async func[Int] f(a[Int]) {
//         var[Int] x = await g(a);
//         return x + 1;
//     }
//
// The generators know nothing of this. Once the body has been checked, it is
// rewritten into plain Dessa, which all of them can already translate:
//
//     func[Task:Int] f(a[Int]) {                  // what callers see
//         var[Task:Int] t = new Task:Int();
//         var[f$frame] frame = new f$frame();
//         frame._base = t; frame._task = t; frame.a = a;
//         frame._step();                          // runs up to its first wait
//         return t;
//     }
//     class f$frame(_Frame) {                     // the function, stopped and started
//         var[Int] a; var[Int] x; ...
//         func[Emp] _step() {
//             loop {
//                 try {
//                     switch (this._s) {
//                         case 0 := { ...; this._s = 1; if (!w._isDone()) { w._waitBy(this); return; } continue; }
//                         case 1 := { this.x = w._take(); ... this._task._resolve(this.x + 1); return; }
//                     }
//                 } catch (e[Error]) { this._caught(e); } catch (e[Str]) { this._caughtText(e); }
//             }
//         }
//     }
//
// - Local variables become fields, as they must outlive each wait. Reads keep
//   the type the first check found (MemberExpr::trustedType), so narrowing works.
// - A statement with no await inside stays whole, with its own inner locals.
// - An await inside an expression is moved in front of the statement, and so is
//   anything that had to be worked out before it, to keep the order things happen.
// - A lambda keeps copies of what it uses, so those fields are copied to real
//   locals of the same name just before the statement holding it.
// - try around an await: _h says which state handles errors right now. Errors of
//   any kind land in the catch at the bottom, which goes to that state.
#include "analyser.hpp"

#include <map>
#include <set>

namespace dessa {

struct AsyncLowering {
    Analyser& analyser;
    FuncDeclStmt* fn;
    SemanticType resultType;      // T of Task:T
    SemanticType taskType;
    SemanticType selfType;
    bool hasSelf = false;

    std::vector<StmtPtr> fields;
    std::vector<std::vector<StmtPtr>> states;
    size_t current = 0;
    int line = 0, column = 0;
    int handler = -1;             // the state handling errors at this point
    int fieldCount = 0;

    struct Binding { std::string field; };
    std::vector<std::map<std::string, Binding>> scopes;
    std::vector<std::set<std::string>> shadows;
    struct LoopTargets { int breakTo; int continueTo; int handler; };
    std::vector<LoopTargets> loops;
    int innerLoops = 0;           // inside a loop that stays whole (rewriteStatement)
    std::set<const Expr*> finished;   // made here: nothing more to rewrite
    std::map<std::string, SemanticType> snapshots;   // what the statement's lambdas use

    // ---- Small pieces of tree ----

    TypeNode typeNode(const SemanticType& type) {
        TypeNode node;
        node.name = "T";
        node.resolved = std::make_shared<SemanticType>(type);
        return node;
    }
    template <typename T>
    std::unique_ptr<T> at(std::unique_ptr<T> node) {
        node->line = line;
        node->column = column;
        return node;
    }
    Token name(const std::string& text) { return Token(TokenType::IDENTIFIER, text, line, column); }
    ExprPtr thisExpr() {
        ExprPtr e = at(std::make_unique<IdentifierExpr>(Token(TokenType::THIS, "this", line, column)));
        finished.insert(e.get());
        return e;
    }
    ExprPtr memberOf(ExprPtr object, const std::string& field) {
        ExprPtr e = at(std::make_unique<MemberExpr>(std::move(object), name(field), false));
        finished.insert(e.get());
        return e;
    }
    ExprPtr field(const std::string& fieldName) { return memberOf(thisExpr(), fieldName); }
    // A read of a field, as the type the first check found
    ExprPtr fieldRead(const std::string& fieldName, const SemanticType& type) {
        auto e = at(std::make_unique<MemberExpr>(thisExpr(), name(fieldName), false));
        e->trustedType = std::make_shared<SemanticType>(type);
        finished.insert(e.get());
        return e;
    }
    ExprPtr number(int value) { return at(std::make_unique<LiteralExpr>(Token(TokenType::INT_LITERAL, std::to_string(value), line, column))); }
    ExprPtr nil() { return at(std::make_unique<LiteralExpr>(Token(TokenType::NIL, "nil", line, column))); }
    ExprPtr call(ExprPtr object, const std::string& method, std::vector<ExprPtr> values = {}) {
        std::vector<Argument> arguments;
        for (auto& v : values) {
            Argument a;
            a.value = std::move(v);
            arguments.push_back(std::move(a));
        }
        ExprPtr e = at(std::make_unique<CallExpr>(memberOf(std::move(object), method), std::move(arguments), Token(TokenType::R_PAREN, ")", line, column)));
        finished.insert(e.get());
        return e;
    }
    StmtPtr statement(ExprPtr e) { return at(std::make_unique<ExpressionStmt>(std::move(e))); }
    StmtPtr assign(ExprPtr target, ExprPtr value) {
        ExprPtr e = at(std::make_unique<AssignExpr>(std::move(target), Token(TokenType::EQUAL, "=", line, column), std::move(value)));
        finished.insert(e.get());
        return statement(std::move(e));
    }
    StmtPtr setField(const std::string& fieldName, ExprPtr value) { return assign(field(fieldName), std::move(value)); }
    StmtPtr block(std::vector<StmtPtr> list) { return at(std::make_unique<BlockStmt>(std::move(list))); }
    template <typename... S>
    StmtPtr blockOf(S&&... list) {
        std::vector<StmtPtr> out;
        (out.push_back(std::move(list)), ...);
        return block(std::move(out));
    }
    StmtPtr returnNothing() { return at(std::make_unique<ReturnStmt>(nullptr)); }
    StmtPtr jumpStatement(int state) { return blockOf(setField("_s", number(state)), StmtPtr(at(std::make_unique<ContinueStmt>()))); }

    // ---- States ----

    int newState() {
        states.emplace_back();
        return static_cast<int>(states.size()) - 1;
    }
    void start(int state) { current = static_cast<size_t>(state); }
    void emit(StmtPtr s) {
        if (!snapshots.empty()) {
            std::vector<StmtPtr> wrapped = takeSnapshots();
            wrapped.push_back(std::move(s));
            s = block(std::move(wrapped));
        }
        states[current].push_back(std::move(s));
    }
    void jump(int state) { states[current].push_back(jumpStatement(state)); }

    std::vector<StmtPtr> takeSnapshots() {
        std::vector<StmtPtr> out;
        for (const auto& [localName, type] : snapshots) {
            ExprPtr value;
            if (localName == "_this") {
                value = field("_self");
            } else {
                value = fieldRead(bound(localName)->field, type);
            }
            out.push_back(at(std::make_unique<VarDeclStmt>(true, typeNode(type), std::optional<std::string>(localName), std::move(value))));
        }
        snapshots.clear();
        return out;
    }

    std::string newField(const SemanticType& type, const std::string& hint) {
        std::string fieldName = "v" + std::to_string(fieldCount++) + "_" + hint;
        fields.push_back(at(std::make_unique<VarDeclStmt>(false, typeNode(type), std::optional<std::string>(fieldName), nullptr)));
        return fieldName;
    }

    // ---- Names ----

    const Binding* bound(const std::string& text) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            auto found = scope->find(text);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    }
    bool shadowed(const std::string& text) const {
        for (const auto& s : shadows) if (s.count(text)) return true;
        return false;
    }

    const SemanticType& typeOfExpr(const Expr* e) {
        static const SemanticType unknown = SemanticType::unknown();
        const SemanticType* t = analyser.typeOf(e);
        return t ? *t : unknown;
    }

    // ---- What is inside what ----

    // The expressions directly inside this one, in the order they run
    void childrenOf(Expr* e, std::vector<ExprPtr*>& out) {
        switch (e->kind) {
            case ExprKind::TemplateString: for (auto& i : static_cast<TemplateStringExpr*>(e)->interpolations) out.push_back(&i); break;
            case ExprKind::Unary: out.push_back(&static_cast<UnaryExpr*>(e)->operand); break;
            case ExprKind::Postfix: out.push_back(&static_cast<PostfixExpr*>(e)->operand); break;
            case ExprKind::Binary: out.push_back(&static_cast<BinaryExpr*>(e)->left); out.push_back(&static_cast<BinaryExpr*>(e)->right); break;
            case ExprKind::Assign: out.push_back(&static_cast<AssignExpr*>(e)->target); out.push_back(&static_cast<AssignExpr*>(e)->value); break;
            case ExprKind::Ternary: {
                auto* t = static_cast<TernaryExpr*>(e);
                out.push_back(&t->condition); out.push_back(&t->thenBranch); out.push_back(&t->elseBranch);
                break;
            }
            case ExprKind::Call: {
                auto* c = static_cast<CallExpr*>(e);
                out.push_back(&c->callee);
                for (auto& a : c->args) out.push_back(&a.value);
                break;
            }
            case ExprKind::Index: out.push_back(&static_cast<IndexExpr*>(e)->object); out.push_back(&static_cast<IndexExpr*>(e)->index); break;
            case ExprKind::Member: out.push_back(&static_cast<MemberExpr*>(e)->object); break;
            case ExprKind::New: for (auto& a : static_cast<NewExpr*>(e)->args) out.push_back(&a.value); break;
            case ExprKind::List: for (auto& i : static_cast<ListExpr*>(e)->elements) out.push_back(&i.value); break;
            case ExprKind::Set: for (auto& i : static_cast<SetExpr*>(e)->elements) out.push_back(&i.value); break;
            case ExprKind::Map:
                for (auto& m : static_cast<MapExpr*>(e)->entries) {
                    if (m.key) out.push_back(&m.key);
                    out.push_back(&m.value);
                }
                break;
            case ExprKind::Convert: out.push_back(&static_cast<ConvertExpr*>(e)->value); break;
            case ExprKind::Is: out.push_back(&static_cast<IsExpr*>(e)->value); break;
            case ExprKind::Grouping: out.push_back(&static_cast<GroupingExpr*>(e)->inner); break;
            case ExprKind::Await: out.push_back(&static_cast<AwaitExpr*>(e)->operand); break;
            default: break;
        }
    }

    bool hasAwait(const Expr* e) {
        if (!e) return false;
        if (e->lowered) return hasAwait(e->lowered.get());
        if (e->kind == ExprKind::Await) return true;
        if (e->kind == ExprKind::Lambda) return false;   // the parser allows none in there
        std::vector<ExprPtr*> children;
        childrenOf(const_cast<Expr*>(e), children);
        for (auto* c : children) if (hasAwait(c->get())) return true;
        return false;
    }

    bool hasAwait(const Stmt* s) {
        if (!s) return false;
        switch (s->kind) {
            case StmtKind::Expression: return hasAwait(static_cast<const ExpressionStmt*>(s)->expression.get());
            case StmtKind::VarDecl: return hasAwait(static_cast<const VarDeclStmt*>(s)->initializer.get());
            case StmtKind::Return: return hasAwait(static_cast<const ReturnStmt*>(s)->value.get());
            case StmtKind::Throw: return hasAwait(static_cast<const ThrowStmt*>(s)->value.get());
            case StmtKind::Block:
                for (const auto& c : static_cast<const BlockStmt*>(s)->statements) if (hasAwait(c.get())) return true;
                return false;
            case StmtKind::If: {
                const auto* i = static_cast<const IfStmt*>(s);
                return hasAwait(i->condition.get()) || hasAwait(i->thenBranch.get()) || hasAwait(i->elseBranch.get());
            }
            case StmtKind::While: return hasAwait(static_cast<const WhileStmt*>(s)->condition.get()) || hasAwait(static_cast<const WhileStmt*>(s)->body.get());
            case StmtKind::Loop: return hasAwait(static_cast<const LoopStmt*>(s)->body.get());
            case StmtKind::For: {
                const auto* f = static_cast<const ForStmt*>(s);
                return hasAwait(f->init.get()) || hasAwait(f->condition.get()) || hasAwait(f->increment.get()) || hasAwait(f->body.get());
            }
            case StmtKind::ForIn: return hasAwait(static_cast<const ForInStmt*>(s)->iterable.get()) || hasAwait(static_cast<const ForInStmt*>(s)->body.get());
            case StmtKind::Switch: {
                const auto* w = static_cast<const SwitchStmt*>(s);
                if (hasAwait(w->discriminant.get())) return true;
                for (const auto& c : w->cases) if (hasAwait(c.value.get()) || hasAwait(c.body.get())) return true;
                return false;
            }
            case StmtKind::Try: {
                const auto* t = static_cast<const TryStmt*>(s);
                if (hasAwait(t->tryBlock.get())) return true;
                for (const auto& c : t->catches) if (hasAwait(c.body.get())) return true;
                return false;
            }
            default: return false;
        }
    }

    // ---- Lambdas: what they use is copied just before ----

    void scanLambdaExpr(Expr* e) {
        if (!e) return;
        if (e->lowered) { scanLambdaExpr(e->lowered.get()); return; }
        if (e->kind == ExprKind::Identifier) {
            auto* id = static_cast<IdentifierExpr*>(e);
            if (id->name.type == TokenType::THIS) {
                if (hasSelf) {
                    id->name = Token(TokenType::IDENTIFIER, "_this", id->name.line, id->name.column);
                    snapshots.emplace("_this", selfType);
                }
                return;
            }
            if (!shadowed(id->name.lexeme) && bound(id->name.lexeme)) {
                const SemanticType& used = typeOfExpr(e);
                if (!used.isUnknown()) snapshots.emplace(id->name.lexeme, used);
            }
            return;
        }
        if (e->kind == ExprKind::Lambda) {
            scanLambda(static_cast<LambdaExpr*>(e));
            return;
        }
        std::vector<ExprPtr*> children;
        childrenOf(e, children);
        for (auto* c : children) scanLambdaExpr(c->get());
    }

    void scanLambda(LambdaExpr* lambda) {
        shadows.emplace_back();
        for (const auto& p : lambda->params) shadows.back().insert(p.name);
        scanLambdaStmt(lambda->body.get());
        shadows.pop_back();
    }

    void scanLambdaStmt(Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Expression: scanLambdaExpr(static_cast<ExpressionStmt*>(s)->expression.get()); break;
            case StmtKind::VarDecl: {
                auto* v = static_cast<VarDeclStmt*>(s);
                scanLambdaExpr(v->initializer.get());
                if (v->name) shadows.back().insert(*v->name);
                break;
            }
            case StmtKind::Return: scanLambdaExpr(static_cast<ReturnStmt*>(s)->value.get()); break;
            case StmtKind::Throw: scanLambdaExpr(static_cast<ThrowStmt*>(s)->value.get()); break;
            case StmtKind::Block:
                shadows.emplace_back();
                for (auto& c : static_cast<BlockStmt*>(s)->statements) scanLambdaStmt(c.get());
                shadows.pop_back();
                break;
            case StmtKind::If: {
                auto* i = static_cast<IfStmt*>(s);
                scanLambdaExpr(i->condition.get());
                scanLambdaStmt(i->thenBranch.get());
                scanLambdaStmt(i->elseBranch.get());
                break;
            }
            case StmtKind::While: scanLambdaExpr(static_cast<WhileStmt*>(s)->condition.get()); scanLambdaStmt(static_cast<WhileStmt*>(s)->body.get()); break;
            case StmtKind::Loop: scanLambdaStmt(static_cast<LoopStmt*>(s)->body.get()); break;
            case StmtKind::For: {
                auto* f = static_cast<ForStmt*>(s);
                shadows.emplace_back();
                scanLambdaStmt(f->init.get());
                scanLambdaExpr(f->condition.get());
                scanLambdaExpr(f->increment.get());
                scanLambdaStmt(f->body.get());
                shadows.pop_back();
                break;
            }
            case StmtKind::ForIn: {
                auto* f = static_cast<ForInStmt*>(s);
                scanLambdaExpr(f->iterable.get());
                shadows.emplace_back();
                scanLambdaStmt(f->declaration.get());
                scanLambdaStmt(f->body.get());
                shadows.pop_back();
                break;
            }
            case StmtKind::Switch: {
                auto* w = static_cast<SwitchStmt*>(s);
                scanLambdaExpr(w->discriminant.get());
                for (auto& c : w->cases) {
                    scanLambdaExpr(c.value.get());
                    scanLambdaStmt(c.body.get());
                }
                break;
            }
            case StmtKind::Try: {
                auto* t = static_cast<TryStmt*>(s);
                scanLambdaStmt(t->tryBlock.get());
                for (auto& c : t->catches) {
                    shadows.emplace_back();
                    if (c.param) shadows.back().insert(*c.param);
                    scanLambdaStmt(c.body.get());
                    shadows.pop_back();
                }
                break;
            }
            default: break;
        }
    }

    // ---- Rewriting what stays whole ----

    void rewrite(ExprPtr& e, bool isTarget = false) {
        if (!e || finished.count(e.get())) return;
        if (e->lowered) {
            rewrite(e->lowered);
            return;
        }
        if (e->kind == ExprKind::Identifier) {
            auto* id = static_cast<IdentifierExpr*>(e.get());
            line = e->line ? e->line : line;
            column = e->line ? e->column : column;
            if (id->name.type == TokenType::THIS) {
                if (hasSelf) e = field("_self");
                return;
            }
            if (id->name.type == TokenType::SUPER || shadowed(id->name.lexeme)) return;
            const Binding* binding = bound(id->name.lexeme);
            if (!binding) return;
            const SemanticType used = typeOfExpr(e.get());
            const std::string fieldName = binding->field;
            e = isTarget || used.isUnknown() ? field(fieldName) : fieldRead(fieldName, used);
            return;
        }
        if (e->kind == ExprKind::Lambda) {
            scanLambda(static_cast<LambdaExpr*>(e.get()));
            return;
        }
        if (e->kind == ExprKind::Assign) {
            auto* a = static_cast<AssignExpr*>(e.get());
            rewrite(a->target, true);
            rewrite(a->value);
            return;
        }
        if (e->kind == ExprKind::Postfix) {
            rewrite(static_cast<PostfixExpr*>(e.get())->operand, true);
            return;
        }
        std::vector<ExprPtr*> children;
        childrenOf(e.get(), children);
        for (auto* c : children) rewrite(*c);
    }

    // return in code that stays whole: finishes the task and leaves _step
    StmtPtr finishWith(ExprPtr value) {
        std::vector<ExprPtr> values;
        if (value) {
            values.push_back(std::move(value));
        } else {
            auto made = at(std::make_unique<NewExpr>(name("_Nothing"), std::vector<Argument>{}));
            made->resolvedClass = std::make_shared<SemanticType>(resultType);
            finished.insert(made.get());
            values.push_back(std::move(made));
        }
        std::vector<StmtPtr> out;
        out.push_back(setField("_s", number(-1)));
        out.push_back(statement(call(field("_task"), "_resolve", std::move(values))));
        out.push_back(returnNothing());
        return block(std::move(out));
    }

    StmtPtr leaveLoop(bool isBreak) {
        const LoopTargets& l = loops.back();
        std::vector<StmtPtr> out;
        if (l.handler != handler) out.push_back(setField("_h", number(l.handler)));
        out.push_back(setField("_s", number(isBreak ? l.breakTo : l.continueTo)));
        out.push_back(StmtPtr(at(std::make_unique<ContinueStmt>())));
        return block(std::move(out));
    }

    void rewriteList(std::vector<StmtPtr>& list, size_t from = 0) {
        for (size_t i = from; i < list.size(); ++i) {
            const auto saved = snapshots;
            snapshots.clear();
            rewriteStatement(list[i]);
            if (!snapshots.empty()) {
                // Copies just before it. What follows goes inside too, as the copies'
                // block would otherwise end a declaration's life.
                std::vector<StmtPtr> inner = takeSnapshots();
                for (size_t k = i; k < list.size(); ++k) inner.push_back(std::move(list[k]));
                list.resize(i);
                list.push_back(block(std::move(inner)));
                auto* made = static_cast<BlockStmt*>(list.back().get());
                // the statement itself is rewritten: go on from the one after it
                size_t after = 0;
                for (; after < made->statements.size(); ++after) {
                    const Stmt* candidate = made->statements[after].get();
                    if (candidate && candidate->kind == StmtKind::VarDecl && snapshotDecls(candidate)) continue;
                    break;
                }
                shadows.emplace_back();
                rewriteList(made->statements, after + 1);
                shadows.pop_back();
                snapshots = saved;
                return;
            }
            snapshots = saved;
        }
    }

    // Made by takeSnapshots: const, and given a value made here
    bool snapshotDecls(const Stmt* s) const {
        const auto* v = static_cast<const VarDeclStmt*>(s);
        return v->isConst && v->initializer && finished.count(v->initializer.get());
    }

    void rewriteStatement(StmtPtr& s) {
        if (!s) return;
        line = s->line ? s->line : line;
        column = s->line ? s->column : column;
        switch (s->kind) {
            case StmtKind::Expression: rewrite(static_cast<ExpressionStmt*>(s.get())->expression); break;
            case StmtKind::VarDecl: {
                auto* v = static_cast<VarDeclStmt*>(s.get());
                rewrite(v->initializer);
                if (v->name) shadows.back().insert(*v->name);
                break;
            }
            case StmtKind::Return: {
                auto* r = static_cast<ReturnStmt*>(s.get());
                rewrite(r->value);
                StmtPtr finish = finishWith(std::move(r->value));
                s = std::move(finish);
                break;
            }
            case StmtKind::Throw: rewrite(static_cast<ThrowStmt*>(s.get())->value); break;
            case StmtKind::Block:
                shadows.emplace_back();
                rewriteList(static_cast<BlockStmt*>(s.get())->statements);
                shadows.pop_back();
                break;
            case StmtKind::If: {
                auto* i = static_cast<IfStmt*>(s.get());
                rewrite(i->condition);
                rewriteBranch(i->thenBranch);
                rewriteBranch(i->elseBranch);
                break;
            }
            case StmtKind::While: {
                auto* w = static_cast<WhileStmt*>(s.get());
                rewrite(w->condition);
                ++innerLoops;
                rewriteBranch(w->body);
                --innerLoops;
                break;
            }
            case StmtKind::Loop:
                ++innerLoops;
                rewriteBranch(static_cast<LoopStmt*>(s.get())->body);
                --innerLoops;
                break;
            case StmtKind::For: {
                auto* f = static_cast<ForStmt*>(s.get());
                shadows.emplace_back();
                rewriteStatement(f->init);
                rewrite(f->condition);
                rewrite(f->increment);
                ++innerLoops;
                rewriteBranch(f->body);
                --innerLoops;
                shadows.pop_back();
                break;
            }
            case StmtKind::ForIn: {
                auto* f = static_cast<ForInStmt*>(s.get());
                rewrite(f->iterable);
                shadows.emplace_back();
                if (auto* v = static_cast<VarDeclStmt*>(f->declaration.get()); v && v->name) shadows.back().insert(*v->name);
                ++innerLoops;
                rewriteBranch(f->body);
                --innerLoops;
                shadows.pop_back();
                break;
            }
            case StmtKind::Switch: {
                auto* w = static_cast<SwitchStmt*>(s.get());
                rewrite(w->discriminant);
                for (auto& c : w->cases) {
                    rewrite(c.value);
                    rewriteBranch(c.body);
                }
                break;
            }
            case StmtKind::Try: {
                auto* t = static_cast<TryStmt*>(s.get());
                rewriteBranch(t->tryBlock);
                for (auto& c : t->catches) {
                    shadows.emplace_back();
                    if (c.param) shadows.back().insert(*c.param);
                    rewriteBranch(c.body);
                    shadows.pop_back();
                }
                break;
            }
            case StmtKind::Break:
                if (innerLoops == 0 && !loops.empty()) s = leaveLoop(true);
                break;
            case StmtKind::Continue:
                if (innerLoops == 0 && !loops.empty()) s = leaveLoop(false);
                break;
            default: break;
        }
    }

    // A body: one statement or a block. Copies a lambda needs stay inside it.
    void rewriteBranch(StmtPtr& s) {
        if (!s) return;
        const auto saved = snapshots;
        snapshots.clear();
        rewriteStatement(s);
        if (!snapshots.empty()) {
            std::vector<StmtPtr> inner = takeSnapshots();
            inner.push_back(std::move(s));
            s = block(std::move(inner));
        }
        snapshots = saved;
    }

    // ---- Moving waits out of expressions ----

    bool isSimple(const Expr* e) {
        return !e || e->kind == ExprKind::Literal || e->kind == ExprKind::Lambda || finished.count(e) ||
               (e->kind == ExprKind::Identifier && static_cast<const IdentifierExpr*>(e)->name.type != TokenType::IDENTIFIER);
    }

    // a, this.a, a.b.c: a place, not a value worked out from something
    bool isVariablePath(const Expr* e) const {
        while (e && e->kind == ExprKind::Member && !e->lowered) e = static_cast<const MemberExpr*>(e)->object.get();
        return e && !e->lowered && e->kind == ExprKind::Identifier;
    }

    // Keeps a value in a field, so it's worked out before the wait
    void spill(ExprPtr& e) {
        const SemanticType type = typeOfExpr(actual(e.get()));
        rewrite(e);
        if (type.isUnknown() || type.isVoidLike()) return;
        const std::string fieldName = newField(type, "t");
        emit(setField(fieldName, std::move(e)));
        e = fieldRead(fieldName, type);
    }

    void hoist(ExprPtr& e) {
        if (!e || !hasAwait(e.get())) return;
        if (e->lowered) {
            hoist(e->lowered);
            return;
        }
        line = e->line ? e->line : line;
        column = e->line ? e->column : column;
        switch (e->kind) {
            case ExprKind::Await: {
                auto* a = static_cast<AwaitExpr*>(e.get());
                const SemanticType awaited = typeOfExpr(e.get());
                const SemanticType task = typeOfExpr(actual(a->operand.get()));
                hoist(a->operand);
                rewrite(a->operand);
                const std::string waitField = newField(task, "wait");
                emit(setField(waitField, std::move(a->operand)));
                const int resume = newState();
                emit(setField("_s", number(resume)));
                // Done already: go on without waiting
                std::vector<ExprPtr> thisValue;
                thisValue.push_back(thisExpr());
                ExprPtr notDone = at(std::make_unique<UnaryExpr>(Token(TokenType::NOT, "!", line, column), call(field(waitField), "_isDone")));
                finished.insert(notDone.get());
                emit(at(std::make_unique<IfStmt>(std::move(notDone), blockOf(statement(call(field(waitField), "_waitBy", std::move(thisValue))), returnNothing()), nullptr)));
                states[current].push_back(StmtPtr(at(std::make_unique<ContinueStmt>())));
                start(resume);
                const bool nothing = awaited.isVoidLike();
                if (nothing) {
                    emit(statement(call(field(waitField), "_take")));
                    e = nil();
                    finished.insert(e.get());
                } else {
                    const std::string resultField = newField(awaited, "got");
                    emit(setField(resultField, call(field(waitField), "_take")));
                    e = fieldRead(resultField, awaited);
                }
                return;
            }
            case ExprKind::Ternary: {
                auto* t = static_cast<TernaryExpr*>(e.get());
                const SemanticType whole = typeOfExpr(e.get());
                hoist(t->condition);
                rewrite(t->condition);
                const std::string resultField = newField(whole, "if");
                const int yes = newState(), no = newState(), done = newState();
                emit(at(std::make_unique<IfStmt>(std::move(t->condition), jumpStatement(yes), jumpStatement(no))));
                start(yes);
                hoist(t->thenBranch);
                rewrite(t->thenBranch);
                emit(setField(resultField, std::move(t->thenBranch)));
                jump(done);
                start(no);
                hoist(t->elseBranch);
                rewrite(t->elseBranch);
                emit(setField(resultField, std::move(t->elseBranch)));
                jump(done);
                start(done);
                e = fieldRead(resultField, whole);
                return;
            }
            case ExprKind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e.get());
                const TokenType op = b->op.type;
                if ((op == TokenType::AND_AND || op == TokenType::OR_OR || op == TokenType::QUESTION_QUESTION) && hasAwait(b->right.get())) {
                    const SemanticType whole = typeOfExpr(e.get());
                    const SemanticType left = typeOfExpr(actual(b->left.get()));
                    hoist(b->left);
                    rewrite(b->left);
                    const std::string resultField = newField(whole, "and");
                    const std::string leftField = newField(left, "left");
                    emit(setField(leftField, std::move(b->left)));
                    const int more = newState(), done = newState();
                    ExprPtr test;
                    if (op == TokenType::QUESTION_QUESTION) {
                        test = at(std::make_unique<BinaryExpr>(fieldRead(leftField, left), Token(TokenType::EQUAL_EQUAL, "==", line, column), nil()));
                    } else if (op == TokenType::AND_AND) {
                        test = fieldRead(leftField, left);
                    } else {
                        test = at(std::make_unique<UnaryExpr>(Token(TokenType::NOT, "!", line, column), fieldRead(leftField, left)));
                    }
                    finished.insert(test.get());
                    const SemanticType kept = op == TokenType::QUESTION_QUESTION ? withoutNil(left) : left;
                    emit(at(std::make_unique<IfStmt>(std::move(test), jumpStatement(more),
                                                     blockOf(setField(resultField, kept.isUnknown() ? field(leftField) : fieldRead(leftField, kept)), jumpStatement(done)))));
                    start(more);
                    hoist(b->right);
                    rewrite(b->right);
                    emit(setField(resultField, std::move(b->right)));
                    jump(done);
                    start(done);
                    e = fieldRead(resultField, whole);
                    return;
                }
                break;
            }
            default: break;
        }
        // Anything else: its parts in order, keeping what runs before the last wait
        std::vector<ExprPtr*> children;
        Expr* target = e.get();
        if (target->kind == ExprKind::Call) {
            auto* c = static_cast<CallExpr*>(target);
            // obj.method(...): obj comes first; the method's name is no value of its own.
            // A variable stays itself: list.push(await x) changes that list, not a copy.
            if (c->callee->kind == ExprKind::Member) {
                ExprPtr& object = static_cast<MemberExpr*>(c->callee.get())->object;
                if (!isVariablePath(object.get())) children.push_back(&object);
            }
            for (auto& a : c->args) children.push_back(&a.value);
        } else if (target->kind == ExprKind::Assign) {
            auto* a = static_cast<AssignExpr*>(target);
            // What is assigned to stays itself, for the same reason
            if (a->target->kind == ExprKind::Member && !isVariablePath(static_cast<MemberExpr*>(a->target.get())->object.get())) {
                children.push_back(&static_cast<MemberExpr*>(a->target.get())->object);
            }
            if (a->target->kind == ExprKind::Index) {
                auto* index = static_cast<IndexExpr*>(a->target.get());
                if (!isVariablePath(index->object.get())) children.push_back(&index->object);
                children.push_back(&index->index);
            }
            children.push_back(&a->value);
        } else {
            childrenOf(target, children);
        }
        size_t last = 0;
        for (size_t i = 0; i < children.size(); ++i) if (hasAwait(children[i]->get())) last = i;
        for (size_t i = 0; i <= last && i < children.size(); ++i) {
            if (hasAwait(children[i]->get())) hoist(*children[i]);
            else if (!isSimple(children[i]->get())) spill(*children[i]);
        }
    }

    // ---- Statements with a wait inside ----

    void flattenBlockContents(std::vector<StmtPtr>& list) {
        scopes.emplace_back();
        for (auto& s : list) flatten(s);
        scopes.pop_back();
    }

    void flattenBody(StmtPtr& s) {
        if (s && s->kind == StmtKind::Block) flattenBlockContents(static_cast<BlockStmt*>(s.get())->statements);
        else flatten(s);
    }

    void declareLocal(const VarDeclStmt* v, const SemanticType& type) {
        const std::string fieldName = newField(type, *v->name);
        scopes.back()[*v->name] = Binding{fieldName};
    }

    void flattenVar(StmtPtr& s) {
        auto* v = static_cast<VarDeclStmt*>(s.get());
        if (!v->name) return;
        const SemanticType* declared = analyser.declaredTypeOf(v);
        const SemanticType type = declared ? *declared : typeOfExpr(actual(v->initializer.get()));
        // A lambda may call itself, by its name: that needs a real variable
        if (v->initializer && v->initializer->kind == ExprKind::Lambda) {
            const SemanticType kept = typeOfExpr(v->initializer.get());
            scanLambda(static_cast<LambdaExpr*>(v->initializer.get()));
            snapshots.erase(*v->name);
            const std::string localName = *v->name;
            std::vector<StmtPtr> inner = takeSnapshots();
            declareLocal(v, kept);
            const std::string fieldName = bound(localName)->field;
            inner.push_back(std::move(s));
            ExprPtr name2 = at(std::make_unique<IdentifierExpr>(name(localName)));
            finished.insert(name2.get());
            inner.push_back(setField(fieldName, std::move(name2)));
            states[current].push_back(block(std::move(inner)));
            return;
        }
        ExprPtr value = std::move(v->initializer);
        if (value) {
            hoist(value);
            rewrite(value);
        }
        // Its own name means the new one only from here on
        declareLocal(v, type);
        if (value) {
            emit(setField(bound(*v->name)->field, std::move(value)));
        } else if (!loops.empty()) {
            // var[T] x; starts over each time round, as anywhere else. A field
            // nobody sets holds the value it starts with.
            emit(setField(bound(*v->name)->field, field(newField(type, "start"))));
        }
    }

    void flatten(StmtPtr& s) {
        if (!s) return;
        line = s->line ? s->line : line;
        column = s->line ? s->column : column;
        if (s->kind == StmtKind::VarDecl) {
            flattenVar(s);
            return;
        }
        if (!hasAwait(s.get())) {
            snapshots.clear();
            rewriteStatement(s);
            emit(std::move(s));
            return;
        }
        switch (s->kind) {
            case StmtKind::Expression: {
                auto* x = static_cast<ExpressionStmt*>(s.get());
                hoist(x->expression);
                rewrite(x->expression);
                // what is left of `await f();` is only the value it gave
                if (!finished.count(x->expression.get()) || x->expression->kind == ExprKind::Call) emit(std::move(s));
                break;
            }
            case StmtKind::Return: {
                auto* r = static_cast<ReturnStmt*>(s.get());
                hoist(r->value);
                rewrite(r->value);
                emit(finishWith(std::move(r->value)));
                break;
            }
            case StmtKind::Throw: {
                auto* t = static_cast<ThrowStmt*>(s.get());
                hoist(t->value);
                rewrite(t->value);
                emit(std::move(s));
                break;
            }
            case StmtKind::Block:
                flattenBlockContents(static_cast<BlockStmt*>(s.get())->statements);
                break;
            case StmtKind::If: {
                auto* i = static_cast<IfStmt*>(s.get());
                hoist(i->condition);
                rewrite(i->condition);
                const int yes = newState(), done = newState();
                const int no = i->elseBranch ? newState() : done;
                emit(at(std::make_unique<IfStmt>(std::move(i->condition), jumpStatement(yes), jumpStatement(no))));
                start(yes);
                flattenBody(i->thenBranch);
                jump(done);
                if (i->elseBranch) {
                    start(no);
                    flattenBody(i->elseBranch);
                    jump(done);
                }
                start(done);
                break;
            }
            case StmtKind::While: {
                auto* w = static_cast<WhileStmt*>(s.get());
                const int head = newState(), body = newState(), done = newState();
                jump(head);
                start(head);
                hoist(w->condition);
                rewrite(w->condition);
                emit(at(std::make_unique<IfStmt>(std::move(w->condition), jumpStatement(body), jumpStatement(done))));
                loops.push_back({done, head, handler});
                start(body);
                flattenBody(w->body);
                jump(head);
                loops.pop_back();
                start(done);
                break;
            }
            case StmtKind::Loop: {
                auto* l = static_cast<LoopStmt*>(s.get());
                const int head = newState(), done = newState();
                jump(head);
                loops.push_back({done, head, handler});
                start(head);
                flattenBody(l->body);
                jump(head);
                loops.pop_back();
                start(done);
                break;
            }
            case StmtKind::For: {
                auto* f = static_cast<ForStmt*>(s.get());
                scopes.emplace_back();
                flatten(f->init);
                const int head = newState(), body = newState(), step = newState(), done = newState();
                jump(head);
                start(head);
                if (f->condition) {
                    hoist(f->condition);
                    rewrite(f->condition);
                    emit(at(std::make_unique<IfStmt>(std::move(f->condition), jumpStatement(body), jumpStatement(done))));
                } else {
                    jump(body);
                }
                loops.push_back({done, step, handler});
                start(body);
                flattenBody(f->body);
                jump(step);
                loops.pop_back();
                start(step);
                if (f->increment) {
                    hoist(f->increment);
                    rewrite(f->increment);
                    emit(statement(std::move(f->increment)));
                }
                jump(head);
                scopes.pop_back();
                start(done);
                break;
            }
            case StmtKind::ForIn: flattenForIn(s); break;
            case StmtKind::Switch: {
                auto* w = static_cast<SwitchStmt*>(s.get());
                hoist(w->discriminant);
                rewrite(w->discriminant);
                const int done = newState();
                std::vector<int> targets;
                bool hasDefault = false;
                for (auto& c : w->cases) {
                    if (!c.value) hasDefault = true;
                    rewrite(c.value);
                    targets.push_back(newState());
                }
                std::vector<SwitchCase> dispatch;
                for (size_t k = 0; k < w->cases.size(); ++k) {
                    dispatch.push_back({std::move(w->cases[k].value), jumpStatement(targets[k])});
                }
                if (!hasDefault) dispatch.push_back({nullptr, jumpStatement(done)});
                std::vector<StmtPtr> bodies;
                emit(at(std::make_unique<SwitchStmt>(std::move(w->discriminant), std::move(dispatch))));
                for (size_t k = 0; k < w->cases.size(); ++k) {
                    start(targets[k]);
                    flattenBody(w->cases[k].body);
                    jump(done);
                }
                start(done);
                break;
            }
            case StmtKind::Try: flattenTry(s); break;
            default: {
                rewriteStatement(s);
                emit(std::move(s));
            }
        }
    }

    void flattenForIn(StmtPtr& s) {
        auto* f = static_cast<ForInStmt*>(s.get());
        const auto* decl = static_cast<const VarDeclStmt*>(f->declaration.get());
        const SemanticType iterableType = typeOfExpr(actual(f->iterable.get()));
        hoist(f->iterable);
        rewrite(f->iterable);
        // What it walks through, as a list
        ExprPtr source = std::move(f->iterable);
        SemanticType listType = iterableType;
        if (iterableType.is(SemanticTypeKind::Str)) {
            source = call(std::move(source), "chars");
            listType = SemanticType::listOf(SemanticType::simple(SemanticTypeKind::Chr));
        } else if (iterableType.is(SemanticTypeKind::Map)) {
            source = call(std::move(source), "keys");
            listType = SemanticType::listOf(iterableType.keyType ? *iterableType.keyType : SemanticType::unknown());
        } else if (iterableType.is(SemanticTypeKind::Set)) {
            source = call(std::move(source), "toList");
            listType = SemanticType::listOf(iterableType.elementType ? *iterableType.elementType : SemanticType::unknown());
        } else if (iterableType.isClass()) {
            source = call(std::move(source), "_iter");
            const Symbol* method = analyser.findMember(iterableType, "_iter");
            listType = method ? method->returnType : SemanticType::unknown();
        }
        const SemanticType itemType = listType.elementType ? *listType.elementType : SemanticType::unknown();
        const std::string seq = newField(listType, "seq"), index = newField(SemanticType::simple(SemanticTypeKind::Int), "i");
        emit(setField(seq, std::move(source)));
        emit(setField(index, number(0)));
        scopes.emplace_back();
        const SemanticType* declared = analyser.declaredTypeOf(decl);
        declareLocal(decl, declared ? *declared : itemType);
        const std::string item = bound(*decl->name)->field;
        const int head = newState(), body = newState(), step = newState(), done = newState();
        jump(head);
        start(head);
        ExprPtr test = at(std::make_unique<BinaryExpr>(fieldRead(index, SemanticType::simple(SemanticTypeKind::Int)), Token(TokenType::L_ANGLE, "<", line, column),
                                                       call(fieldRead(seq, listType), "length")));
        finished.insert(test.get());
        emit(at(std::make_unique<IfStmt>(std::move(test), jumpStatement(body), jumpStatement(done))));
        loops.push_back({done, step, handler});
        start(body);
        ExprPtr element = at(std::make_unique<IndexExpr>(fieldRead(seq, listType), fieldRead(index, SemanticType::simple(SemanticTypeKind::Int))));
        finished.insert(element.get());
        emit(setField(item, std::move(element)));
        flattenBody(f->body);
        jump(step);
        loops.pop_back();
        start(step);
        ExprPtr one = at(std::make_unique<BinaryExpr>(fieldRead(index, SemanticType::simple(SemanticTypeKind::Int)), Token(TokenType::PLUS, "+", line, column), number(1)));
        finished.insert(one.get());
        emit(setField(index, std::move(one)));
        jump(head);
        scopes.pop_back();
        start(done);
    }

    void flattenTry(StmtPtr& s) {
        auto* t = static_cast<TryStmt*>(s.get());
        const int outer = handler;
        const int dispatch = newState(), done = newState();
        emit(setField("_h", number(dispatch)));
        handler = dispatch;
        flattenBody(t->tryBlock);
        handler = outer;
        emit(setField("_h", number(outer)));
        jump(done);

        // Which catch it goes to: the first that fits, as in any try
        start(dispatch);
        emit(setField("_h", number(outer)));
        std::vector<int> arms;
        for (size_t i = 0; i < t->catches.size(); ++i) arms.push_back(newState());
        std::vector<std::pair<std::string, std::string>> bindings;
        const SemanticType errorOrNil = SemanticType::unionOf({errorType(), SemanticType::simple(SemanticTypeKind::Nil)});
        for (size_t i = 0; i < t->catches.size(); ++i) {
            auto& clause = t->catches[i];
            std::string fieldName;
            const SemanticType* caught = analyser.catchTypeOf(&clause);
            if (clause.param && caught) fieldName = newField(*caught, *clause.param);
            bindings.emplace_back(clause.param.value_or(""), fieldName);
            if (!clause.param || !caught) {
                jump(arms[i]);
                continue;
            }
            if (caught->is(SemanticTypeKind::Str)) {
                states[current].push_back(setField(fieldName, call(thisExpr(), "_text")));
                jump(arms[i]);
                continue;
            }
            // err is T, through a real variable: it narrows
            ExprPtr seen = at(std::make_unique<IdentifierExpr>(name("_seen")));
            finished.insert(seen.get());
            auto test = at(std::make_unique<IsExpr>(std::move(seen), typeNode(*caught)));
            finished.insert(test.get());
            ExprPtr value = at(std::make_unique<IdentifierExpr>(name("_seen")));
            finished.insert(value.get());
            std::vector<StmtPtr> inner;
            inner.push_back(at(std::make_unique<VarDeclStmt>(false, typeNode(errorOrNil), std::optional<std::string>("_seen"), field("_err"))));
            inner.push_back(at(std::make_unique<IfStmt>(std::move(test), blockOf(setField(fieldName, std::move(value)), jumpStatement(arms[i])), nullptr)));
            states[current].push_back(block(std::move(inner)));
        }
        states[current].push_back(statement(call(thisExpr(), "_rethrow")));

        for (size_t i = 0; i < t->catches.size(); ++i) {
            start(arms[i]);
            scopes.emplace_back();
            if (!bindings[i].first.empty() && !bindings[i].second.empty()) scopes.back()[bindings[i].first] = Binding{bindings[i].second};
            flattenBody(t->catches[i].body);
            scopes.pop_back();
            jump(done);
        }
        start(done);
    }

    SemanticType errorType() {
        const Symbol* symbol = analyser.resolveSymbol("Error");
        return symbol ? symbol->type : SemanticType::unknown();
    }
};

const Symbol* Analyser::resolveQualifiedOrLocal(const std::string& name, int line, int column) {
    return name.find('.') != std::string::npos ? resolveQualified(name, line, column) : resolveSymbol(name);
}

// `async` itself, or its name through the hidden import
std::string Analyser::asyncName(const std::string& name) {
    return resolveSymbol("_xdsasync") ? "_xdsasync." + name : name;
}

// async func[Int] f() is a func[Task:Int] f(). An async func[Emp] gives a Task:Emp.
void Analyser::prepareAsync(const FuncDeclStmt* declaration) {
    if (!declaration->asyncResult.name.empty() || declaration->asyncResult.resolved) return;
    auto* fn = const_cast<FuncDeclStmt*>(declaration);
    fn->asyncResult = fn->returnType;
    TypeNode result = fn->returnType;
    TypeNode task;
    task.name = asyncName("Task");
    task.typeArgs.push_back(std::move(result));
    fn->returnType = std::move(task);
}

SemanticType Analyser::analyseAwait(const AwaitExpr* expression) {
    const SemanticType task = analyseExpression(expression->operand.get());
    if (task.isUnknown()) return task;
    auto instance = classInstances.find(task.key());
    if (instance == classInstances.end() || templates[static_cast<size_t>(instance->second.first)].name != "Task") {
        error(expression, "await waits for a Task, like what an async function gives, not " + task.toString());
        return SemanticType::unknown();
    }
    return instance->second.second.at(0);
}

// After the body has been checked: replace it with the state machine.
void Analyser::lowerAsync(const FuncDeclStmt* statement) {
    auto* fn = const_cast<FuncDeclStmt*>(statement);
    const Symbol* signature = functionOf(statement);
    if (!signature) return;
    auto instance = classInstances.find(signature->returnType.key());
    if (instance == classInstances.end()) return;

    AsyncLowering lower{*this, fn, instance->second.second.at(0), signature->returnType};
    lower.line = statement->line;
    lower.column = statement->column;
    lower.scopes.emplace_back();
    lower.shadows.emplace_back();
    lower.states.emplace_back();
    if (const ClassInfo* owner = currentClass()) {
        lower.hasSelf = true;
        lower.selfType = owner->type();
    }

    // The parameters come in as fields
    std::vector<std::pair<std::string, std::string>> parameters;
    for (size_t i = 0; i < statement->params.size(); ++i) {
        const std::string field = lower.newField(signature->parameters[i].type, statement->params[i].name);
        lower.scopes.back()[statement->params[i].name] = {field};
        parameters.emplace_back(statement->params[i].name, field);
    }

    auto* body = static_cast<BlockStmt*>(fn->body.get());
    lower.flattenBlockContents(body->statements);
    // The end of the function (with a return type, the check has said it never gets here)
    if (lower.resultType.isClass() && lower.resultType.name == "_Nothing") lower.emit(lower.finishWith(nullptr));
    else lower.emit(lower.returnNothing());

    // class frame(_Frame) { fields...; func[Emp] _step() { ... } }
    const std::string className = "async$" + std::to_string(++asyncFrames) + "$" + statement->name;
    const auto makeSwitch = [&]() {
        std::vector<SwitchCase> cases;
        for (size_t s = 0; s < lower.states.size(); ++s) {
            cases.push_back({lower.number(static_cast<int>(s)), lower.block(std::move(lower.states[s]))});
        }
        cases.push_back({nullptr, lower.blockOf(lower.returnNothing())});
        return cases;
    };
    std::vector<SwitchCase> cases = makeSwitch();
    StmtPtr dispatch = lower.at(std::make_unique<SwitchStmt>(lower.field("_s"), std::move(cases)));

    const auto catchArm = [&](const char* typeName, const char* method) {
        CatchClause clause;
        clause.param = "ds_e";
        TypeNode type;
        type.name = typeName;
        clause.type = std::move(type);
        std::vector<ExprPtr> values;
        ExprPtr value = lower.at(std::make_unique<IdentifierExpr>(lower.name("ds_e")));
        values.push_back(std::move(value));
        clause.body = lower.blockOf(lower.statement(lower.call(lower.thisExpr(), method, std::move(values))));
        clause.line = statement->line;
        clause.column = statement->column;
        return clause;
    };
    std::vector<CatchClause> catches;
    catches.push_back(catchArm("Error", "_caught"));
    catches.push_back(catchArm("Str", "_caughtText"));
    StmtPtr guarded = lower.at(std::make_unique<TryStmt>(lower.blockOf(std::move(dispatch)), std::move(catches)));
    StmtPtr forever = lower.at(std::make_unique<LoopStmt>(lower.blockOf(std::move(guarded))));

    TypeNode emp;
    emp.name = "Emp";
    auto step = lower.at(std::make_unique<FuncDeclStmt>(std::move(emp), "_step", std::vector<Param>{}, lower.blockOf(std::move(forever))));
    step->annotations.push_back(Token(TokenType::IDENTIFIER, "override", statement->line, statement->column));

    std::vector<StmtPtr> members = std::move(lower.fields);
    members.push_back(lower.at(std::make_unique<VarDeclStmt>(false, lower.typeNode(lower.taskType), std::optional<std::string>("_task"), nullptr)));
    if (lower.hasSelf) {
        members.push_back(lower.at(std::make_unique<VarDeclStmt>(false, lower.typeNode(lower.selfType), std::optional<std::string>("_self"), nullptr)));
    }
    members.push_back(std::move(step));

    auto klass = lower.at(std::make_unique<ClassDeclStmt>(className, std::nullopt, std::nullopt, std::move(members)));
    const Symbol* frameBase = resolveQualifiedOrLocal(asyncName("_Frame"), statement->line, statement->column);
    if (!frameBase) return;
    klass->parentType = lower.typeNode(frameBase->type);
    klass->parentClassName = "_Frame";

    const SemanticType frameType = SemanticType::userDefined(className, currentModule, UserTypeKind::Class);
    SemanticType::setShownName(frameType.key(), "an async function's frame");

    auto* classPointer = klass.get();
    {
        const FillInContext saved = enterTemplateModule(currentModule);
        Symbol symbol;
        symbol.kind = SymbolKind::Class;
        symbol.name = className;
        symbol.moduleId = currentModule;
        symbol.isGlobal = true;
        symbol.line = statement->line;
        symbol.column = statement->column;
        symbol.type = frameType;
        if (declareSymbol(symbol, statement->line, statement->column)) {
            ClassInfo info;
            info.name = className;
            info.moduleId = currentModule;
            info.decl = classPointer;
            classKeys.push_back(frameType.key());
            classes.emplace(frameType.key(), std::move(info));
            collectClassMembers(classes.at(frameType.key()));
            checkClassMembers(classes.at(frameType.key()));
            pendingBodies.push_back({currentModule, classPointer, "", ""});
        }
        leaveTemplateModule(saved);
    }
    instanceDecls.emplace_back(currentModule, std::move(klass));

    // What callers run: makes the frame, and starts it
    std::vector<StmtPtr> ramp;
    const auto newOf = [&](const SemanticType& type, const std::string& shownName) {
        auto made = lower.at(std::make_unique<NewExpr>(lower.name(shownName), std::vector<Argument>{}));
        made->resolvedClass = std::make_shared<SemanticType>(type);
        return made;
    };
    const auto local = [&](const std::string& name) {
        ExprPtr e = lower.at(std::make_unique<IdentifierExpr>(lower.name(name)));
        return e;
    };
    ramp.push_back(lower.at(std::make_unique<VarDeclStmt>(false, lower.typeNode(lower.taskType), std::optional<std::string>("ds_task"), ExprPtr(newOf(lower.taskType, "Task")))));
    ramp.push_back(lower.at(std::make_unique<VarDeclStmt>(false, lower.typeNode(frameType), std::optional<std::string>("ds_frame"), ExprPtr(newOf(frameType, className)))));
    ramp.push_back(lower.assign(lower.memberOf(local("ds_frame"), "_base"), local("ds_task")));
    ramp.push_back(lower.assign(lower.memberOf(local("ds_frame"), "_task"), local("ds_task")));
    if (lower.hasSelf) {
        ExprPtr self = lower.at(std::make_unique<IdentifierExpr>(Token(TokenType::THIS, "this", statement->line, statement->column)));
        ramp.push_back(lower.assign(lower.memberOf(local("ds_frame"), "_self"), std::move(self)));
    }
    for (const auto& [name, field] : parameters) ramp.push_back(lower.assign(lower.memberOf(local("ds_frame"), field), local(name)));
    ramp.push_back(lower.statement(lower.call(local("ds_frame"), "_step")));
    ramp.push_back(lower.at(std::make_unique<ReturnStmt>(local("ds_task"))));

    fn->body = lower.block(std::move(ramp));
    fn->isAsync = false;
    analyseFunctionBody(fn);
}

} // namespace dessa
