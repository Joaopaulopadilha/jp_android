// codegen_atribuicao.hpp
// Transpilacao de atribuicao de variaveis e entrada (stdin) para C++

// Este arquivo eh incluido inline dentro da classe Codegen em codegen.hpp

// ======================================================================
// ATRIBUICAO
// ======================================================================

void emit_assign(const AssignStmt& node) {
    RuntimeType type = infer_expr_type(*node.value);

    // Detectar chamada a entrada() — leitura do stdin
    if (std::holds_alternative<ChamadaExpr>(node.value->node)) {
        auto& call = std::get<ChamadaExpr>(node.value->node);
        if (call.name == "entrada") {
            emit_entrada(node.name, call);
            return;
        }
        // Detectar conversoes de tipo
        if (call.name == "inteiro" && call.args.size() == 1) {
            emit_conversao_inteiro(node.name, *call.args[0]);
            return;
        }
        if (call.name == "decimal" && call.args.size() == 1) {
            emit_conversao_decimal(node.name, *call.args[0]);
            return;
        }
        if (call.name == "texto" && call.args.size() == 1) {
            emit_conversao_texto(node.name, *call.args[0]);
            return;
        }
    }

    var_types_[node.name] = type;
    std::string val = emit_expr(*node.value);
    std::string cpp_type = type_to_cpp(type);

    if (declared_vars_.find(node.name) == declared_vars_.end()) {
        declared_vars_.insert(node.name);
        emit_line(cpp_type + " " + node.name + " = " + val + ";");
    } else {
        emit_line(node.name + " = " + val + ";");
    }
}

// ======================================================================
// ENTRADA (leitura do stdin)
// ======================================================================

void emit_entrada(const std::string& var_name, const ChamadaExpr& call) {
    var_types_[var_name] = RuntimeType::String;

    // Se tem argumento, imprime o prompt
    if (!call.args.empty()) {
        std::string prompt = emit_expr(*call.args[0]);
        emit_line("printf(\"%s\", " + prompt + ");");
        emit_line("fflush(stdout);");
    }

    // Declara buffer e le com fgets
    if (declared_vars_.find(var_name) == declared_vars_.end()) {
        declared_vars_.insert(var_name);
        // Buffer estatico pra cada variavel de entrada
        std::string buf_name = "__entrada_buf_" + var_name;
        if (declared_vars_.find(buf_name) == declared_vars_.end()) {
            declared_vars_.insert(buf_name);
            emit_line("static char " + buf_name + "[4096];");
        }
        emit_line("fgets(" + buf_name + ", sizeof(" + buf_name + "), stdin);");
        // Remover newline
        emit_line("{ size_t __len = strlen(" + buf_name + "); "
                  "if (__len > 0 && " + buf_name + "[__len-1] == '\\n') "
                  + buf_name + "[__len-1] = '\\0'; }");
        emit_line("const char* " + var_name + " = " + buf_name + ";");
    } else {
        std::string buf_name = "__entrada_buf_" + var_name;
        emit_line("fgets(" + buf_name + ", sizeof(" + buf_name + "), stdin);");
        emit_line("{ size_t __len = strlen(" + buf_name + "); "
                  "if (__len > 0 && " + buf_name + "[__len-1] == '\\n') "
                  + buf_name + "[__len-1] = '\\0'; }");
        emit_line(var_name + " = " + buf_name + ";");
    }
}

// ======================================================================
// CONVERSOES DE TIPO
// ======================================================================

// inteiro("123") -> 123
void emit_conversao_inteiro(const std::string& var_name, const Expr& arg) {
    var_types_[var_name] = RuntimeType::Int;
    std::string val = emit_expr(arg);
    auto arg_type = infer_expr_type(arg);

    std::string converted;
    if (arg_type == RuntimeType::String) {
        converted = "atoll(" + val + ")";
    } else if (arg_type == RuntimeType::Float) {
        converted = "(long long)(" + val + ")";
    } else {
        converted = "(long long)(" + val + ")";
    }

    if (declared_vars_.find(var_name) == declared_vars_.end()) {
        declared_vars_.insert(var_name);
        emit_line("long long " + var_name + " = " + converted + ";");
    } else {
        emit_line(var_name + " = " + converted + ";");
    }
}

// decimal("3.14") -> 3.14
void emit_conversao_decimal(const std::string& var_name, const Expr& arg) {
    var_types_[var_name] = RuntimeType::Float;
    std::string val = emit_expr(arg);
    auto arg_type = infer_expr_type(arg);

    std::string converted;
    if (arg_type == RuntimeType::String) {
        converted = "atof(" + val + ")";
    } else {
        converted = "(double)(" + val + ")";
    }

    if (declared_vars_.find(var_name) == declared_vars_.end()) {
        declared_vars_.insert(var_name);
        emit_line("double " + var_name + " = " + converted + ";");
    } else {
        emit_line(var_name + " = " + converted + ";");
    }
}

// texto(123) -> "123"
void emit_conversao_texto(const std::string& var_name, const Expr& arg) {
    var_types_[var_name] = RuntimeType::String;
    std::string val = emit_expr(arg);
    auto arg_type = infer_expr_type(arg);

    std::string buf_name = "__texto_buf_" + var_name;
    if (declared_vars_.find(buf_name) == declared_vars_.end()) {
        declared_vars_.insert(buf_name);
        emit_line("static char " + buf_name + "[256];");
    }

    if (arg_type == RuntimeType::Float) {
        emit_line("snprintf(" + buf_name + ", sizeof(" + buf_name + "), \"%g\", " + val + ");");
    } else if (arg_type == RuntimeType::String) {
        // Ja eh string, so copia a referencia
        if (declared_vars_.find(var_name) == declared_vars_.end()) {
            declared_vars_.insert(var_name);
            emit_line("const char* " + var_name + " = " + val + ";");
        } else {
            emit_line(var_name + " = " + val + ";");
        }
        return;
    } else {
        emit_line("snprintf(" + buf_name + ", sizeof(" + buf_name + "), \"%lld\", (long long)" + val + ");");
    }

    if (declared_vars_.find(var_name) == declared_vars_.end()) {
        declared_vars_.insert(var_name);
        emit_line("const char* " + var_name + " = " + buf_name + ";");
    } else {
        emit_line(var_name + " = " + buf_name + ";");
    }
}
