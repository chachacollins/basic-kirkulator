#include <cassert>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <readline/readline.h>
#include <readline/history.h>
#include <stack>
#include <ranges>
#include <variant>
#include <vector>
#include <unordered_map>

namespace fs = std::filesystem;

std::string read_file_to_string(const fs::path& path) 
{
    if (!fs::exists(path) || !fs::is_regular_file(path)) 
    {
        throw std::runtime_error(
                std::format("file {} does not exist or is not a regular file", path.string()));
    }
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error(std::format("could not open file {}", path.string()));
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

enum class TokenKind
{
    Arrow,
    Number,
    Plus,
    Minus,
    Slash,
    Backslash,
    Star,
    LParen,
    RParen,
    Comma,
    DoubleStar,
    Ident,
    Equal,
    Percent,
    Illegal,
    Eof,
};

struct Token
{
    TokenKind   kind;
    const char *start;
    size_t      len;
};

class Lexer
{
    const char *start;
    const char *current;
public:
    Lexer(const std::string& source)
    {
        current = start = source.c_str();
    }

    Token next_token()
    {
        skip_whitespace();
        start = current;
        if (is_at_end()) return make_token(TokenKind::Eof);
        char c = advance();
        if (isdigit(c)) return number();
        if (isalpha(c)) return ident();
        switch (c)
        {
            case '+' : return make_token(TokenKind::Plus);
            case '/' : return make_token(TokenKind::Slash);
            case '*' : return make_token(match('*') ? TokenKind::DoubleStar : TokenKind::Star);
            case '-' : return make_token(match('>') ? TokenKind::Arrow      : TokenKind::Minus);
            case '(' : return make_token(TokenKind::LParen);
            case ')' : return make_token(TokenKind::RParen);
            case ',' : return make_token(TokenKind::Comma);
            case '=' : return make_token(TokenKind::Equal);
            case '%' : return make_token(TokenKind::Percent);
            case '\\': return make_token(TokenKind::Backslash);
            default  : return make_token(TokenKind::Illegal);
        }
    }
private:

    Token make_token(TokenKind kind)
    {
        return {kind, start, (current - start)};
    }

    bool is_at_end()
    {
        return *current == '\0';
    }

    char advance()
    {
        if (is_at_end()) return '\0';
        current++;
        return current[-1];
    }

    char peek()
    {
        return *current;
    }

    bool match(char c)
    {
        if (peek() == c)
        {
            advance();
            return true;
        }
        return false;
    }

    void skip_whitespace()
    {
        for (;;)
        {
            char c = peek();
            switch (c)
            {
                case ' ' :
                case '\r':
                case '\t':
                case '\n':
                    advance();
                    break;
                default: return;
            }
        }
    }

    Token number()
    {
        while (isdigit(peek())) advance();
        if (peek() == '.')
        {
            advance();
            while (isdigit(peek())) advance();
        }
        return make_token(TokenKind::Number);
    }

    Token ident()
    {
        while (isalnum(peek())) advance();
        return make_token(TokenKind::Ident);
    }
};

class Expr
{
public:
    enum class Kind { Number, Unary, Binary, Assign, Var, Fun, Call } kind;
    Expr(Kind k) : kind{k} {}
    virtual ~Expr() = default;
};

struct NumberExpr : public Expr
{
    double number;
    NumberExpr(const Token& token)
    : Expr{Kind::Number}
    {
        assert(token.kind == TokenKind::Number);
        number = std::stod(std::string{token.start, token.len});
    }
};

enum class UnaryOp
{
    Neg
};

struct UnaryExpr : public Expr
{
    UnaryOp               op;
    std::unique_ptr<Expr> expr;

    UnaryExpr(UnaryOp op, std::unique_ptr<Expr> expr)
    : Expr{Kind::Unary}, op{op}, expr{std::move(expr)}
    {
    }
};

enum class BinaryOp
{
    Add,
    Sub,
    Div,
    Mod,
    Mul,
    Exp,
};

struct BinaryExpr : public Expr
{
    BinaryOp              op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;

    BinaryExpr(BinaryOp op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
    : Expr{Kind::Binary}, op{op}, lhs{std::move(lhs)}, rhs{std::move(rhs)}
    {
    }
};

struct VarExpr : public Expr
{
    std::string name;

    VarExpr(std::string name)
    : Expr{Kind::Var}, name{std::move(name)}
    {
    }
};

struct AssignExpr : public Expr
{
    std::string           name;
    std::unique_ptr<Expr> value;

    AssignExpr(std::string name, std::unique_ptr<Expr> value)
    : Expr{Kind::Assign}, name{std::move(name)}, value{std::move(value)}
    {
    }
};

struct FunExpr : public Expr
{
    std::vector<std::string> params;
    std::unique_ptr<Expr>    body;

    FunExpr(std::vector<std::string> params, std::unique_ptr<Expr> body)
    : Expr{Kind::Fun}, params{std::move(params)}, body{std::move(body)}
    {
    }
};

struct CallExpr : public Expr
{
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    CallExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> args)
    : Expr{Kind::Call}, callee{std::move(callee)}, args{std::move(args)}
    {
    }
};

class Parser
{
    Lexer                lexer;
    std::optional<Token> peeked_token;

    Token peek_token()
    {
        if (peeked_token.has_value()) return peeked_token.value();
        peeked_token = lexer.next_token();
        return peeked_token.value();
    }

    Token next_token()
    {
        if (peeked_token.has_value())
        {
            auto token    = peeked_token.value();
            peeked_token  = std::nullopt;
            return token;
        }
        return lexer.next_token();
    }

    std::vector<std::unique_ptr<Expr>> parse_args()
    {
        std::vector<std::unique_ptr<Expr>> args;
        if (peek_token().kind != TokenKind::RParen)
        {
            args.emplace_back(parse_expr(0));
            while (peek_token().kind == TokenKind::Comma)
            {
                next_token();
                args.emplace_back(parse_expr(0));
            }
        }
        Token closing = next_token();
        if (closing.kind != TokenKind::RParen)
            throw std::runtime_error(
                std::format("expected `)` but found `{}`",
                            std::string{closing.start, closing.len}));
        return args;
    }

    std::unique_ptr<Expr> parse_factor()
    {
        Token tok = peek_token();
        switch (tok.kind)
        {
            case TokenKind::Minus:
            {
                next_token();
                auto expr = parse_expr(0);
                return std::make_unique<UnaryExpr>(UnaryOp::Neg, std::move(expr));
            }
            case TokenKind::Number:
                return std::make_unique<NumberExpr>(next_token());
            case TokenKind::LParen:
            {
                next_token();
                auto expr    = parse_expr(0);
                Token closing = next_token();
                if (closing.kind != TokenKind::RParen)
                    throw std::runtime_error(
                        std::format("expected `)` but found `{}`",
                                    std::string{closing.start, closing.len}));
                if (peek_token().kind == TokenKind::LParen)
                {
                    next_token();
                    return std::make_unique<CallExpr>(std::move(expr), parse_args());
                }
                return expr;
            }
            case TokenKind::Ident:
            {
                next_token();
                auto expr = std::make_unique<VarExpr>(std::string{tok.start, tok.len});
                if (peek_token().kind == TokenKind::LParen)
                {
                    next_token();
                    return std::make_unique<CallExpr>(std::move(expr), parse_args());
                }
                return expr;
            }
            case TokenKind::Backslash:
            {
                next_token();
                std::vector<std::string> params;
                for (auto token = peek_token();
                     token.kind != TokenKind::Arrow && token.kind != TokenKind::Eof;
                     token = peek_token())
                {
                    Token param = next_token();
                    if (param.kind != TokenKind::Ident)
                        throw std::runtime_error("expected parameter name in function definition");
                    params.emplace_back(std::string{param.start, param.len});
                }
                if (next_token().kind != TokenKind::Arrow)
                    throw std::runtime_error("expected `->` after parameters in function definition");
                auto body = parse_expr(0);
                return std::make_unique<FunExpr>(std::move(params), std::move(body));
            }
            default:
                throw std::runtime_error(
                    std::format("unexpected token `{}`", std::string{tok.start, tok.len}));
        }
    }

    bool is_binop(TokenKind kind)
    {
        switch (kind)
        {
            case TokenKind::Plus      :
            case TokenKind::Minus     :
            case TokenKind::Slash     :
            case TokenKind::Star      :
            case TokenKind::DoubleStar:
            case TokenKind::Equal     :
            case TokenKind::Percent   :
                return true;
            default: return false;
        }
    }

    BinaryOp parse_binop(TokenKind kind)
    {
        switch (kind)
        {
            case TokenKind::Plus      : return BinaryOp::Add;
            case TokenKind::Minus     : return BinaryOp::Sub;
            case TokenKind::Slash     : return BinaryOp::Div;
            case TokenKind::Percent   : return BinaryOp::Mod;
            case TokenKind::Star      : return BinaryOp::Mul;
            case TokenKind::DoubleStar: return BinaryOp::Exp;
            default: throw std::runtime_error(std::format("Unreachable {}", int(kind)));
        }
    }

    int get_precedence(TokenKind kind)
    {
        switch (kind)
        {
            case TokenKind::Equal:
                return 10;
            case TokenKind::Plus :
            case TokenKind::Minus:
                return 45;
            case TokenKind::Slash     :
            case TokenKind::Star      :
            case TokenKind::Percent   :
            case TokenKind::DoubleStar:
                return 50;
            default: throw std::runtime_error(std::format("Unreachable {}", int(kind)));
        }
    }

    std::unique_ptr<Expr> parse_expr(int curr_prec)
    {
        auto left = parse_factor();
        std::unique_ptr<Expr> right;
        for (auto token = peek_token();
             is_binop(token.kind) && get_precedence(token.kind) >= curr_prec;
             token = peek_token())
        {
            if (token.kind == TokenKind::Equal)
            {
                auto* var = dynamic_cast<VarExpr*>(left.get());
                if (!var) throw std::runtime_error("Invalid lvalue");
                std::string name = var->name;
                next_token();
                right = parse_expr(get_precedence(token.kind));
                left  = std::make_unique<AssignExpr>(name, std::move(right));
            }
            else
            {
                auto op = parse_binop(next_token().kind);
                right = (op == BinaryOp::Exp) ? parse_expr(get_precedence(token.kind))
                                              : parse_expr(get_precedence(token.kind) + 1);
                left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
            }
        }
        return left;
    }
public:
    Parser(Lexer lexer)
    : lexer{lexer}
    {
    }

    std::unique_ptr<Expr> parse()
    {
        auto  expr  = parse_expr(0);
        Token token = peek_token();
        if (token.kind != TokenKind::Eof)
            throw std::runtime_error(
                std::format("Expected EOF but found `{}`",
                            std::string{token.start, token.len}));
        return expr;
    }
};

namespace Op
{
    struct Num    { double value; };
    struct SetVar { std::string name; };
    struct GetVar { std::string name; };
    struct Call   { int arg_count; };
    struct Add{};
    struct Sub{};
    struct Div{};
    struct Mod{};
    struct Mul{};
    struct Exp{};
    struct Neg{};
    struct Log{};
    struct Halt{};
}

using Ops = std::variant<Op::Add, Op::Sub, Op::Mul,  Op::Div,    Op::Exp,
                         Op::Neg, Op::Num, Op::Halt, Op::SetVar, Op::GetVar,
                         Op::Mod, Op::Call, Op::Log>;

struct CalcFn
{
    std::vector<std::string> params;
    std::vector<Ops> chunk;
};

struct CompileCtx
{
    std::vector<CalcFn> functions;
};

void builtin_log(std::vector<Ops>& chunk, CompileCtx& ctx)
{
    int index = (int)ctx.functions.size();
    ctx.functions.emplace_back();
    CalcFn logfn;
    logfn.params.emplace_back("x");
    logfn.chunk.emplace_back(Op::GetVar{"x"});
    logfn.chunk.emplace_back(Op::Log{});
    logfn.chunk.emplace_back(Op::Halt{});
    ctx.functions[index] = std::move(logfn);
    chunk.emplace_back(Op::Num{(double)index});
    chunk.emplace_back(Op::SetVar{"log"});
}

void compile_aux(std::unique_ptr<Expr> expr, std::vector<Ops>& chunk, CompileCtx& ctx)
{
    switch (expr->kind)
    {
        case Expr::Kind::Unary:
        {
            auto unary = static_cast<UnaryExpr*>(expr.get());
            compile_aux(std::move(unary->expr), chunk, ctx);
            chunk.emplace_back(Op::Neg{});
            break;
        }
        case Expr::Kind::Number:
        {
            auto num = static_cast<NumberExpr*>(expr.get());
            chunk.emplace_back(Op::Num{num->number});
            break;
        }
        case Expr::Kind::Binary:
        {
            auto binary = static_cast<BinaryExpr*>(expr.get());
            compile_aux(std::move(binary->lhs), chunk, ctx);
            compile_aux(std::move(binary->rhs), chunk, ctx);
            switch (binary->op)
            {
                case BinaryOp::Add: chunk.emplace_back(Op::Add{}); break;
                case BinaryOp::Sub: chunk.emplace_back(Op::Sub{}); break;
                case BinaryOp::Div: chunk.emplace_back(Op::Div{}); break;
                case BinaryOp::Mul: chunk.emplace_back(Op::Mul{}); break;
                case BinaryOp::Exp: chunk.emplace_back(Op::Exp{}); break;
                case BinaryOp::Mod: chunk.emplace_back(Op::Mod{}); break;
            }
            break;
        }
        case Expr::Kind::Assign:
        {
            auto assign = static_cast<AssignExpr*>(expr.get());
            compile_aux(std::move(assign->value), chunk, ctx);
            chunk.emplace_back(Op::SetVar{assign->name});
            break;
        }
        case Expr::Kind::Var:
        {
            auto var = static_cast<VarExpr*>(expr.get());
            chunk.emplace_back(Op::GetVar{var->name});
            break;
        }
        case Expr::Kind::Fun:
        {
            auto fun = static_cast<FunExpr*>(expr.get());
            int index = (int)ctx.functions.size();
            ctx.functions.emplace_back();
            CalcFn fn;
            fn.params = fun->params;
            compile_aux(std::move(fun->body), fn.chunk, ctx);
            fn.chunk.emplace_back(Op::Halt{});
            ctx.functions[index] = std::move(fn);
            chunk.emplace_back(Op::Num{(double)index});
            break;
        }
        case Expr::Kind::Call:
        {
            auto call = static_cast<CallExpr*>(expr.get());
            for (auto& arg : call->args)
                compile_aux(std::move(arg), chunk, ctx);
            compile_aux(std::move(call->callee), chunk, ctx);
            chunk.emplace_back(Op::Call{(int)call->args.size()});
            break;
        }
        default: throw std::runtime_error("Unreachable compile_aux");
    }
}

template <typename T, typename BinOp>
void binary_op(std::stack<T>& stack, BinOp op)
{
    if (stack.size() < 2)
        throw std::runtime_error("Stack underflow");
    auto b = stack.top(); stack.pop();
    auto a = stack.top(); stack.pop();
    stack.push(op(a, b));
}

class Vm
{
    std::unordered_map<std::string, double> globals;
    CompileCtx ctx;

    double run(const std::vector<Ops>& chunk,
               std::unordered_map<std::string, double>& vars, bool is_top_level)
    {
        std::stack<double> stack;

        for (const auto& op : chunk)
        {
            bool halted = false;
            std::visit([&](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Op::Num>)
                    stack.push(arg.value);
                else if constexpr (std::is_same_v<T, Op::Add>)
                    binary_op(stack, std::plus{});
                else if constexpr (std::is_same_v<T, Op::Sub>)
                    binary_op(stack, std::minus{});
                else if constexpr (std::is_same_v<T, Op::Mul>)
                    binary_op(stack, std::multiplies{});
                else if constexpr (std::is_same_v<T, Op::Div>)
                    binary_op(stack, std::divides{});
                else if constexpr (std::is_same_v<T, Op::Exp>)
                    binary_op(stack, pow);
                else if constexpr (std::is_same_v<T, Op::Mod>)
                    binary_op(stack, fmod);
                else if constexpr (std::is_same_v<T, Op::Log>)
                {
                    if (stack.size() < 1) throw std::runtime_error("Stack underflow (log)");
                    auto a = stack.top(); stack.pop();
                    stack.push(log10f(a));
                }
                else if constexpr (std::is_same_v<T, Op::Neg>)
                {
                    if (stack.size() < 1) throw std::runtime_error("Stack underflow");
                    auto a = stack.top(); stack.pop();
                    stack.push(-a);
                }
                else if constexpr (std::is_same_v<T, Op::GetVar>)
                {
                    auto it = vars.find(arg.name);
                    if (it != vars.end()) { stack.push(it->second); return; }
                    auto git = globals.find(arg.name);
                    if (git != globals.end()) { stack.push(git->second); return; }
                    throw std::runtime_error(std::format("unknown variable `{}`", arg.name));
                }
                else if constexpr (std::is_same_v<T, Op::SetVar>)
                {
                    if (stack.size() < 1) throw std::runtime_error("Stack underflow");
                    double value   = stack.top();
                    vars[arg.name] = value;
                    if (is_top_level) globals[arg.name] = value;
                }
                else if constexpr (std::is_same_v<T, Op::Call>)
                {
                    if (stack.size() < 1) throw std::runtime_error("Stack underflow (callee)");
                    int fn_index = (int)stack.top(); stack.pop();
                    if (fn_index < 0 || fn_index >= (int)ctx.functions.size())
                        throw std::runtime_error(
                            std::format("invalid function index {}", fn_index));
                    const CalcFn& fn = ctx.functions[fn_index];
                    if (arg.arg_count != (int)fn.params.size())
                        throw std::runtime_error(
                            std::format("function expects {} arg(s) but got {}",
                                        fn.params.size(), arg.arg_count));
                    std::unordered_map<std::string, double> callee_vars;
                    for (int i = (int)fn.params.size() - 1; i >= 0; --i)
                    {
                        if (stack.size() < 1) throw std::runtime_error("Stack underflow (args)");
                        callee_vars[fn.params[i]] = stack.top(); stack.pop();
                    }
                    double result = run(fn.chunk, callee_vars, false);
                    stack.push(result);
                }
                else if constexpr (std::is_same_v<T, Op::Halt>)
                    halted = true;
                else
                    static_assert(false, "non-exhaustive visitor!");
            }, op);
            if (halted) break;
        }
        if (stack.size() < 1) throw std::runtime_error("expression produced no value");
        return stack.top();
    }


public:
    Vm() 
    {
        std::vector<Ops> chunk{};
        builtin_log(chunk, ctx);
        chunk.emplace_back(Op::Halt{});
        run(chunk, globals, false);
    }

    void eval(const std::string& src)
    {
        Lexer  lexer{src};
        Parser parser{lexer};
        auto expr = parser.parse();
        std::vector<Ops> chunk{};
        compile_aux(std::move(expr), chunk, ctx);
        chunk.emplace_back(Op::Halt{});
        double result = run(chunk, globals, true);
        std::cout << result << "\n";
    }
};

int main(void)
{
    Vm vm;
    auto stdlib = read_file_to_string("std.bk");
    std::streambuf* original_buf = std::cout.rdbuf(nullptr);
    for (auto s : std::views::split(stdlib, '\n'))
    {
        auto line = std::string{s.begin(), s.end()};
        if (line.empty() || line.starts_with('#')) continue;
        vm.eval(line);
    }
    std::cout.rdbuf(original_buf);
    while (true)
    {
        char *line = readline("> ");
        std::string src{line};
        free(line);
        if (src == "quit") break;
        add_history(src.c_str());
        try { vm.eval(src); }
        catch (const std::exception& e) { std::cout << e.what() << std::endl; }
    }
    return 0;
}
