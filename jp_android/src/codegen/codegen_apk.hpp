// codegen_apk.hpp
// Gera o C++ especifico pra modo APK — funcoes JNI que conectam com a Activity Java
//
// graf_html("...") — define o HTML da interface
// graf_evento(evento, dado) — funcao do usuario que recebe eventos do JS
//
// No JS: jp.evento("click", "btn1") -> chama nativeOnEvent -> chama graf_evento

// Este arquivo eh incluido inline dentro da classe Codegen em codegen.hpp

// ======================================================================
// MODO APK — ESTADO
// ======================================================================

bool apk_mode_ = false;

void set_apk_mode(bool enabled) { apk_mode_ = enabled; }
bool is_apk_mode() const { return apk_mode_; }

// ======================================================================
// COMPILE APK
// ======================================================================

bool compile_apk(const Program& program, const std::string& output_path,
                 const std::string& base_dir = "",
                 const LangConfig& lang_config = LangConfig{}) {
    base_dir_ = base_dir;
    lang_config_ = lang_config;
    apk_mode_ = true;
    out_.str("");
    indent_ = 0;

    // Headers
    emit_line("#include <cstdio>");
    emit_line("#include <cstdlib>");
    emit_line("#include <cstring>");
    emit_line("#include <cstdint>");
    emit_line("#include <string>");
    emit_line("#include <jni.h>");
    emit_line("#include <android/log.h>");
    emit_line("");
    emit_line("#define LOG_TAG \"JPLang\"");
    emit_line("#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)");
    emit_line("");
    emit_line("static char __concat_buf[4096];");
    emit_line("");

    // Pre-scan: nativos
    for (auto& stmt : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, NativoStmt>) {
                emit_nativo(node);
            }
        }, stmt->node);
    }
    emit_native_headers();

    // Pre-scan: registrar funcoes e tipos de retorno
    for (auto& stmt : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, FuncaoStmt>) {
                emit_function_forward(node);
            }
        }, stmt->node);
    }

    // Forcar graf_evento a ter parametros string (sera chamada com const char*)
    if (declared_funcs_.count("graf_evento")) {
        func_param_types_["graf_evento"] = { RuntimeType::String, RuntimeType::String };
    }

    // Pre-scan: inferir tipos dos parametros pelas chamadas
    prescan_calls(program.statements);

    // Emitir forward declarations com tipos corretos
    emit_function_forwards_final(program);
    if (!declared_funcs_.empty()) emit_line("");

    // Emitir funcoes do usuario
    for (auto& stmt : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, FuncaoStmt>) {
                emit_function(node);
                emit_line("");
            }
        }, stmt->node);
    }

    // ================================================================
    // Estado global
    // ================================================================
    emit_line("static std::string __app_html;");
    emit_line("");

    // ================================================================
    // __jplang_init — codigo top-level do .jp
    // ================================================================
    emit_line("static void __jplang_init() {");
    indent_++;

    emit_native_init();

    for (auto& stmt : program.statements) {
        std::visit([&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (!std::is_same_v<T, FuncaoStmt> &&
                          !std::is_same_v<T, NativoStmt>) {
                // Interceptar graf_html()
                if constexpr (std::is_same_v<T, ExprStmt>) {
                    if (is_graf_html_call(*node.expr)) {
                        emit_graf_html(*node.expr);
                        return;
                    }
                }
                emit_stmt(node);
            }
        }, stmt->node);
    }

    indent_--;
    emit_line("}");
    emit_line("");

    // ================================================================
    // JNI: nativeGetHtml
    // ================================================================
    emit_line("extern \"C\" JNIEXPORT jstring JNICALL");
    emit_line("Java_com_jplang_app_JPLangActivity_nativeGetHtml(JNIEnv* env, jclass) {");
    indent_++;
    emit_line("__jplang_init();");
    emit_line("return env->NewStringUTF(__app_html.c_str());");
    indent_--;
    emit_line("}");
    emit_line("");

    // ================================================================
    // JNI: nativeOnEvent
    // ================================================================
    emit_line("extern \"C\" JNIEXPORT jstring JNICALL");
    emit_line("Java_com_jplang_app_JPLangActivity_nativeOnEvent(JNIEnv* env, jclass, jstring jEvent, jstring jData) {");
    indent_++;
    emit_line("const char* evento = env->GetStringUTFChars(jEvent, nullptr);");
    emit_line("const char* dado = env->GetStringUTFChars(jData, nullptr);");
    emit_line("LOGI(\"Evento: %s, Dado: %s\", evento, dado);");
    emit_line("");
    emit_line("const char* resultado = \"\";");
    emit_line("");

    // Se tem graf_evento, chamar
    if (declared_funcs_.count("graf_evento")) {
        emit_line("resultado = graf_evento(evento, dado);");
    }

    emit_line("");
    emit_line("jstring jResult = env->NewStringUTF(resultado);");
    emit_line("env->ReleaseStringUTFChars(jEvent, evento);");
    emit_line("env->ReleaseStringUTFChars(jData, dado);");
    emit_line("return jResult;");
    indent_--;
    emit_line("}");

    // Escrever
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Erro: Nao foi possivel criar " << output_path << std::endl;
        return false;
    }
    file << out_.str();
    file.close();
    return true;
}

// ======================================================================
// HELPERS PARA GRAF
// ======================================================================

bool is_graf_html_call(const Expr& expr) {
    return std::visit([](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ChamadaExpr>) {
            return node.name == "graf_html";
        }
        return false;
    }, expr.node);
}

void emit_graf_html(const Expr& expr) {
    auto& call = std::get<ChamadaExpr>(expr.node);
    if (!call.args.empty()) {
        std::string html = emit_expr(*call.args[0]);
        emit_line("__app_html = " + html + ";");
    }
}