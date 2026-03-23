// codegen_nativo.hpp
// Transpilacao de bibliotecas nativas para C++
//
// Tipo estatico: gera extern "C" declarations, compila o .cpp da lib junto
// Tipo dinamico: gera dlopen/dlsym em runtime

// Este arquivo eh incluido inline dentro da classe Codegen em codegen.hpp

// ======================================================================
// ESTRUTURA DE FUNCAO NATIVA
// ======================================================================

struct NativeFuncInfo {
    std::string name;
    std::string retorno;
    std::vector<std::string> params;
    std::string lib_name;
    bool is_static;  // true = estatico (extern C), false = dinamico (dlopen)
};

std::unordered_map<std::string, NativeFuncInfo> native_funcs_;
std::unordered_set<std::string> native_libs_loaded_;
std::vector<std::string> native_init_code_;       // codigo dlopen/dlsym (dinamico)
std::vector<std::string> native_extern_code_;     // extern "C" declarations (estatico)
std::vector<std::string> native_static_sources_;  // caminhos dos .cpp pra compilar junto
bool has_dynamic_natives_ = false;

// ======================================================================
// PARSER JSON MINIMO
// ======================================================================

static std::string json_skip_ws(const std::string& json, size_t& pos) {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
           json[pos] == '\n' || json[pos] == '\r')) pos++;
    return "";
}

static std::string json_read_string(const std::string& json, size_t& pos) {
    json_skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    if (pos < json.size()) pos++;
    return result;
}

struct NativeJsonFunc {
    std::string nome;
    std::string retorno;
    std::vector<std::string> params;
};

struct NativeJsonInfo {
    std::string tipo;  // "estatico" ou "dinamico"
    std::vector<NativeJsonFunc> funcs;
};

NativeJsonInfo parse_native_json(const std::string& json_content) {
    NativeJsonInfo info;

    // Parse "tipo"
    size_t tipo_pos = json_content.find("\"tipo\"");
    if (tipo_pos != std::string::npos) {
        size_t p = json_content.find(':', tipo_pos);
        if (p != std::string::npos) {
            p++;
            info.tipo = json_read_string(json_content, p);
        }
    }

    // Parse "funcoes"
    size_t pos = json_content.find("\"funcoes\"");
    if (pos == std::string::npos) return info;
    pos = json_content.find('[', pos);
    if (pos == std::string::npos) return info;
    pos++;

    while (pos < json_content.size()) {
        json_skip_ws(json_content, pos);
        if (json_content[pos] == ']') break;
        if (json_content[pos] == ',') { pos++; continue; }
        if (json_content[pos] != '{') { pos++; continue; }
        pos++;

        NativeJsonFunc func;
        while (pos < json_content.size() && json_content[pos] != '}') {
            json_skip_ws(json_content, pos);
            if (json_content[pos] == ',') { pos++; continue; }
            std::string key = json_read_string(json_content, pos);
            json_skip_ws(json_content, pos);
            if (pos < json_content.size() && json_content[pos] == ':') pos++;
            json_skip_ws(json_content, pos);

            if (key == "nome") {
                func.nome = json_read_string(json_content, pos);
            } else if (key == "retorno") {
                func.retorno = json_read_string(json_content, pos);
            } else if (key == "params") {
                if (pos < json_content.size() && json_content[pos] == '[') {
                    pos++;
                    while (pos < json_content.size() && json_content[pos] != ']') {
                        json_skip_ws(json_content, pos);
                        if (json_content[pos] == ',') { pos++; continue; }
                        if (json_content[pos] == '"') {
                            func.params.push_back(json_read_string(json_content, pos));
                        } else {
                            pos++;
                        }
                    }
                    if (pos < json_content.size()) pos++;
                }
            } else {
                if (json_content[pos] == '"') json_read_string(json_content, pos);
                else while (pos < json_content.size() && json_content[pos] != ',' && json_content[pos] != '}') pos++;
            }
        }
        if (pos < json_content.size()) pos++;
        if (!func.nome.empty()) info.funcs.push_back(func);
    }

    return info;
}

// ======================================================================
// CONVERSAO DE TIPO JP -> C++
// ======================================================================

std::string native_type_to_cpp(const std::string& tipo) {
    if (tipo == "inteiro") return "int64_t";
    if (tipo == "texto")   return "int64_t";
    if (tipo == "decimal") return "double";
    return "int64_t";
}

std::string native_type_to_cpp_real(const std::string& tipo) {
    if (tipo == "inteiro") return "int64_t";
    if (tipo == "texto")   return "const char*";
    if (tipo == "decimal") return "double";
    return "int64_t";
}

RuntimeType native_retorno_to_runtime(const std::string& retorno) {
    if (retorno == "inteiro") return RuntimeType::Int;
    if (retorno == "texto")   return RuntimeType::String;
    if (retorno == "decimal") return RuntimeType::Float;
    return RuntimeType::Int;
}

// ======================================================================
// EMIT NATIVO — processa um NativoStmt (importar biblioteca)
// ======================================================================

void emit_nativo(const NativoStmt& node) {
    std::string lib_path = node.lib_path;

    // Extrair nome da biblioteca do path (ex: "bibliotecas/tempo" -> "tempo")
    std::string lib_name = lib_path;
    size_t last_sep = lib_name.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        lib_name = lib_name.substr(last_sep + 1);
    }

    if (native_libs_loaded_.count(lib_name)) return;

    // Resolver caminho do JSON — tenta varias combinacoes
    std::string json_path;
    std::vector<std::string> search_paths = {
        // lib_path pode ja ser o caminho completo relativo
        base_dir_ + "/" + lib_path + "/" + lib_name + ".json",
        exe_dir_ + "/" + lib_path + "/" + lib_name + ".json",
        // Ou pode ser so o nome
        base_dir_ + "/bibliotecas/" + lib_name + "/" + lib_name + ".json",
        exe_dir_ + "/bibliotecas/" + lib_name + "/" + lib_name + ".json",
        // Caminho direto
        lib_path + "/" + lib_name + ".json",
    };

    for (auto& p : search_paths) {
        std::ifstream test(p);
        if (test.is_open()) {
            json_path = p;
            test.close();
            break;
        }
    }

    if (json_path.empty()) {
        std::cerr << "Aviso: Biblioteca nativa '" << lib_name << "' nao encontrada (JSON)." << std::endl;
        return;
    }

    // Ler e parsear JSON
    std::ifstream json_file(json_path);
    std::string json_content((std::istreambuf_iterator<char>(json_file)),
                              std::istreambuf_iterator<char>());
    json_file.close();

    auto json_info = parse_native_json(json_content);
    if (json_info.funcs.empty()) {
        std::cerr << "Aviso: Nenhuma funcao encontrada em " << json_path << std::endl;
        return;
    }

    native_libs_loaded_.insert(lib_name);
    bool is_static = (json_info.tipo == "estatico");

    // Resolver caminho do .cpp fonte (pra estatico)
    std::string lib_dir = json_path.substr(0, json_path.find_last_of("/\\"));
    std::string cpp_source = lib_dir + "/" + lib_name + ".cpp";

    if (is_static) {
        // ============================================================
        // ESTATICO: gera extern "C" declarations
        // O .cpp da lib sera compilado junto pelo clang
        // ============================================================
        for (auto& func : json_info.funcs) {
            std::string ret_cpp = native_type_to_cpp_real(func.retorno);
            std::string params_cpp;
            for (size_t i = 0; i < func.params.size(); i++) {
                if (i > 0) params_cpp += ", ";
                params_cpp += native_type_to_cpp(func.params[i]);
            }
            if (params_cpp.empty()) params_cpp = "void";

            native_extern_code_.push_back(
                "extern \"C\" " + ret_cpp + " " + func.nome + "(" + params_cpp + ");");

            NativeFuncInfo info;
            info.name = func.nome;
            info.retorno = func.retorno;
            info.params = func.params;
            info.lib_name = lib_name;
            info.is_static = true;
            native_funcs_[func.nome] = info;
            func_return_types_[func.nome] = native_retorno_to_runtime(func.retorno);
        }

        // Registrar fonte pra compilar junto
        if (std::ifstream(cpp_source).is_open()) {
            native_static_sources_.push_back(cpp_source);
        }

    } else {
        // ============================================================
        // DINAMICO: gera dlopen/dlsym
        // ============================================================
        has_dynamic_natives_ = true;
        std::string so_name = "lib" + lib_name + ".so";
        std::string handle_var = "__lib_" + lib_name;

        native_init_code_.push_back("void* " + handle_var + " = dlopen(\"./" + so_name + "\", RTLD_LAZY);");
        native_init_code_.push_back("if (!" + handle_var + ") " + handle_var +
                                    " = dlopen(\"" + so_name + "\", RTLD_LAZY);");
        native_init_code_.push_back("if (!" + handle_var + ") { "
                                    "fprintf(stderr, \"Erro: Nao foi possivel carregar %s: %s\\n\", \"" +
                                    so_name + "\", dlerror()); exit(1); }");

        for (auto& func : json_info.funcs) {
            std::string typedef_name = "__type_" + func.nome;
            std::string ptr_var = "__fn_" + func.nome;
            std::string ret_cpp = native_type_to_cpp(func.retorno);
            std::string params_cpp;
            for (size_t i = 0; i < func.params.size(); i++) {
                if (i > 0) params_cpp += ", ";
                params_cpp += native_type_to_cpp(func.params[i]);
            }
            if (params_cpp.empty()) params_cpp = "void";

            native_init_code_.push_back("typedef " + ret_cpp + " (*" + typedef_name + ")(" + params_cpp + ");");
            native_init_code_.push_back(typedef_name + " " + ptr_var +
                                        " = (" + typedef_name + ")dlsym(" + handle_var + ", \"" + func.nome + "\");");
            native_init_code_.push_back("if (!" + ptr_var + ") { "
                                        "fprintf(stderr, \"Erro: Funcao '%s' nao encontrada: %s\\n\", \"" +
                                        func.nome + "\", dlerror()); exit(1); }");

            NativeFuncInfo info;
            info.name = func.nome;
            info.retorno = func.retorno;
            info.params = func.params;
            info.lib_name = lib_name;
            info.is_static = false;
            native_funcs_[func.nome] = info;
            func_return_types_[func.nome] = native_retorno_to_runtime(func.retorno);
        }
    }
}

// ======================================================================
// EMITIR HEADERS E INIT DE NATIVAS
// ======================================================================

void emit_native_headers() {
    if (has_dynamic_natives_) {
        emit_line("#include <dlfcn.h>");
        emit_line("");
    }
    for (auto& line : native_extern_code_) {
        emit_line(line);
    }
    if (!native_extern_code_.empty()) emit_line("");
}

void emit_native_init() {
    for (auto& line : native_init_code_) {
        emit_line(line);
    }
    if (!native_init_code_.empty()) emit_line("");
}

// ======================================================================
// ACESSORES
// ======================================================================

bool is_native_func(const std::string& name) {
    return native_funcs_.count(name) > 0;
}

const std::vector<std::string>& get_native_static_sources() const {
    return native_static_sources_;
}

bool has_dynamic_libs() const {
    return has_dynamic_natives_;
}

// ======================================================================
// CHAMADA DE FUNCAO NATIVA
// ======================================================================

std::string emit_native_call(const ChamadaExpr& node) {
    auto& info = native_funcs_[node.name];

    // Montar argumentos
    std::string args;
    for (size_t i = 0; i < node.args.size(); i++) {
        if (i > 0) args += ", ";
        std::string val = emit_expr(*node.args[i]);
        auto arg_type = infer_expr_type(*node.args[i]);

        if (i < info.params.size()) {
            if (info.params[i] == "texto" && arg_type == RuntimeType::String) {
                if (info.is_static) {
                    // Estatico: funcao espera const char* ou int64_t dependendo
                    args += val;
                } else {
                    args += "(int64_t)" + val;
                }
            } else if (info.params[i] == "inteiro") {
                args += "(int64_t)" + val;
            } else if (info.params[i] == "decimal") {
                args += "(double)" + val;
            } else {
                args += val;
            }
        } else {
            args += val;
        }
    }

    std::string call;
    if (info.is_static) {
        // Chamada direta
        call = info.name + "(" + args + ")";
    } else {
        // Via ponteiro de funcao
        call = "__fn_" + info.name + "(" + args + ")";
    }

    // Cast de retorno
    if (info.retorno == "texto") {
        if (info.is_static) {
            return call;  // ja retorna const char*
        }
        return "(const char*)" + call;
    }

    return call;
}