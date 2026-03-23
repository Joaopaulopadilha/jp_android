// apk_builder.hpp
// Pipeline de geracao de APK para JPLang Android
//
// Etapas:
//   1. Copiar templates (Activity.java, AndroidManifest.xml)
//   2. Substituir placeholders (nome do app)
//   3. Compilar .cpp -> .so via clang do NDK
//   4. Compilar .java -> .class via javac
//   5. Converter .class -> classes.dex via d8
//   6. Empacotar APK via aapt2
//   7. Adicionar .dex e .so ao APK
//   8. Alinhar via zipalign
//   9. Assinar via apksigner (debug keystore)
//  10. Instalar e executar via adb

#ifndef JPLANG_APK_BUILDER_HPP
#define JPLANG_APK_BUILDER_HPP

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace jplang {

// ============================================================================
// CONFIGURACAO DO BUILD
// ============================================================================

struct ApkBuildConfig {
    std::string app_name;          // nome do app (ex: "teste")
    std::string cpp_path;          // .cpp gerado pelo codegen
    std::string arch;              // "aarch64" ou "x86_64"

    // Diretorios de ferramentas
    std::string ndk_toolchain_bin; // caminho do bin do toolchain NDK
    int api_level = 30;
    std::string build_tools_dir;   // sdk/build-tools/30.0.3/
    std::string platform_jar;     // sdk/platforms/android-30/android.jar
    std::string java_home;         // jdk path
    std::string templates_dir;     // templates/ (Activity.java, Manifest)

    // Diretorio de trabalho e saida
    std::string work_dir;          // temp de build
    std::string output_apk;        // caminho final do .apk

    // Fontes extras (bibliotecas estaticas)
    std::vector<std::string> extra_sources;
};

// ============================================================================
// HELPERS
// ============================================================================

static int run_cmd(const std::string& cmd, bool show_output = false) {
    if (show_output) {
        std::cout << "  > " << cmd << std::endl;
    }
#ifdef _WIN32
    // Windows system() precisa de aspas extras pra paths com espacos
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

static bool write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << content;
    f.close();
    return true;
}

static std::string read_file_str(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

// ============================================================================
// DETECTAR BUILD TOOLS
// ============================================================================

struct ApkToolchain {
    std::string aapt2;
    std::string d8_jar;       // d8.jar (chamado via java -cp)
    std::string zipalign;
    std::string apksigner_jar; // apksigner.jar (chamado via java -jar)
    std::string java;          // java.exe
    std::string javac;
    std::string keytool;
    std::string platform_jar;

    bool valid() const {
        return !aapt2.empty() && !d8_jar.empty() && !zipalign.empty() &&
               !apksigner_jar.empty() && !javac.empty() && !java.empty() &&
               !platform_jar.empty();
    }
};

static ApkToolchain detect_apk_toolchain(const std::string& exe_dir) {
    ApkToolchain tc;

    // Procurar build-tools
    std::vector<std::string> bt_search = {
        exe_dir + "/sdk/build-tools/30.0.3",
        exe_dir + "/../sdk/build-tools/30.0.3",
        exe_dir + "/sdk/build-tools/30.0.3",
    };
    // Procurar todas as versoes disponiveis
    std::string sdk_bt_dir;
    for (auto& p : bt_search) {
        if (fs::exists(p)) { sdk_bt_dir = p; break; }
    }
    if (sdk_bt_dir.empty()) {
        // Procurar qualquer versao
        std::vector<std::string> sdk_dirs = {
            exe_dir + "/sdk/build-tools",
            exe_dir + "/../sdk/build-tools",
        };
        for (auto& sd : sdk_dirs) {
            if (fs::exists(sd)) {
                for (auto& entry : fs::directory_iterator(sd)) {
                    if (entry.is_directory()) {
                        sdk_bt_dir = entry.path().string();
                        break;
                    }
                }
            }
            if (!sdk_bt_dir.empty()) break;
        }
    }

    if (!sdk_bt_dir.empty()) {
#ifdef _WIN32
        tc.aapt2 = sdk_bt_dir + "/aapt2.exe";
        tc.zipalign = sdk_bt_dir + "/zipalign.exe";
#else
        tc.aapt2 = sdk_bt_dir + "/aapt2";
        tc.zipalign = sdk_bt_dir + "/zipalign";
#endif
        // d8.jar
        if (fs::exists(sdk_bt_dir + "/lib/d8.jar")) {
            tc.d8_jar = sdk_bt_dir + "/lib/d8.jar";
        } else if (fs::exists(sdk_bt_dir + "/d8.jar")) {
            tc.d8_jar = sdk_bt_dir + "/d8.jar";
        }
        // apksigner.jar
        if (fs::exists(sdk_bt_dir + "/lib/apksigner.jar")) {
            tc.apksigner_jar = sdk_bt_dir + "/lib/apksigner.jar";
        } else if (fs::exists(sdk_bt_dir + "/apksigner.jar")) {
            tc.apksigner_jar = sdk_bt_dir + "/apksigner.jar";
        }
    }

    // Procurar platform jar
    std::vector<std::string> plat_search = {
        exe_dir + "/sdk/platforms/android-30/android.jar",
    };
    for (auto& p : plat_search) {
        if (fs::exists(p)) { tc.platform_jar = p; break; }
    }

    // Procurar JDK
    std::vector<std::string> jdk_search;
    std::vector<std::string> jdk_bases = {
        exe_dir + "/jdk",
    };
    for (auto& jdk_base : jdk_bases) {
        if (fs::exists(jdk_base)) {
            for (auto& entry : fs::directory_iterator(jdk_base)) {
                if (entry.is_directory()) {
                    jdk_search.push_back(entry.path().string());
                }
            }
        }
    }
    for (auto& jdk : jdk_search) {
#ifdef _WIN32
        std::string java_path = jdk + "/bin/java.exe";
        std::string javac_path = jdk + "/bin/javac.exe";
        std::string keytool_path = jdk + "/bin/keytool.exe";
#else
        std::string java_path = jdk + "/bin/java";
        std::string javac_path = jdk + "/bin/javac";
        std::string keytool_path = jdk + "/bin/keytool";
#endif
        if (fs::exists(javac_path)) {
            tc.java = java_path;
            tc.javac = javac_path;
            tc.keytool = keytool_path;
            break;
        }
    }

    return tc;
}

// ============================================================================
// GERAR DEBUG KEYSTORE
// ============================================================================

static std::string ensure_debug_keystore(const std::string& exe_dir, const std::string& keytool) {
    std::string ks_path = exe_dir + "/debug.keystore";
    if (fs::exists(ks_path)) return ks_path;

    std::cout << "[APK] Gerando debug keystore..." << std::endl;
    std::string cmd = "\"" + keytool + "\" -genkeypair -keystore \"" + ks_path + "\""
                      " -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000"
                      " -storepass android -keypass android"
                      " -dname \"CN=JPLang Debug, OU=Debug, O=JPLang, L=BR, ST=PR, C=BR\"";
    int ret = run_cmd(cmd);
    if (ret != 0) {
        std::cerr << "Erro: Nao foi possivel gerar debug keystore." << std::endl;
        return "";
    }
    return ks_path;
}

// ============================================================================
// BUILD APK
// ============================================================================

static bool build_apk(const ApkBuildConfig& cfg, const ApkToolchain& tc,
                       const std::string& device_serial) {

    std::string work = fs::absolute(cfg.work_dir).string();
    // Normalizar separadores pra /
    for (auto& c : work) { if (c == '\\') c = '/'; }
    fs::create_directories(work);
    fs::create_directories(work + "/java");
    fs::create_directories(work + "/classes");
    fs::create_directories(work + "/apk");
    fs::create_directories(work + "/lib");

    std::string abi = (cfg.arch == "aarch64") ? "arm64-v8a" : "x86_64";
    fs::create_directories(work + "/lib/" + abi);

    // ================================================================
    // 1. COMPILAR C++ -> .so (shared library)
    // ================================================================
    std::cout << "[APK] Compilando C++ -> .so (" << cfg.arch << ")..." << std::endl;

    std::string clang = cfg.ndk_toolchain_bin + "/";
    if (cfg.arch == "aarch64") {
        clang += "aarch64-linux-android" + std::to_string(cfg.api_level) + "-clang++";
    } else {
        clang += "x86_64-linux-android" + std::to_string(cfg.api_level) + "-clang++";
    }
#ifdef _WIN32
    clang += ".cmd";
#endif

    std::string so_path = work + "/lib/" + abi + "/libjplang_app.so";
    std::string cmd = "\"" + clang + "\" -shared -fPIC -O2 -o \"" + so_path + "\" \"" + cfg.cpp_path + "\"";

    // Fontes extras (bibliotecas estaticas)
    for (auto& src : cfg.extra_sources) {
        cmd += " \"" + src + "\"";
    }

    cmd += " -llog -static-libstdc++";

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: Compilacao da .so falhou." << std::endl;
        return false;
    }

    // ================================================================
    // 2. PREPARAR JAVA
    // ================================================================
    std::cout << "[APK] Compilando Java..." << std::endl;

    // Copiar Activity.java
    std::string java_src = cfg.templates_dir + "/JPLangActivity.java";
    std::string java_dst = work + "/java/JPLangActivity.java";

    if (!fs::exists(java_src)) {
        std::cerr << "Erro: Template JPLangActivity.java nao encontrado em " << cfg.templates_dir << std::endl;
        return false;
    }
    fs::copy_file(java_src, java_dst, fs::copy_options::overwrite_existing);

    // Preparar AndroidManifest.xml com nome do app
    std::string manifest_src = cfg.templates_dir + "/AndroidManifest.xml";
    std::string manifest_content = read_file_str(manifest_src);
    if (manifest_content.empty()) {
        std::cerr << "Erro: Template AndroidManifest.xml nao encontrado." << std::endl;
        return false;
    }
    manifest_content = replace_all(manifest_content, "JPLANG_APP_NAME", cfg.app_name);
    std::string manifest_dst = work + "/AndroidManifest.xml";
    write_file(manifest_dst, manifest_content);

    // Compilar .java -> .class
    cmd = "\"" + tc.javac + "\" -source 8 -target 8"
          " -classpath \"" + tc.platform_jar + "\""
          " -d \"" + work + "/classes\""
          " \"" + java_dst + "\"";

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: Compilacao Java falhou." << std::endl;
        return false;
    }

    // ================================================================
    // 3. CONVERTER .class -> classes.dex
    // ================================================================
    std::cout << "[APK] Gerando DEX..." << std::endl;

    // d8 precisa de JAVA_HOME
    std::string java_home = fs::path(tc.javac).parent_path().parent_path().string();
#ifdef _WIN32
    _putenv_s("JAVA_HOME", java_home.c_str());
#else
    setenv("JAVA_HOME", java_home.c_str(), 1);
#endif

    // Converter caminhos pra absolutos (d8 pode ter problemas com relativos)
    std::string abs_platform = fs::absolute(tc.platform_jar).string();
    std::string abs_apk_dir = fs::absolute(work + "/apk").string();

    // Coletar todos os .class
    std::string class_files;
    std::string class_dir = work + "/classes/com/jplang/app";
    for (auto& entry : fs::directory_iterator(class_dir)) {
        if (entry.path().extension() == ".class") {
            class_files += " \"" + fs::absolute(entry.path()).string() + "\"";
        }
    }

    // Chamar d8 via java diretamente (d8.bat nao funciona sem find_java.bat)
    cmd = "\"" + tc.java + "\" -cp \"" + tc.d8_jar + "\""
          " com.android.tools.r8.D8"
          " --lib \"" + abs_platform + "\""
          " --output \"" + abs_apk_dir + "\""
          + class_files;

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: d8 (DEX) falhou." << std::endl;
        return false;
    }

    // Verificar se classes.dex foi gerado
    if (!fs::exists(abs_apk_dir + "/classes.dex")) {
        std::cerr << "Erro: classes.dex nao foi gerado." << std::endl;
        std::cerr << "  JAVA_HOME=" << java_home << std::endl;
        std::cerr << "  d8_jar=" << tc.d8_jar << std::endl;
        // Listar o que tem no output dir
        if (fs::exists(abs_apk_dir)) {
            for (auto& e : fs::directory_iterator(abs_apk_dir)) {
                std::cerr << "  apk dir: " << e.path().filename() << std::endl;
            }
        }
        return false;
    }

    // ================================================================
    // 4. EMPACOTAR APK com aapt2
    // ================================================================
    std::cout << "[APK] Empacotando APK..." << std::endl;

    // aapt2 link precisa de pelo menos um resource compilado
    // Criar res/values/strings.xml minimo
    fs::create_directories(work + "/res/values");
    write_file(work + "/res/values/strings.xml",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<resources>\n"
        "    <string name=\"app_name\">" + cfg.app_name + "</string>\n"
        "</resources>\n");

    // Compilar resources
    std::string flat_dir = work + "/flat";
    fs::create_directories(flat_dir);

    // aapt2 no Windows precisa de caminhos nativos
    std::string res_file = fs::path(work + "/res/values/strings.xml").make_preferred().string();
    std::string flat_dir_native = fs::path(flat_dir).make_preferred().string();

    cmd = "\"" + tc.aapt2 + "\" compile"
          " -o \"" + flat_dir_native + "\""
          " \"" + res_file + "\"";

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: aapt2 compile falhou." << std::endl;
        return false;
    }

    // Achar o .flat gerado
    std::string flat_file;
    for (auto& entry : fs::directory_iterator(flat_dir)) {
        if (entry.path().extension() == ".flat") {
            flat_file = entry.path().string();
            break;
        }
    }

    // Link — gera o APK base
    std::string base_apk = work + "/base.apk";
    std::string base_apk_native = fs::path(base_apk).make_preferred().string();
    std::string manifest_native = fs::path(manifest_dst).make_preferred().string();
    std::string platform_native = fs::path(tc.platform_jar).make_preferred().string();

    cmd = "\"" + tc.aapt2 + "\" link"
          " -o \"" + base_apk_native + "\""
          " --manifest \"" + manifest_native + "\""
          " -I \"" + platform_native + "\"";

    if (!flat_file.empty()) {
        std::string flat_native = fs::path(flat_file).make_preferred().string();
        cmd += " \"" + flat_native + "\"";
    }

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: aapt2 link falhou." << std::endl;
        return false;
    }

    // ================================================================
    // 5. ADICIONAR .dex e .so ao APK (via zip/jar)
    // ================================================================
    std::cout << "[APK] Adicionando DEX e .so..." << std::endl;

    // Copiar base.apk pra unsigned.apk pra modificar
    std::string unsigned_apk = work + "/unsigned.apk";
    fs::copy_file(base_apk, unsigned_apk, fs::copy_options::overwrite_existing);

    // Usar jar (do JDK) pra adicionar arquivos ao zip
    // jar aceita paths relativos, entao vamos trabalhar no work dir
    std::string jar_tool;
#ifdef _WIN32
    // Extrair diretorio do javac pra achar jar
    std::string jdk_bin = fs::path(tc.javac).parent_path().string();
    jar_tool = jdk_bin + "/jar.exe";
#else
    std::string jdk_bin = fs::path(tc.javac).parent_path().string();
    jar_tool = jdk_bin + "/jar";
#endif

    // Copiar classes.dex e lib/ pra um diretorio temporario de staging
    std::string staging = work + "/staging";
    fs::create_directories(staging + "/lib/" + abi);
    fs::copy_file(work + "/apk/classes.dex", staging + "/classes.dex",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(so_path, staging + "/lib/" + abi + "/libjplang_app.so",
                  fs::copy_options::overwrite_existing);

    // Copiar base.apk pro staging e adicionar os arquivos
    fs::copy_file(base_apk, staging + "/app.apk", fs::copy_options::overwrite_existing);

    // Usar jar pra atualizar o zip
    std::string saved_dir = fs::current_path().string();
    fs::current_path(staging);

    cmd = "\"" + jar_tool + "\" uf app.apk classes.dex lib/" + abi + "/libjplang_app.so";
    int ret = run_cmd(cmd);
    fs::current_path(saved_dir);

    if (ret != 0) {
        std::cerr << "Erro: Falha ao adicionar arquivos ao APK." << std::endl;
        return false;
    }

    // ================================================================
    // 6. ZIPALIGN
    // ================================================================
    std::cout << "[APK] Alinhando APK..." << std::endl;

    std::string aligned_apk = work + "/aligned.apk";
    cmd = "\"" + tc.zipalign + "\" -f -p 4 \"" + staging + "/app.apk\" \"" + aligned_apk + "\"";

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: zipalign falhou." << std::endl;
        return false;
    }

    // ================================================================
    // 7. ASSINAR APK
    // ================================================================
    std::cout << "[APK] Assinando APK..." << std::endl;

    // Garantir keystore
    std::string exe_dir = fs::path(tc.javac).parent_path().parent_path().parent_path().string();
    // Usar o diretorio do work como fallback
    std::string ks = ensure_debug_keystore(
        fs::path(work).parent_path().string(), tc.keytool);
    if (ks.empty()) return false;

    cmd = "\"" + tc.java + "\" -jar \"" + tc.apksigner_jar + "\" sign"
          " --ks \"" + ks + "\""
          " --ks-key-alias androiddebugkey"
          " --ks-pass pass:android"
          " --key-pass pass:android"
          " --out \"" + cfg.output_apk + "\""
          " \"" + aligned_apk + "\"";

    if (run_cmd(cmd) != 0) {
        std::cerr << "Erro: apksigner falhou." << std::endl;
        return false;
    }

    std::cout << "[APK] APK gerado: " << cfg.output_apk << std::endl;

    // ================================================================
    // 8. INSTALAR E EXECUTAR
    // ================================================================
    if (!device_serial.empty()) {
        std::cout << "[APK] Instalando no dispositivo..." << std::endl;

        cmd = "adb -s " + device_serial + " install -r \"" + cfg.output_apk + "\"";
        if (run_cmd(cmd) != 0) {
            std::cerr << "Aviso: Instalacao falhou." << std::endl;
            return true;  // APK foi gerado, so a instalacao falhou
        }

        // Iniciar Activity
        std::cout << "[APK] Iniciando app..." << std::endl;
        cmd = "adb -s " + device_serial + " shell am start -n com.jplang.app/.JPLangActivity";
        run_cmd(cmd);
    }

    return true;
}

} // namespace jplang

#endif // JPLANG_APK_BUILDER_HPP