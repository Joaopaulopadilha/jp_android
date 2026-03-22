// codegen_funcoes.hpp
// Transpilacao de funcoes do usuario para C++ — com inferencia de tipos de parametros

// Este arquivo eh incluido inline dentro da classe Codegen em codegen.hpp

// ======================================================================
// MAPA DE TIPOS DE PARAMETROS POR FUNCAO
// ======================================================================

// func_name -> vetor de tipos dos parametros
std::unordered_map<std::string, std::vector<RuntimeType>> func_param_types_;

// ======================================================================
// PRE-SCAN DE CHAMADAS — infere tipos dos parametros
// ======================================================================

void prescan_calls(const StmtList& stmts) {
    for (auto& stmt : stmts) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            // Procurar ChamadaExpr em ExprStmt
            if constexpr (std::is_same_v<T, ExprStmt>) {
                prescan_expr_calls(*node.expr);
            }
            // Procurar em atribuicoes
            else if constexpr (std::is_same_v<T, AssignStmt>) {
                prescan_expr_calls(*node.value);
            }
            // Procurar em saida
            else if constexpr (std::is_same_v<T, SaidaStmt>) {
                prescan_expr_calls(*node.value);
            }
            // Procurar em if
            else if constexpr (std::is_same_v<T, IfStmt>) {
                for (auto& b : node.branches) {
                    if (b.condition) prescan_expr_calls(*b.condition);
                    prescan_calls(b.body);
                }
            }
            // Procurar em loops
            else if constexpr (std::is_same_v<T, EnquantoStmt>) {
                prescan_expr_calls(*node.condition);
                prescan_calls(node.body);
            }
            else if constexpr (std::is_same_v<T, ParaStmt>) {
                prescan_calls(node.body);
            }
            else if constexpr (std::is_same_v<T, RepetirStmt>) {
                prescan_calls(node.body);
            }
            // Procurar em funcoes
            else if constexpr (std::is_same_v<T, FuncaoStmt>) {
                prescan_calls(node.body);
            }
            // Procurar em retorna
            else if constexpr (std::is_same_v<T, RetornaStmt>) {
                if (node.value) prescan_expr_calls(*node.value);
            }
        }, stmt->node);
    }
}

void prescan_expr_calls(const Expr& expr) {
    std::visit([&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ChamadaExpr>) {
            // Registrar tipos dos argumentos
            if (declared_funcs_.count(node.name) &&
                func_param_types_.find(node.name) == func_param_types_.end()) {

                std::vector<RuntimeType> types;
                for (auto& arg : node.args) {
                    types.push_back(infer_expr_type(*arg));
                }
                func_param_types_[node.name] = types;
            }
            // Recursivo nos argumentos
            for (auto& arg : node.args) {
                prescan_expr_calls(*arg);
            }
        }
        else if constexpr (std::is_same_v<T, BinOpExpr>) {
            prescan_expr_calls(*node.left);
            prescan_expr_calls(*node.right);
        }
        else if constexpr (std::is_same_v<T, CmpOpExpr>) {
            prescan_expr_calls(*node.left);
            prescan_expr_calls(*node.right);
        }
        else if constexpr (std::is_same_v<T, LogicOpExpr>) {
            prescan_expr_calls(*node.left);
            prescan_expr_calls(*node.right);
        }
        else if constexpr (std::is_same_v<T, ConcatExpr>) {
            prescan_expr_calls(*node.left);
            prescan_expr_calls(*node.right);
        }
    }, expr.node);
}

// ======================================================================
// HELPERS DE TIPO PARA PARAMETROS
// ======================================================================

std::string param_type_cpp(const std::string& func_name, size_t idx) {
    auto it = func_param_types_.find(func_name);
    if (it != func_param_types_.end() && idx < it->second.size()) {
        return type_to_cpp(it->second[idx]);
    }
    return "long long";
}

RuntimeType param_runtime_type(const std::string& func_name, size_t idx) {
    auto it = func_param_types_.find(func_name);
    if (it != func_param_types_.end() && idx < it->second.size()) {
        return it->second[idx];
    }
    return RuntimeType::Int;
}

std::string build_params_str(const std::string& func_name,
                              const std::vector<std::string>& param_names) {
    std::string params;
    for (size_t i = 0; i < param_names.size(); i++) {
        if (i > 0) params += ", ";
        params += param_type_cpp(func_name, i) + " " + param_names[i];
    }
    return params;
}

// ======================================================================
// FORWARD DECLARATION
// ======================================================================

void emit_function_forward(const FuncaoStmt& node) {
    declared_funcs_.insert(node.name);

    RuntimeType ret_type = preanalyze_return_type(node.body);
    func_return_types_[node.name] = ret_type;

    // Forward declaration — tipos reais serao resolvidos depois do prescan
    // Emitido como placeholder, sera substituido pelo emit_function_forward_final
}

// Emitir forward declarations finais (apos prescan)
void emit_function_forwards_final(const Program& program) {
    for (auto& stmt : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, FuncaoStmt>) {
                RuntimeType ret_type = func_return_types_.count(node.name) ?
                    func_return_types_[node.name] : RuntimeType::Unknown;
                std::string cpp_ret = type_to_cpp(ret_type);
                std::string params = build_params_str(node.name, node.params);
                emit_line(cpp_ret + " " + node.name + "(" + params + ");");
            }
        }, stmt->node);
    }
}

// ======================================================================
// CORPO DA FUNCAO
// ======================================================================

void emit_function(const FuncaoStmt& node) {
    auto saved_vars = var_types_;
    auto saved_declared = declared_vars_;

    // Registrar parametros com tipos inferidos
    for (size_t i = 0; i < node.params.size(); i++) {
        var_types_[node.params[i]] = param_runtime_type(node.name, i);
        declared_vars_.insert(node.params[i]);
    }

    RuntimeType ret_type = func_return_types_.count(node.name) ?
        func_return_types_[node.name] : RuntimeType::Unknown;
    std::string cpp_ret = type_to_cpp(ret_type);
    std::string params = build_params_str(node.name, node.params);

    emit_line(cpp_ret + " " + node.name + "(" + params + ") {");
    indent_++;
    emit_body(node.body);

    if (ret_type == RuntimeType::String) {
        emit_line("return \"\";");
    } else if (ret_type == RuntimeType::Float) {
        emit_line("return 0.0;");
    } else {
        emit_line("return 0;");
    }

    indent_--;
    emit_line("}");

    var_types_ = saved_vars;
    declared_vars_ = saved_declared;
}

// ======================================================================
// RETORNA
// ======================================================================

void emit_retorna(const RetornaStmt& node) {
    if (node.value) {
        emit_line("return " + emit_expr(*node.value) + ";");
    } else {
        emit_line("return 0;");
    }
}

// ======================================================================
// CHAMADA DE FUNCAO (como expressao)
// ======================================================================

std::string emit_call(const ChamadaExpr& node) {
    // Funcoes builtin
    if (node.name == "entrada") {
        std::string buf_name = "__entrada_tmp_" + std::to_string(node.line);
        if (declared_vars_.find(buf_name) == declared_vars_.end()) {
            declared_vars_.insert(buf_name);
            emit_line("static char " + buf_name + "[4096];");
        }
        if (!node.args.empty()) {
            std::string prompt = emit_expr(*node.args[0]);
            emit_line("printf(\"%s\", " + prompt + ");");
            emit_line("fflush(stdout);");
        }
        emit_line("fgets(" + buf_name + ", sizeof(" + buf_name + "), stdin);");
        emit_line("{ size_t __len = strlen(" + buf_name + "); "
                  "if (__len > 0 && " + buf_name + "[__len-1] == '\\n') "
                  + buf_name + "[__len-1] = '\\0'; }");
        return "(const char*)" + buf_name;
    }

    if (node.name == "inteiro" && node.args.size() == 1) {
        std::string val = emit_expr(*node.args[0]);
        auto t = infer_expr_type(*node.args[0]);
        if (t == RuntimeType::String) return "atoll(" + val + ")";
        return "(long long)(" + val + ")";
    }
    if (node.name == "decimal" && node.args.size() == 1) {
        std::string val = emit_expr(*node.args[0]);
        auto t = infer_expr_type(*node.args[0]);
        if (t == RuntimeType::String) return "atof(" + val + ")";
        return "(double)(" + val + ")";
    }
    if (node.name == "texto" && node.args.size() == 1) {
        std::string val = emit_expr(*node.args[0]);
        auto t = infer_expr_type(*node.args[0]);
        if (t == RuntimeType::String) return val;
        std::string buf = "__texto_tmp_" + std::to_string(node.line);
        if (declared_vars_.find(buf) == declared_vars_.end()) {
            declared_vars_.insert(buf);
            emit_line("static char " + buf + "[256];");
        }
        if (t == RuntimeType::Float) {
            emit_line("snprintf(" + buf + ", sizeof(" + buf + "), \"%g\", " + val + ");");
        } else {
            emit_line("snprintf(" + buf + ", sizeof(" + buf + "), \"%lld\", (long long)" + val + ");");
        }
        return "(const char*)" + buf;
    }

    // Funcao nativa?
    if (is_native_func(node.name)) {
        return emit_native_call(node);
    }

    // Chamada normal
    std::string args;
    for (size_t i = 0; i < node.args.size(); i++) {
        if (i > 0) args += ", ";
        args += emit_expr(*node.args[i]);
    }
    return node.name + "(" + args + ")";
}

// ======================================================================
// PRE-ANALISE DE TIPO DE RETORNO
// ======================================================================

RuntimeType preanalyze_return_type(const StmtList& body) {
    for (auto& stmt : body) {
        RuntimeType t = std::visit([&](const auto& node) -> RuntimeType {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, RetornaStmt>) {
                if (node.value) return infer_expr_type(*node.value);
                return RuntimeType::Int;
            }
            else if constexpr (std::is_same_v<T, IfStmt>) {
                for (auto& branch : node.branches) {
                    RuntimeType bt = preanalyze_return_type(branch.body);
                    if (bt != RuntimeType::Unknown) return bt;
                }
            }
            else if constexpr (std::is_same_v<T, EnquantoStmt>) {
                return preanalyze_return_type(node.body);
            }
            else if constexpr (std::is_same_v<T, ParaStmt>) {
                return preanalyze_return_type(node.body);
            }
            else if constexpr (std::is_same_v<T, RepetirStmt>) {
                return preanalyze_return_type(node.body);
            }
            return RuntimeType::Unknown;
        }, stmt->node);
        if (t != RuntimeType::Unknown) return t;
    }
    return RuntimeType::Unknown;
}