// main.cpp
// Entry point do compilador JPLang para Android
//
// Pipeline: .jp -> codegen -> .cpp -> clang (NDK) -> executavel -> adb push -> executa
//
// Compilar (no Windows):
//   g++ -std=c++17 -Wall -Wextra -static -I. -o jp_android.exe src/main.cpp
//
// Uso:
//   jp_android <arquivo.jp>                   Compila e executa no dispositivo (auto-detecta)
//   jp_android <arquivo.jp> -arm64            Forca ARM64
//   jp_android <arquivo.jp> -x86              Forca x86_64 (emulador)
//   jp_android build <arquivo.jp>             Compila em output/
//   jp_android build <arquivo.jp> -arm64      Compila ARM64 em output/

#include "src/frontend/lexer.hpp"
#include "src/frontend/parser.hpp"
#include "src/codegen/codegen.hpp"
#include "src/backend_android/linker_android.hpp"
#include "src/backend_android/apk_builder.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

// ============================================================================
// DIRETORIO DO EXECUTAVEL
// ============================================================================

static std::string g_exe_dir;

static std::string get_exe_dir(const char* argv0) {
#ifdef _WIN32
    {
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH)
            return fs::path(buf).parent_path().string();
    }
#else
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            return fs::path(buf).parent_path().string();
        }
    }
#endif
    fs::path p(argv0);
    if (p.has_parent_path()) return fs::absolute(p.parent_path()).string();
    return fs::current_path().string();
}

// ============================================================================
// LEITURA DE ARQUIVO
// ============================================================================

static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Erro: Nao foi possivel abrir '" << path << "'" << std::endl;
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ============================================================================
// TRANSPILACAO: .jp -> .cpp
// ============================================================================

struct TranspileResult {
    bool success = false;
    std::vector<std::string> static_sources;
    bool has_dynamic_libs = false;
};

static TranspileResult transpile(const std::string& source, const std::string& cpp_path,
                                  const std::string& base_dir) {
    TranspileResult result;

    jplang::Lexer lexer(source, base_dir);
    jplang::Parser parser(lexer, base_dir);

    auto program = parser.parse();
    if (!program.has_value()) {
        std::cerr << "Compilacao abortada devido a erros de sintaxe." << std::endl;
        return result;
    }

    jplang::Codegen codegen;
    codegen.set_exe_dir(g_exe_dir);
    if (!codegen.compile(program.value(), cpp_path, base_dir, parser.lang_config())) {
        std::cerr << "Erro na geracao de codigo." << std::endl;
        return result;
    }

    result.success = true;
    result.static_sources = codegen.static_sources();
    result.has_dynamic_libs = codegen.uses_dynamic_libs();
    return result;
}

// ============================================================================
// COMPILACAO: .cpp -> executavel via clang do NDK
// ============================================================================

static bool compile_with_ndk(const std::string& cpp_path, const std::string& exe_path,
                              const jplang::NdkConfig& ndk, bool has_dynamic_libs = false,
                              const std::vector<std::string>& extra_sources = {}) {
    // Montar o nome do clang
    std::string clang = ndk.toolchain_bin + "/";
    if (ndk.arch == "aarch64") {
        clang += "aarch64-linux-android" + std::to_string(ndk.api_level) + "-clang++";
    } else {
        clang += "x86_64-linux-android" + std::to_string(ndk.api_level) + "-clang++";
    }
#ifdef _WIN32
    clang += ".cmd";
#endif

    if (!fs::exists(clang)) {
        std::cerr << "Erro: Clang nao encontrado: " << clang << std::endl;
        return false;
    }

    // Compilar
    std::string cmd = "\"" + clang + "\" -O2 -o \"" + exe_path + "\" \"" + cpp_path + "\"";

    // Adicionar fontes extras (bibliotecas estaticas)
    for (auto& src : extra_sources) {
        cmd += " \"" + src + "\"";
    }

    if (has_dynamic_libs) {
        cmd += " -ldl";
    } else if (extra_sources.empty()) {
        cmd += " -static";
    } else {
        // Com fontes extras: linkar libc++ estaticamente
        cmd += " -static-libstdc++";
    }

    int ret = std::system(("\"" + cmd + "\"").c_str());
    if (ret != 0) {
        std::cerr << "Erro: Compilacao NDK falhou (codigo " << ret << ")" << std::endl;
        return false;
    }

    return true;
}

// ============================================================================
// MODO RUN: transpila, compila, envia e executa
// ============================================================================

static int mode_run(const std::string& input_path, std::string arch) {
    std::string source = read_file(input_path);
    if (source.empty()) return 1;

    std::string base_dir = fs::path(input_path).parent_path().string();

    // Detectar SDK e configurar PATH
    jplang::SdkConfig sdk = jplang::detect_sdk(g_exe_dir);
    if (!sdk.valid()) {
        std::cerr << "Erro: SDK nao encontrado." << std::endl;
        return 1;
    }
    jplang::setup_sdk_path(sdk);

    // Detectar NDK
    jplang::NdkConfig ndk = jplang::detect_ndk(g_exe_dir);
    if (!ndk.valid()) {
        std::cerr << "Erro: NDK nao encontrado." << std::endl;
        return 1;
    }

    // Garantir dispositivo disponivel
    std::string device_serial = jplang::ensure_device(sdk);
    if (device_serial.empty()) {
        std::cerr << "Erro: Nenhum dispositivo disponivel." << std::endl;
        return 1;
    }

    // Auto-detectar arch se nao especificada
    if (arch == "auto") {
        arch = jplang::detect_device_arch(device_serial);
        if (arch.empty()) {
            std::cerr << "Erro: Nao foi possivel detectar arquitetura do dispositivo." << std::endl;
            return 1;
        }
        std::cout << "[JPLang Android] Arquitetura detectada: " << arch << std::endl;
    }
    ndk.arch = arch;

    // Temporarios
    fs::path temp_dir = "temp_android";
    fs::create_directories(temp_dir);

    fs::path stem = fs::path(input_path).stem();
    fs::path cpp_path = temp_dir / (stem.string() + ".cpp");
    fs::path exe_path = temp_dir / stem.string();

    // 1) Transpilar .jp -> .cpp
    std::cout << "[JPLang Android] Transpilando " << input_path << "..." << std::endl;
    auto tr = transpile(source, cpp_path.string(), base_dir);
    if (!tr.success) {
        fs::remove_all(temp_dir);
        return 1;
    }

    // 2) Compilar .cpp -> executavel com clang do NDK
    std::cout << "[JPLang Android] Compilando C++ (" << arch << ")..." << std::endl;
    if (!compile_with_ndk(cpp_path.string(), exe_path.string(), ndk,
                          tr.has_dynamic_libs, tr.static_sources)) {
        fs::remove_all(temp_dir);
        return 1;
    }

    // 3) Push e executar
    std::cout << "[JPLang Android] Executando no dispositivo..." << std::endl;
    std::cout << "---" << std::endl;
    bool ok = jplang::push_and_run(exe_path.string(), device_serial, stem.string());
    std::cout << "---" << std::endl;

    if (!ok) {
        std::cerr << "[JPLang Android] Execucao falhou." << std::endl;
    }

    fs::remove_all(temp_dir);
    return ok ? 0 : 1;
}

// ============================================================================
// MODO BUILD: transpila e compila em output/
// ============================================================================

static int mode_build(const std::string& input_path, const std::string& arch) {
    std::string source = read_file(input_path);
    if (source.empty()) return 1;

    std::string base_dir = fs::path(input_path).parent_path().string();

    jplang::NdkConfig ndk = jplang::detect_ndk(g_exe_dir);
    if (!ndk.valid()) {
        std::cerr << "Erro: NDK nao encontrado." << std::endl;
        return 1;
    }
    ndk.arch = arch;

    fs::path stem = fs::path(input_path).stem();
    fs::path out_dir = fs::path("output") / stem;
    fs::create_directories(out_dir);

    fs::path cpp_path = out_dir / (stem.string() + ".cpp");
    fs::path exe_path = out_dir / stem.string();

    std::cout << "[JPLang Android] Transpilando " << input_path << "..." << std::endl;
    auto tr = transpile(source, cpp_path.string(), base_dir);
    if (!tr.success) {
        return 1;
    }

    std::cout << "[JPLang Android] Compilando C++ (" << arch << ")..." << std::endl;
    if (!compile_with_ndk(cpp_path.string(), exe_path.string(), ndk,
                          tr.has_dynamic_libs, tr.static_sources)) {
        return 1;
    }

    std::cout << "[JPLang Android] Compilado: " << exe_path.string() << std::endl;
    std::cout << "[JPLang Android] C++ gerado: " << cpp_path.string() << std::endl;
    return 0;
}

// ============================================================================
// MODO APK: transpila, gera APK, instala e abre
// ============================================================================

static int mode_apk(const std::string& input_path, std::string arch) {
    std::string source = read_file(input_path);
    if (source.empty()) return 1;

    std::string base_dir = fs::path(input_path).parent_path().string();

    // Detectar SDK
    jplang::SdkConfig sdk = jplang::detect_sdk(g_exe_dir);
    if (sdk.valid()) jplang::setup_sdk_path(sdk);

    // Detectar NDK
    jplang::NdkConfig ndk = jplang::detect_ndk(g_exe_dir);
    if (!ndk.valid()) {
        std::cerr << "Erro: NDK nao encontrado." << std::endl;
        return 1;
    }

    // Detectar APK toolchain
    jplang::ApkToolchain tc = jplang::detect_apk_toolchain(g_exe_dir);

    if (!tc.valid()) {
        std::cerr << "Erro: Ferramentas de build APK nao encontradas." << std::endl;
        if (tc.aapt2.empty()) std::cerr << "  Faltando: aapt2" << std::endl;
        if (tc.d8_jar.empty()) std::cerr << "  Faltando: d8.jar" << std::endl;
        if (tc.zipalign.empty()) std::cerr << "  Faltando: zipalign" << std::endl;
        if (tc.apksigner_jar.empty()) std::cerr << "  Faltando: apksigner.jar" << std::endl;
        if (tc.java.empty()) std::cerr << "  Faltando: java" << std::endl;
        if (tc.javac.empty()) std::cerr << "  Faltando: javac" << std::endl;
        if (tc.platform_jar.empty()) std::cerr << "  Faltando: android.jar" << std::endl;
        return 1;
    }

    // Garantir dispositivo
    std::string device_serial;
    if (sdk.valid()) {
        device_serial = jplang::ensure_device(sdk);
    }

    // Auto-detectar arch
    if (arch == "auto" && !device_serial.empty()) {
        arch = jplang::detect_device_arch(device_serial);
        if (arch.empty()) arch = "aarch64";
        std::cout << "[APK] Arquitetura detectada: " << arch << std::endl;
    } else if (arch == "auto") {
        arch = "aarch64";
    }
    ndk.arch = arch;

    // Diretorios
    fs::path stem = fs::path(input_path).stem();
    fs::path work_dir = fs::path("temp_apk") / stem;
    fs::path output_apk = fs::path("output") / (stem.string() + ".apk");
    fs::create_directories("output");

    // Transpilar no modo APK
    std::cout << "[APK] Transpilando " << input_path << " (modo APK)..." << std::endl;

    jplang::Lexer lexer(source, base_dir);
    jplang::Parser parser(lexer, base_dir);
    auto program = parser.parse();
    if (!program.has_value()) {
        std::cerr << "Compilacao abortada devido a erros de sintaxe." << std::endl;
        return 1;
    }

    jplang::Codegen codegen;
    codegen.set_exe_dir(g_exe_dir);

    fs::path cpp_path = work_dir / (stem.string() + "_jni.cpp");
    fs::create_directories(work_dir);

    if (!codegen.compile_apk_public(program.value(), cpp_path.string(), base_dir, parser.lang_config())) {
        std::cerr << "Erro na geracao de codigo APK." << std::endl;
        return 1;
    }

    // Encontrar templates
    std::string templates_dir;
    std::vector<std::string> tmpl_search = {
        g_exe_dir + "/templates",
    };
    for (auto& t : tmpl_search) {
        if (fs::exists(t + "/JPLangActivity.java")) {
            templates_dir = t;
            break;
        }
    }
    if (templates_dir.empty()) {
        std::cerr << "Erro: Templates nao encontrados." << std::endl;
        return 1;
    }

    // Configurar build
    jplang::ApkBuildConfig cfg;
    cfg.app_name = stem.string();
    cfg.cpp_path = cpp_path.string();
    cfg.arch = arch;
    cfg.ndk_toolchain_bin = ndk.toolchain_bin;
    cfg.api_level = ndk.api_level;
    cfg.templates_dir = templates_dir;
    cfg.work_dir = work_dir.string();
    cfg.output_apk = output_apk.string();
    cfg.extra_sources = codegen.static_sources();

    // Build!
    bool ok = jplang::build_apk(cfg, tc, device_serial);

    // Limpar temporarios
    fs::remove_all(work_dir);

    if (!ok) {
        std::cerr << "[APK] Build falhou." << std::endl;
        return 1;
    }

    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    g_exe_dir = get_exe_dir(argv[0]);

    if (argc < 2) {
        std::cerr << "JPLang Android Compiler v0.3 (transpiler C++ / APK)" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Uso:" << std::endl;
        std::cerr << "  jp_android <arquivo.jp>              Compila e executa no dispositivo (console)" << std::endl;
        std::cerr << "  jp_android <arquivo.jp> -arm64       Forca ARM64" << std::endl;
        std::cerr << "  jp_android <arquivo.jp> -x86         Forca x86_64" << std::endl;
        std::cerr << "  jp_android build <arquivo.jp>        Compila em output/" << std::endl;
        std::cerr << "  jp_android apk <arquivo.jp>          Gera APK com interface grafica" << std::endl;
        std::cerr << "  jp_android apk <arquivo.jp> -arm64   Gera APK ARM64" << std::endl;
        return 1;
    }

    std::string first_arg = argv[1];

    auto detect_arch = [&](int start) -> std::string {
        for (int i = start; i < argc; i++) {
            std::string flag = argv[i];
            if (flag == "-arm64" || flag == "--arm64" || flag == "-aarch64")
                return "aarch64";
            if (flag == "-x86" || flag == "--x86" || flag == "-x86_64")
                return "x86_64";
        }
        return "auto";  // auto-detecta do dispositivo conectado
    };

    if (first_arg == "build") {
        if (argc < 3) {
            std::cerr << "Erro: Esperado arquivo apos 'build'" << std::endl;
            return 1;
        }
        return mode_build(argv[2], detect_arch(3));
    }

    if (first_arg == "apk") {
        if (argc < 3) {
            std::cerr << "Erro: Esperado arquivo apos 'apk'" << std::endl;
            return 1;
        }
        return mode_apk(argv[2], detect_arch(3));
    }

    return mode_run(first_arg, detect_arch(2));
}