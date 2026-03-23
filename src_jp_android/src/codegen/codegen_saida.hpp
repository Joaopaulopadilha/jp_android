// codegen_saida.hpp
// Transpilacao de saida/saidal para C++ — suporta strings, inteiros, floats, bools e cores

// Este arquivo eh incluido inline dentro da classe Codegen em codegen.hpp

// ======================================================================
// SAIDA (print)
// ======================================================================

void emit_saida(const SaidaStmt& node) {
    auto type = infer_expr_type(*node.value);
    std::string val = emit_expr(*node.value);
    std::string nl = node.newline ? "\\n" : "";

    // Cores ANSI
    std::string cor_prefix;
    std::string cor_suffix;
    if (node.cor != Cor::Nenhuma) {
        switch (node.cor) {
            case Cor::Vermelho: cor_prefix = "\\033[31m"; break;
            case Cor::Verde:    cor_prefix = "\\033[32m"; break;
            case Cor::Amarelo:  cor_prefix = "\\033[33m"; break;
            case Cor::Azul:     cor_prefix = "\\033[34m"; break;
            default: break;
        }
        if (!cor_prefix.empty()) cor_suffix = "\\033[0m";
    }

    if (type == RuntimeType::String) {
        emit_line("printf(\"" + cor_prefix + "%s" + cor_suffix + nl + "\", " + val + ");");
    } else if (type == RuntimeType::Float) {
        emit_line("printf(\"" + cor_prefix + "%g" + cor_suffix + nl + "\", " + val + ");");
    } else if (type == RuntimeType::Bool) {
        std::string bool_true = "\"verdadeiro\"";
        std::string bool_false = "\"falso\"";
        if (!lang_config_.bool_true.empty()) bool_true = "\"" + lang_config_.bool_true + "\"";
        if (!lang_config_.bool_false.empty()) bool_false = "\"" + lang_config_.bool_false + "\"";
        emit_line("printf(\"" + cor_prefix + "%s" + cor_suffix + nl + "\", " +
                  val + " ? " + bool_true + " : " + bool_false + ");");
    } else {
        emit_line("printf(\"" + cor_prefix + "%lld" + cor_suffix + nl + "\", (long long)" + val + ");");
    }
}
