// codegen.hpp
// Gerador de codigo JPLang para Android — transpila para C++
//
// Ao inves de emitir instrucoes diretamente, gera um arquivo .cpp
// que eh compilado pelo clang do NDK pra qualquer arquitetura alvo.
//
// Sub-headers (incluidos inline):
//   codegen_saida.hpp       — saida/saidal com cores
//   codegen_atribuicao.hpp  — atribuicao, entrada, conversoes
//   codegen_funcoes.hpp     — funcoes, chamadas, retorno

#ifndef JPLANG_CODEGEN_CPP_HPP
#define JPLANG_CODEGEN_CPP_HPP

#include "../frontend/ast.hpp"
#include "../frontend/lexer.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <iostream>

namespace jplang {

// ============================================================================
// TIPO EM TEMPO DE COMPILACAO
// ============================================================================

enum class RuntimeType {
    Int,
    Float,
    String,
    Bool,
    Null,
    Unknown
};

// ============================================================================
// CODEGEN — TRANSPILER PARA C++
// ============================================================================

class Codegen {
public:
    Codegen() = default;

    bool compile(const Program& program, const std::string& output_path,
                 const std::string& base_dir = "",
                 const LangConfig& lang_config = LangConfig{}) {
        base_dir_ = base_dir;
        lang_config_ = lang_config;
        out_.str("");
        indent_ = 0;

        // Headers C++
        emit_line("#include <cstdio>");
        emit_line("#include <cstdlib>");
        emit_line("#include <cstring>");
        emit_line("#include <cstdint>");
        emit_line("");

        // Pre-scan: nativos (antes de tudo pra registrar funcoes)
        for (auto& stmt : program.statements) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, NativoStmt>) {
                    emit_nativo(node);
                }
            }, stmt->node);
        }

        // Headers extras (dlopen se tem nativos)
        emit_native_headers();

        emit_line("// Buffers para concatenacao e conversao");
        emit_line("static char __concat_buf[4096];");
        emit_line("");

        // Pre-scan: registrar nomes de funcoes e analisar tipos de retorno
        for (auto& stmt : program.statements) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncaoStmt>) {
                    emit_function_forward(node);
                }
            }, stmt->node);
        }

        // Pre-scan: analisar chamadas pra inferir tipos dos parametros
        prescan_calls(program.statements);

        // Emitir forward declarations com tipos corretos
        emit_function_forwards_final(program);
        if (!declared_funcs_.empty()) emit_line("");

        // Emitir funcoes (fora do main)
        for (auto& stmt : program.statements) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncaoStmt>) {
                    emit_function(node);
                    emit_line("");
                }
            }, stmt->node);
        }

        // main
        emit_line("int main() {");
        indent_++;

        // Inicializar bibliotecas nativas (dlopen/dlsym)
        emit_native_init();

        for (auto& stmt : program.statements) {
            std::visit([&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                // Pular funcoes (emitidas fora) e nativos (processados no pre-scan)
                if constexpr (!std::is_same_v<T, FuncaoStmt> &&
                              !std::is_same_v<T, NativoStmt>) {
                    emit_stmt(node);
                }
            }, stmt->node);
        }

        emit_line("return 0;");
        indent_--;
        emit_line("}");

        // Escrever arquivo
        std::ofstream file(output_path);
        if (!file.is_open()) {
            std::cerr << "Erro: Nao foi possivel criar " << output_path << std::endl;
            return false;
        }
        file << out_.str();
        file.close();
        return true;
    }

    void set_exe_dir(const std::string& dir) { exe_dir_ = dir; }
    void set_debug_mode(bool enabled) { debug_mode_ = enabled; }

    // Acessores pra linkagem (usados pelo main)
    const std::vector<std::string>& static_sources() const { return native_static_sources_; }
    bool uses_dynamic_libs() const { return has_dynamic_natives_; }

    // Compilar no modo APK (gera JNI ao inves de main)
    bool compile_apk_public(const Program& program, const std::string& output_path,
                            const std::string& base_dir = "",
                            const LangConfig& lang_config = LangConfig{}) {
        return compile_apk(program, output_path, base_dir, lang_config);
    }

private:
    std::ostringstream out_;
    int indent_ = 0;
    std::string base_dir_;
    std::string exe_dir_;
    LangConfig lang_config_;
    bool debug_mode_ = false;

    std::unordered_map<std::string, RuntimeType> var_types_;
    std::unordered_set<std::string> declared_vars_;
    std::unordered_set<std::string> declared_funcs_;
    std::unordered_map<std::string, RuntimeType> func_return_types_;

    // ======================================================================
    // HELPERS DE EMISSAO
    // ======================================================================

    void emit_line(const std::string& line) {
        for (int i = 0; i < indent_; i++) out_ << "    ";
        out_ << line << "\n";
    }

    // ======================================================================
    // TYPE INFERENCE
    // ======================================================================

    RuntimeType infer_expr_type(const Expr& expr) {
        return std::visit([&](const auto& node) -> RuntimeType {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, NumberLit>)      return RuntimeType::Int;
            else if constexpr (std::is_same_v<T, FloatLit>)  return RuntimeType::Float;
            else if constexpr (std::is_same_v<T, StringLit>) return RuntimeType::String;
            else if constexpr (std::is_same_v<T, StringInterp>) return RuntimeType::String;
            else if constexpr (std::is_same_v<T, BoolLit>)   return RuntimeType::Bool;
            else if constexpr (std::is_same_v<T, NullLit>)   return RuntimeType::Null;
            else if constexpr (std::is_same_v<T, VarExpr>) {
                auto it = var_types_.find(node.name);
                return (it != var_types_.end()) ? it->second : RuntimeType::Unknown;
            }
            else if constexpr (std::is_same_v<T, BinOpExpr>) {
                auto lt = infer_expr_type(*node.left);
                auto rt = infer_expr_type(*node.right);
                if (node.op == BinOp::Add &&
                    (lt == RuntimeType::String || rt == RuntimeType::String))
                    return RuntimeType::String;
                if (lt == RuntimeType::Float || rt == RuntimeType::Float)
                    return RuntimeType::Float;
                if (node.op == BinOp::Div) return RuntimeType::Float;
                return RuntimeType::Int;
            }
            else if constexpr (std::is_same_v<T, CmpOpExpr>)   return RuntimeType::Bool;
            else if constexpr (std::is_same_v<T, LogicOpExpr>)  return RuntimeType::Bool;
            else if constexpr (std::is_same_v<T, ConcatExpr>)   return RuntimeType::String;
            else if constexpr (std::is_same_v<T, ChamadaExpr>) {
                // Builtins
                if (node.name == "entrada") return RuntimeType::String;
                if (node.name == "inteiro") return RuntimeType::Int;
                if (node.name == "decimal") return RuntimeType::Float;
                if (node.name == "texto")   return RuntimeType::String;
                // Funcoes do usuario
                auto it = func_return_types_.find(node.name);
                return (it != func_return_types_.end()) ? it->second : RuntimeType::Unknown;
            }
            else return RuntimeType::Unknown;
        }, expr.node);
    }

    std::string type_to_cpp(RuntimeType t) {
        switch (t) {
            case RuntimeType::Int:    return "long long";
            case RuntimeType::Float:  return "double";
            case RuntimeType::String: return "const char*";
            case RuntimeType::Bool:   return "long long";
            default:                  return "long long";
        }
    }

    // ======================================================================
    // EXPRESSAO -> STRING C++
    // ======================================================================

    std::string emit_expr(const Expr& expr) {
        return std::visit([&](const auto& node) -> std::string {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, NumberLit>) {
                return std::to_string(node.value) + "LL";
            }
            else if constexpr (std::is_same_v<T, FloatLit>) {
                return std::to_string(node.value);
            }
            else if constexpr (std::is_same_v<T, StringLit>) {
                return "\"" + escape_string(node.value) + "\"";
            }
            else if constexpr (std::is_same_v<T, StringInterp>) {
                return emit_string_interp(node);
            }
            else if constexpr (std::is_same_v<T, BoolLit>) {
                return node.value ? "1LL" : "0LL";
            }
            else if constexpr (std::is_same_v<T, NullLit>) {
                return "0LL";
            }
            else if constexpr (std::is_same_v<T, VarExpr>) {
                return node.name;
            }
            else if constexpr (std::is_same_v<T, BinOpExpr>) {
                return emit_binop(node);
            }
            else if constexpr (std::is_same_v<T, CmpOpExpr>) {
                return emit_cmpop(node);
            }
            else if constexpr (std::is_same_v<T, LogicOpExpr>) {
                return emit_logicop(node);
            }
            else if constexpr (std::is_same_v<T, ConcatExpr>) {
                return emit_concat(*node.left, *node.right);
            }
            else if constexpr (std::is_same_v<T, ChamadaExpr>) {
                return emit_call(node);
            }
            else {
                return "0LL /* tipo nao suportado */";
            }
        }, expr.node);
    }

    // ======================================================================
    // HELPERS DE EXPRESSAO
    // ======================================================================

    std::string escape_string(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\r': out += "\\r"; break;
                default:   out += c; break;
            }
        }
        return out;
    }

    std::string emit_string_interp(const StringInterp& node) {
        std::string fmt;
        std::string args;
        for (auto& part : node.parts) {
            if (!part.is_var) {
                fmt += escape_string(part.value);
            } else {
                auto it = var_types_.find(part.value);
                RuntimeType t = (it != var_types_.end()) ? it->second : RuntimeType::Unknown;
                if (t == RuntimeType::String) fmt += "%s";
                else if (t == RuntimeType::Float) fmt += "%g";
                else fmt += "%lld";
                args += ", " + part.value;
            }
        }
        return "(sprintf(__concat_buf, \"" + fmt + "\"" + args + "), (const char*)__concat_buf)";
    }

    std::string emit_binop(const BinOpExpr& node) {
        auto lt = infer_expr_type(*node.left);
        auto rt = infer_expr_type(*node.right);

        if (node.op == BinOp::Add &&
            (lt == RuntimeType::String || rt == RuntimeType::String)) {
            return emit_concat(*node.left, *node.right);
        }

        std::string left = emit_expr(*node.left);
        std::string right = emit_expr(*node.right);
        std::string op;
        switch (node.op) {
            case BinOp::Add: op = " + "; break;
            case BinOp::Sub: op = " - "; break;
            case BinOp::Mul: op = " * "; break;
            case BinOp::Div: op = " / "; break;
            case BinOp::Mod: op = " % "; break;
        }
        return "(" + left + op + right + ")";
    }

    std::string emit_cmpop(const CmpOpExpr& node) {
        std::string left = emit_expr(*node.left);
        std::string right = emit_expr(*node.right);

        auto lt = infer_expr_type(*node.left);
        auto rt = infer_expr_type(*node.right);

        if (lt == RuntimeType::String && rt == RuntimeType::String) {
            switch (node.op) {
                case CmpOp::Eq: return "strcmp(" + left + ", " + right + ") == 0";
                case CmpOp::Ne: return "strcmp(" + left + ", " + right + ") != 0";
                case CmpOp::Gt: return "strcmp(" + left + ", " + right + ") > 0";
                case CmpOp::Lt: return "strcmp(" + left + ", " + right + ") < 0";
                case CmpOp::Ge: return "strcmp(" + left + ", " + right + ") >= 0";
                case CmpOp::Le: return "strcmp(" + left + ", " + right + ") <= 0";
            }
        }

        std::string op;
        switch (node.op) {
            case CmpOp::Eq: op = " == "; break;
            case CmpOp::Ne: op = " != "; break;
            case CmpOp::Gt: op = " > "; break;
            case CmpOp::Lt: op = " < "; break;
            case CmpOp::Ge: op = " >= "; break;
            case CmpOp::Le: op = " <= "; break;
        }
        return left + op + right;
    }

    std::string emit_logicop(const LogicOpExpr& node) {
        std::string left = emit_expr(*node.left);
        std::string right = emit_expr(*node.right);
        std::string op = (node.op == LogicOp::And) ? " && " : " || ";
        return left + op + right;
    }

    std::string emit_concat(const Expr& left, const Expr& right) {
        auto lt = infer_expr_type(left);
        auto rt = infer_expr_type(right);

        std::string l = emit_expr(left);
        std::string r = emit_expr(right);

        std::string fmt;
        if (lt == RuntimeType::String && rt == RuntimeType::String) fmt = "%s%s";
        else if (lt == RuntimeType::String && rt == RuntimeType::Int) fmt = "%s%lld";
        else if (lt == RuntimeType::Int && rt == RuntimeType::String) fmt = "%lld%s";
        else if (lt == RuntimeType::String && rt == RuntimeType::Float) fmt = "%s%g";
        else if (lt == RuntimeType::Float && rt == RuntimeType::String) fmt = "%g%s";
        else fmt = "%lld%lld";

        return "(sprintf(__concat_buf, \"" + fmt + "\", " + l + ", " + r + "), (const char*)__concat_buf)";
    }

    // ======================================================================
    // DISPATCH DE STATEMENTS
    // ======================================================================

    template<typename T>
    void emit_stmt(const T& node) {
        if constexpr (std::is_same_v<T, AssignStmt>)         emit_assign(node);
        else if constexpr (std::is_same_v<T, SaidaStmt>)     emit_saida(node);
        else if constexpr (std::is_same_v<T, IfStmt>)        emit_if(node);
        else if constexpr (std::is_same_v<T, EnquantoStmt>)  emit_enquanto(node);
        else if constexpr (std::is_same_v<T, ParaStmt>)      emit_para(node);
        else if constexpr (std::is_same_v<T, RepetirStmt>)   emit_repetir(node);
        else if constexpr (std::is_same_v<T, PararStmt>)     emit_line("break;");
        else if constexpr (std::is_same_v<T, ContinuarStmt>) emit_line("continue;");
        else if constexpr (std::is_same_v<T, RetornaStmt>)   emit_retorna(node);
        else if constexpr (std::is_same_v<T, ExprStmt>)      emit_line(emit_expr(*node.expr) + ";");
        else if constexpr (std::is_same_v<T, NativoStmt>)    { /* futuro */ }
        else if constexpr (std::is_same_v<T, FuncaoStmt>)    { /* emitido separadamente */ }
        else if constexpr (std::is_same_v<T, ClasseStmt>)    { /* futuro */ }
        else if constexpr (std::is_same_v<T, AttrSetStmt>)   { /* futuro */ }
        else if constexpr (std::is_same_v<T, IndexSetStmt>)  { /* futuro */ }
    }

    void emit_stmt_dispatch(const Stmt& stmt) {
        std::visit([&](const auto& node) {
            emit_stmt(node);
        }, stmt.node);
    }

    void emit_body(const StmtList& body) {
        for (auto& stmt : body) {
            emit_stmt_dispatch(*stmt);
        }
    }

    // ======================================================================
    // CONTROLE DE FLUXO
    // ======================================================================

    void emit_if(const IfStmt& node) {
        for (size_t i = 0; i < node.branches.size(); i++) {
            auto& branch = node.branches[i];
            if (i == 0) {
                emit_line("if (" + emit_expr(*branch.condition) + ") {");
            } else if (branch.condition) {
                emit_line("} else if (" + emit_expr(*branch.condition) + ") {");
            } else {
                emit_line("} else {");
            }
            indent_++;
            emit_body(branch.body);
            indent_--;
        }
        emit_line("}");
    }

    void emit_enquanto(const EnquantoStmt& node) {
        emit_line("while (" + emit_expr(*node.condition) + ") {");
        indent_++;
        emit_body(node.body);
        indent_--;
        emit_line("}");
    }

    void emit_para(const ParaStmt& node) {
        std::string var = node.var;
        std::string start = emit_expr(*node.start);
        std::string end = emit_expr(*node.end);
        std::string step = node.step ? emit_expr(*node.step) : "1LL";

        var_types_[var] = RuntimeType::Int;
        declared_vars_.insert(var);

        emit_line("for (long long " + var + " = " + start +
                  "; " + var + " < " + end +
                  "; " + var + " += " + step + ") {");
        indent_++;
        emit_body(node.body);
        indent_--;
        emit_line("}");
    }

    void emit_repetir(const RepetirStmt& node) {
        std::string count = emit_expr(*node.count);
        std::string var = "__rep_" + std::to_string(node.line);

        emit_line("for (long long " + var + " = 0; " + var + " < " + count + "; " + var + "++) {");
        indent_++;
        emit_body(node.body);
        indent_--;
        emit_line("}");
    }

    // ======================================================================
    // SUB-HEADERS (incluidos inline dentro da classe)
    // ======================================================================

    #include "codegen_saida.hpp"
    #include "codegen_atribuicao.hpp"
    #include "codegen_funcoes.hpp"
    #include "codegen_nativo.hpp"
    #include "codegen_apk.hpp"
};

} // namespace jplang

#endif // JPLANG_CODEGEN_CPP_HPP