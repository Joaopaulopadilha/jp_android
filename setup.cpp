// setup.cpp
// Instalador do ambiente de desenvolvimento JPLang Android
//
// Baixa e instala automaticamente:
//   - Android NDK r27d
//   - Android SDK build-tools 30.0.3
//   - Android SDK platform android-30
//   - Android SDK platform-tools (adb)
//   - Android SDK emulator + system-image x86_64
//
// O JDK (OpenJDK Microsoft 17) ja vem incluido no repositorio.
//
// Compilar:
//   g++ -std=c++17 -Wall -O2 -static -o setup.exe setup.cpp -lwininet
//
// Uso:
//   setup.exe             Instala tudo
//   setup.exe --check     Verifica o que esta instalado

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>

#ifdef _WIN32
    #include <windows.h>
    #include <wininet.h>
    #pragma comment(lib, "wininet.lib")
#else
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

// ============================================================================
// CONFIGURACAO
// ============================================================================

struct SetupConfig {
    std::string base_dir;
    std::string jdk_bin;
    std::string sdkmanager;

    // URLs de download — separadas por plataforma
#ifdef _WIN32
    static constexpr const char* NDK_URL =
        "https://dl.google.com/android/repository/android-ndk-r27d-windows.zip";
    static constexpr const char* NDK_FILE = "android-ndk-r27d-windows.zip";

    static constexpr const char* CMDLINE_TOOLS_URL =
        "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip";
    static constexpr const char* CMDLINE_TOOLS_FILE = "commandlinetools.zip";

    static constexpr const char* JDK_URL =
        "https://aka.ms/download-jdk/microsoft-jdk-17-windows-x64.zip";
    static constexpr const char* JDK_FILE = "microsoft-jdk-17.zip";
#else
    static constexpr const char* NDK_URL =
        "https://dl.google.com/android/repository/android-ndk-r27d-linux.zip";
    static constexpr const char* NDK_FILE = "android-ndk-r27d-linux.zip";

    static constexpr const char* CMDLINE_TOOLS_URL =
        "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip";
    static constexpr const char* CMDLINE_TOOLS_FILE = "commandlinetools.zip";

    static constexpr const char* JDK_URL =
        "https://aka.ms/download-jdk/microsoft-jdk-17-linux-x64.tar.gz";
    static constexpr const char* JDK_FILE = "microsoft-jdk-17.tar.gz";
#endif

    static constexpr const char* NDK_DIR = "android-ndk-r27d";
};

// ============================================================================
// CORES NO TERMINAL
// ============================================================================

#ifdef _WIN32
static void set_color(int color) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(h, (WORD)color);
}
static void color_reset()  { set_color(7); }
static void color_green()  { set_color(10); }
static void color_yellow() { set_color(14); }
static void color_red()    { set_color(12); }
static void color_cyan()   { set_color(11); }
#else
static void color_reset()  { printf("\033[0m"); }
static void color_green()  { printf("\033[32m"); }
static void color_yellow() { printf("\033[33m"); }
static void color_red()    { printf("\033[31m"); }
static void color_cyan()   { printf("\033[36m"); }
#endif

static void print_ok(const std::string& msg) {
    color_green(); printf("  [OK] "); color_reset(); printf("%s\n", msg.c_str());
}
static void print_skip(const std::string& msg) {
    color_yellow(); printf("  [--] "); color_reset(); printf("%s\n", msg.c_str());
}
static void print_fail(const std::string& msg) {
    color_red(); printf("  [!!] "); color_reset(); printf("%s\n", msg.c_str());
}
static void print_info(const std::string& msg) {
    color_cyan(); printf("  [>>] "); color_reset(); printf("%s\n", msg.c_str());
}

// ============================================================================
// DOWNLOAD (Windows — WinINet)
// ============================================================================

#ifdef _WIN32
static bool download_file(const std::string& url, const std::string& dest) {
    print_info("Baixando: " + url);

    HINTERNET hInternet = InternetOpenA("JPLang-Setup/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                         NULL, NULL, 0);
    if (!hInternet) {
        print_fail("Erro ao abrir conexao de internet.");
        return false;
    }

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
                                       INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        print_fail("Erro ao conectar: " + url);
        return false;
    }

    FILE* fp = fopen(dest.c_str(), "wb");
    if (!fp) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        print_fail("Erro ao criar arquivo: " + dest);
        return false;
    }

    char buffer[65536];
    DWORD bytesRead;
    size_t totalBytes = 0;

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        fwrite(buffer, 1, bytesRead, fp);
        totalBytes += bytesRead;

        // Progresso simples
        if (totalBytes % (10 * 1024 * 1024) < sizeof(buffer)) {
            printf("\r        %zu MB baixados...", totalBytes / (1024 * 1024));
            fflush(stdout);
        }
    }

    printf("\r        %zu MB baixados.     \n", totalBytes / (1024 * 1024));
    fclose(fp);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return true;
}
#else
static bool download_file(const std::string& url, const std::string& dest) {
    print_info("Baixando: " + url);
    std::string cmd = "curl -L -o \"" + dest + "\" \"" + url + "\"";
    return (std::system(cmd.c_str()) == 0);
}
#endif

// ============================================================================
// EXTRAIR ARQUIVO (zip ou tar.gz)
// ============================================================================

static bool extract_archive(const std::string& file_path, const std::string& dest_dir) {
    print_info("Extraindo: " + file_path);
    fs::create_directories(dest_dir);

    std::string cmd;

#ifdef _WIN32
    cmd = "powershell -NoProfile -Command \"Expand-Archive -Path '"
          + file_path + "' -DestinationPath '" + dest_dir + "' -Force\"";
#else
    // Detectar se é .tar.gz ou .zip
    if (file_path.find(".tar.gz") != std::string::npos ||
        file_path.find(".tgz") != std::string::npos) {
        cmd = "tar xzf \"" + file_path + "\" -C \"" + dest_dir + "\"";
    } else {
        cmd = "unzip -o -q \"" + file_path + "\" -d \"" + dest_dir + "\"";
    }
#endif

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        print_fail("Erro ao extrair " + file_path);
        return false;
    }
    return true;
}

// ============================================================================
// EXECUTAR COMANDO
// ============================================================================

static int run_cmd(const std::string& cmd) {
#ifdef _WIN32
    // No Windows, system() chama cmd.exe /c <string>
    // Se a string começa com ", cmd.exe remove o primeiro e ultimo " como par,
    // quebrando paths com aspas. Envolver com aspas externas resolve isso.
    std::string wrapped = "\"" + cmd + "\"";
    return std::system(wrapped.c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// ============================================================================
// VERIFICAR COMPONENTES
// ============================================================================

struct ComponentStatus {
    bool jdk = false;
    bool ndk = false;
    bool build_tools = false;
    bool platform = false;
    bool platform_tools = false;
    bool emulator = false;
    bool cmdline_tools = false;
    bool templates = false;
};

static ComponentStatus check_components(const std::string& base) {
    ComponentStatus s;

    // JDK
    std::string jdk_dir = base + "/jdk";
    if (fs::exists(jdk_dir)) {
        for (auto& entry : fs::directory_iterator(jdk_dir)) {
            if (entry.is_directory()) {
#ifdef _WIN32
                if (fs::exists(entry.path() / "bin" / "javac.exe")) s.jdk = true;
#else
                if (fs::exists(entry.path() / "bin" / "javac")) s.jdk = true;
#endif
            }
        }
    }

    // NDK
    if (fs::exists(base + "/ndk")) {
        for (auto& entry : fs::directory_iterator(base + "/ndk")) {
            if (entry.is_directory() &&
                fs::exists(entry.path() / "toolchains")) {
                s.ndk = true;
            }
        }
    }

    // SDK components
    s.build_tools = fs::exists(base + "/sdk/build-tools/30.0.3");
    s.platform = fs::exists(base + "/sdk/platforms/android-30/android.jar");
    s.platform_tools = fs::exists(base + "/sdk/platform-tools");
    s.emulator = fs::exists(base + "/sdk/emulator");
    s.cmdline_tools = fs::exists(base + "/sdk/cmdline-tools/latest/bin");
    s.templates = fs::exists(base + "/templates/JPLangActivity.java");

    return s;
}

static void print_status(const ComponentStatus& s) {
    printf("\n  Componentes:\n");
    s.jdk            ? print_ok("JDK (OpenJDK 17)")       : print_fail("JDK (OpenJDK 17)");
    s.ndk            ? print_ok("NDK (r27d)")              : print_fail("NDK (r27d)");
    s.build_tools    ? print_ok("SDK Build Tools 30.0.3")  : print_fail("SDK Build Tools 30.0.3");
    s.platform       ? print_ok("SDK Platform android-30") : print_fail("SDK Platform android-30");
    s.platform_tools ? print_ok("SDK Platform Tools (adb)"): print_fail("SDK Platform Tools (adb)");
    s.emulator       ? print_ok("Emulador Android")        : print_fail("Emulador Android");
    s.cmdline_tools  ? print_ok("SDK Command-line Tools")  : print_fail("SDK Command-line Tools");
    s.templates      ? print_ok("Templates APK")           : print_fail("Templates APK");
    printf("\n");
}

// ============================================================================
// ENCONTRAR JDK
// ============================================================================

static std::string find_java(const std::string& base) {
    std::string jdk_dir = base + "/jdk";
    if (!fs::exists(jdk_dir)) return "";
    for (auto& entry : fs::directory_iterator(jdk_dir)) {
        if (entry.is_directory()) {
#ifdef _WIN32
            std::string java = (entry.path() / "bin" / "java.exe").string();
#else
            std::string java = (entry.path() / "bin" / "java").string();
#endif
            if (fs::exists(java)) return entry.path().string();
        }
    }
    return "";
}

// ============================================================================
// INSTALAR NDK
// ============================================================================

static bool install_ndk(const std::string& base) {
    print_info("Instalando Android NDK r27d...");
    print_info("(~2.2 GB — pode demorar alguns minutos)");

    std::string zip_path = base + "/" + SetupConfig::NDK_FILE;
    std::string ndk_dir = base + "/ndk";

    if (!download_file(SetupConfig::NDK_URL, zip_path)) return false;
    if (!extract_archive(zip_path, ndk_dir)) return false;

    // Remover zip
    fs::remove(zip_path);

    // Verificar
    if (fs::exists(ndk_dir + "/" + SetupConfig::NDK_DIR + "/toolchains")) {
        print_ok("NDK instalado com sucesso.");
        return true;
    }

    print_fail("NDK extraido mas nao encontrado no local esperado.");
    return false;
}

// ============================================================================
// INSTALAR CMDLINE-TOOLS
// ============================================================================

static bool install_cmdline_tools(const std::string& base) {
    print_info("Instalando SDK Command-line Tools...");

    std::string zip_path = base + "/" + SetupConfig::CMDLINE_TOOLS_FILE;
    std::string sdk_dir = base + "/sdk";
    std::string dest = sdk_dir + "/cmdline-tools/latest";

    fs::create_directories(dest);

    if (!download_file(SetupConfig::CMDLINE_TOOLS_URL, zip_path)) return false;

    // Extrair pra temp
    std::string temp_dir = base + "/_cmdline_temp";
    if (!extract_archive(zip_path, temp_dir)) return false;
    fs::remove(zip_path);

    // Mover conteudo de cmdline-tools/ pra latest/
    std::string extracted = temp_dir + "/cmdline-tools";
    if (fs::exists(extracted)) {
        for (auto& entry : fs::directory_iterator(extracted)) {
            fs::path target = fs::path(dest) / entry.path().filename();
            if (fs::exists(target)) {
                fs::remove_all(target);
            }
            fs::rename(entry.path(), target);
        }
    }
    fs::remove_all(temp_dir);

    print_ok("Command-line Tools instaladas.");
    return true;
}

// ============================================================================
// INSTALAR COMPONENTES VIA SDKMANAGER
// ============================================================================

static bool accept_licenses(const std::string& base, const std::string& java_home) {
    std::string sdk_dir = fs::path(base + "/sdk").make_preferred().string();
    std::string cmd;

#ifdef _WIN32
    _putenv_s("JAVA_HOME", java_home.c_str());
    std::string sdkmanager = sdk_dir + "\\cmdline-tools\\latest\\bin\\sdkmanager.bat";

    std::string bat_path = base + "\\__accept_licenses.bat";
    std::ofstream bat(bat_path);
    bat << "@echo off\n";
    bat << "for /L %%i in (1,1,20) do echo y\n";
    bat.close();

    cmd = bat_path + " | " + sdkmanager + " --sdk_root=" + sdk_dir + " --licenses";
#else
    std::string java_bin = java_home + "/bin/java";
    std::string sdk_jar = sdk_dir + "/cmdline-tools/latest/lib/sdkmanager-classpath.jar";

    cmd = "yes | \"" + java_bin + "\" -cp \"" + sdk_jar + "\""
          " com.android.sdklib.tool.sdkmanager.SdkManagerCli"
          " --sdk_root=\"" + sdk_dir + "\" --licenses";
#endif

    print_info("Aceitando licencas do Android SDK...");
    run_cmd(cmd);

#ifdef _WIN32
    fs::remove(bat_path);
#endif
    return true;
}

static bool install_sdk_component(const std::string& base, const std::string& java_home,
                                   const std::string& component) {
    print_info("Instalando " + component + "...");

    std::string sdk_dir = fs::path(base + "/sdk").make_preferred().string();
    std::string cmd;

#ifdef _WIN32
    _putenv_s("JAVA_HOME", java_home.c_str());
    std::string sdkmanager = sdk_dir + "\\cmdline-tools\\latest\\bin\\sdkmanager.bat";

    if (!fs::exists(sdkmanager)) {
        print_fail("sdkmanager nao encontrado: " + sdkmanager);
        return false;
    }

    cmd = "\"" + sdkmanager + "\" --sdk_root=\"" + sdk_dir + "\" \"" + component + "\"";
#else
    std::string java_bin = java_home + "/bin/java";
    std::string sdk_jar = sdk_dir + "/cmdline-tools/latest/lib/sdkmanager-classpath.jar";

    if (!fs::exists(sdk_jar)) {
        print_fail("sdkmanager jar nao encontrado: " + sdk_jar);
        return false;
    }

    cmd = "\"" + java_bin + "\" -cp \"" + sdk_jar + "\""
          " com.android.sdklib.tool.sdkmanager.SdkManagerCli"
          " --sdk_root=\"" + sdk_dir + "\" '" + component + "'";
#endif

    int ret = run_cmd(cmd);
    if (ret != 0) {
        print_fail("Falha ao instalar " + component);
        return false;
    }

    print_ok(component + " instalado.");
    return true;
}

// ============================================================================
// INSTALAR JDK (OpenJDK Microsoft 17)
// ============================================================================

static bool install_jdk(const std::string& base) {
    print_info("Instalando OpenJDK 17 (Microsoft Build)...");
    print_info("(~170 MB)");

    std::string zip_path = base + "/" + SetupConfig::JDK_FILE;
    std::string jdk_dir = base + "/jdk";

    fs::create_directories(jdk_dir);

    if (!download_file(SetupConfig::JDK_URL, zip_path)) return false;
    if (!extract_archive(zip_path, jdk_dir)) return false;

    fs::remove(zip_path);

    // Verificar — o zip extrai como jdk-17.0.XX+YY dentro de jdk/
    for (auto& entry : fs::directory_iterator(jdk_dir)) {
        if (entry.is_directory()) {
#ifdef _WIN32
            if (fs::exists(entry.path() / "bin" / "javac.exe")) {
#else
            if (fs::exists(entry.path() / "bin" / "javac")) {
#endif
                print_ok("JDK instalado: " + entry.path().filename().string());
                return true;
            }
        }
    }

    print_fail("JDK extraido mas javac nao encontrado.");
    return false;
}

// ============================================================================
// INSTALAR EMULADOR + SYSTEM IMAGE
// ============================================================================

static bool install_emulator(const std::string& base, const std::string& java_home) {
    install_sdk_component(base, java_home, "emulator");
    install_sdk_component(base, java_home, "system-images;android-30;google_apis;x86_64");

    // Criar AVD
    print_info("Criando AVD 'teste_jp'...");

    std::string sdk_dir = fs::path(base + "/sdk").make_preferred().string();
    std::string cmd;

#ifdef _WIN32
    _putenv_s("JAVA_HOME", java_home.c_str());
    std::string avdmanager = sdk_dir + "\\cmdline-tools\\latest\\bin\\avdmanager.bat";
    cmd = "echo no | \"" + avdmanager + "\" create avd"
          " -n teste_jp -k \"system-images;android-30;google_apis;x86_64\""
          " --force";
#else
    // No Linux, replicar exatamente o que o script bin/avdmanager faz:
    // 1. APP_HOME = cmdline-tools/latest (diretorio pai de bin/)
    // 2. CLASSPATH = APP_HOME/lib/avdmanager-classpath.jar
    // 3. -Dcom.android.sdkmanager.toolsdir=$APP_HOME  (note: sdkmanager, nao sdklib)
    // 4. java -classpath $CLASSPATH com.android.sdklib.tool.AvdManagerCli

    std::string java_bin = java_home + "/bin/java";
    std::string app_home = sdk_dir + "/cmdline-tools/latest";
    std::string avd_cp = app_home + "/lib/avdmanager-classpath.jar";

    if (!fs::exists(avd_cp)) {
        print_fail("avdmanager-classpath.jar nao encontrado: " + avd_cp);
        return false;
    }

    // Setar variaveis de ambiente
    setenv("ANDROID_HOME", sdk_dir.c_str(), 1);
    setenv("ANDROID_SDK_ROOT", sdk_dir.c_str(), 1);

    cmd = "echo no | " + java_bin
          + " '-Dcom.android.sdkmanager.toolsdir=" + app_home + "'"
          + " -classpath " + avd_cp
          + " com.android.sdklib.tool.AvdManagerCli"
          + " create avd -n teste_jp"
          + " -k 'system-images;android-30;google_apis;x86_64'"
          + " --force";
#endif

    int avd_ret = run_cmd(cmd);
    if (avd_ret != 0) {
        print_fail("Falha ao criar AVD 'teste_jp' (codigo " + std::to_string(avd_ret) + ")");
        return false;
    }

    print_ok("Emulador configurado.");
    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    // Detectar diretorio base
    std::string base;
#ifdef _WIN32
    {
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
        if (len > 0) base = fs::path(buf).parent_path().string();
    }
#else
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) { buf[len] = '\0'; base = fs::path(buf).parent_path().string(); }
    }
#endif
    if (base.empty()) base = fs::current_path().string();

    // Banner
    printf("\n");
    color_cyan();
    printf("  ============================================\n");
    printf("  JPLang Android - Instalador de Dependencias\n");
    printf("  ============================================\n");
    color_reset();
    printf("\n");
    printf("  Diretorio: %s\n", base.c_str());

    // Check mode
    bool check_only = false;
    if (argc > 1 && std::string(argv[1]) == "--check") {
        check_only = true;
    }

    // Verificar estado atual
    auto status = check_components(base);
    print_status(status);

    if (check_only) return 0;

    // Instalar componentes faltantes
    int installed = 0;
    int failed = 0;

    // 0. JDK (pre-requisito pra tudo que usa sdkmanager)
    if (!status.jdk) {
        if (install_jdk(base)) installed++; else { failed++; }
    } else {
        print_skip("JDK ja instalado.");
    }

    std::string java_home = find_java(base);
    if (java_home.empty()) {
        print_fail("JDK nao disponivel. Nao eh possivel continuar.");
        return 1;
    }
    // Normalizar path pra barras nativas (sdkmanager.bat exige backslash no Windows)
    java_home = fs::path(java_home).make_preferred().string();

#ifndef _WIN32
    // No Linux, se o path tem espacos, o sdkmanager quebra.
    // Criar symlink temporario sem espacos.
    if (java_home.find(' ') != std::string::npos) {
        std::string link_path = "/tmp/jplang_jdk";
        fs::remove_all(link_path);
        fs::create_directory_symlink(java_home, link_path);
        java_home = link_path;
        print_info("Symlink criado: " + link_path + " -> JDK (path com espacos)");
    }
#endif

    print_ok("JDK encontrado: " + java_home);

#ifndef _WIN32
    // Mesma coisa pro SDK — sdkmanager nao aceita espacos no --sdk_root
    std::string base_for_sdk = base;
    if (base.find(' ') != std::string::npos) {
        std::string link_base = "/tmp/jplang_android";
        fs::remove_all(link_base);
        fs::create_directory_symlink(base, link_base);
        base_for_sdk = link_base;
        print_info("Symlink criado: " + link_base + " -> base (path com espacos)");
    }
    // Usar base_for_sdk pras chamadas do sdkmanager
    #define SETUP_BASE base_for_sdk
#else
    #define SETUP_BASE base
#endif

    // 1. NDK
    if (!status.ndk) {
        if (install_ndk(base)) installed++; else failed++;
    } else {
        print_skip("NDK ja instalado.");
    }

    // 2. Command-line Tools (necessario pro sdkmanager)
    if (!status.cmdline_tools) {
        if (install_cmdline_tools(base)) installed++; else failed++;
    } else {
        print_skip("Command-line Tools ja instaladas.");
    }

    // Reler status apos instalar cmdline-tools
    status = check_components(base);

    // Aceitar licencas do SDK (necessario antes de instalar componentes)
    if (!status.build_tools || !status.platform || !status.platform_tools || !status.emulator) {
        accept_licenses(SETUP_BASE, java_home);
    }

    // 3. Build Tools
    if (!status.build_tools) {
        if (install_sdk_component(SETUP_BASE, java_home, "build-tools;30.0.3"))
            installed++; else failed++;
    } else {
        print_skip("Build Tools ja instaladas.");
    }

    // 4. Platform
    if (!status.platform) {
        if (install_sdk_component(SETUP_BASE, java_home, "platforms;android-30"))
            installed++; else failed++;
    } else {
        print_skip("Platform android-30 ja instalada.");
    }

    // 5. Platform Tools (adb)
    if (!status.platform_tools) {
        if (install_sdk_component(SETUP_BASE, java_home, "platform-tools"))
            installed++; else failed++;
    } else {
        print_skip("Platform Tools (adb) ja instaladas.");
    }

    // 6. Emulador
    if (!status.emulator) {
        if (install_emulator(SETUP_BASE, java_home)) installed++; else failed++;
    } else {
        print_skip("Emulador ja instalado.");
    }

    // Resultado final
    printf("\n");
    color_cyan();
    printf("  ============================================\n");
    printf("  Resultado\n");
    printf("  ============================================\n");
    color_reset();

    auto final_status = check_components(base);
    print_status(final_status);

    if (failed > 0) {
        color_yellow();
        printf("  %d componente(s) falharam. Tente novamente.\n", failed);
        color_reset();
    } else if (installed > 0) {
        color_green();
        printf("  %d componente(s) instalados com sucesso!\n", installed);
        color_reset();
    } else {
        color_green();
        printf("  Tudo ja esta instalado. Pronto pra usar!\n");
        color_reset();
    }

    printf("\n  Uso:\n");
    printf("    jp_android <arquivo.jp>       Compila e executa (console)\n");
    printf("    jp_android apk <arquivo.jp>   Gera APK com interface grafica\n");
    printf("\n");

    return (failed > 0) ? 1 : 0;
}