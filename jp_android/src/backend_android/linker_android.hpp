// linker_android.hpp
// Linkagem Android aarch64 — usa ld.lld do NDK para linkar contra bionic (libc do Android)

#ifndef JPLANG_LINKER_ANDROID_HPP
#define JPLANG_LINKER_ANDROID_HPP

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #define popen _popen
    #define pclose _pclose
#else
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace jplang {

// ============================================================================
// CONFIGURACAO DO NDK
// ============================================================================

struct NdkConfig {
    std::string ndk_root;       // ex: G:\distribuicao_JP_BR\jp_android\ndk\android-ndk-r27d
    std::string toolchain_bin;  // .../toolchains/llvm/prebuilt/windows-x86_64/bin
    std::string sysroot;        // .../toolchains/llvm/prebuilt/windows-x86_64/sysroot
    int api_level = 30;         // Android 11 (minimo recomendado)

    // Arquitetura alvo
    // "aarch64" para ARM64 (celulares)
    // "x86_64" para emulador x86_64
    std::string arch = "aarch64";

    bool valid() const {
        return !ndk_root.empty() && fs::exists(toolchain_bin) && fs::exists(sysroot);
    }
};

// Detecta o NDK a partir de um diretorio base
static NdkConfig detect_ndk(const std::string& base_dir) {
    NdkConfig cfg;

    // Procura o NDK em caminhos comuns relativos ao base_dir
    std::vector<std::string> candidates = {
        base_dir + "/ndk",
        base_dir + "/../ndk",
        base_dir + "/android/ndk",
        base_dir + "/ndk",
    };

    std::string ndk_root;
    for (auto& c : candidates) {
        if (!fs::exists(c)) continue;
        // Procura subpasta android-ndk-*
        for (auto& entry : fs::directory_iterator(c)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (name.find("android-ndk") != std::string::npos) {
                    ndk_root = entry.path().string();
                    break;
                }
            }
        }
        if (!ndk_root.empty()) break;
    }

    if (ndk_root.empty()) {
        std::cerr << "Erro: NDK nao encontrado. Verifique a pasta ndk/" << std::endl;
        return cfg;
    }

    cfg.ndk_root = ndk_root;

    // Detectar prebuilt (windows-x86_64 ou linux-x86_64)
    std::string prebuilt_base = ndk_root + "/toolchains/llvm/prebuilt";
    std::string prebuilt;

    if (fs::exists(prebuilt_base + "/windows-x86_64")) {
        prebuilt = prebuilt_base + "/windows-x86_64";
    } else if (fs::exists(prebuilt_base + "/linux-x86_64")) {
        prebuilt = prebuilt_base + "/linux-x86_64";
    } else {
        std::cerr << "Erro: Prebuilt toolchain nao encontrado em " << prebuilt_base << std::endl;
        return cfg;
    }

    cfg.toolchain_bin = prebuilt + "/bin";
    cfg.sysroot = prebuilt + "/sysroot";

    return cfg;
}

// ============================================================================
// LINKAGEM: .o -> executavel Android via ld.lld do NDK
// ============================================================================

static bool link_with_ld(const std::string& obj_path, const std::string& exe_path,
                          const NdkConfig& ndk,
                          const std::vector<std::string>& extra_objs = {},
                          const std::vector<std::string>& extra_libs = {},
                          const std::vector<std::string>& extra_lib_paths = {},
                          const std::vector<std::string>& extra_dlls = {},
                          bool is_static = true) {

    if (!ndk.valid()) {
        std::cerr << "Erro: Configuracao NDK invalida." << std::endl;
        return false;
    }

    // Caminhos do toolchain
    std::string ld_lld = ndk.toolchain_bin + "/ld.lld";
#ifdef _WIN32
    ld_lld += ".exe";
#endif

    if (!fs::exists(ld_lld)) {
        std::cerr << "Erro: ld.lld nao encontrado em " << ld_lld << std::endl;
        return false;
    }

    // Determinar triple e caminhos do sysroot
    std::string triple;
    if (ndk.arch == "aarch64") {
        triple = "aarch64-linux-android";
    } else if (ndk.arch == "x86_64") {
        triple = "x86_64-linux-android";
    } else {
        std::cerr << "Erro: Arquitetura nao suportada: " << ndk.arch << std::endl;
        return false;
    }

    std::string api_str = std::to_string(ndk.api_level);
    std::string lib_dir = ndk.sysroot + "/usr/lib/" + triple + "/" + api_str;
    std::string lib_dir_base = ndk.sysroot + "/usr/lib/" + triple;

    if (!fs::exists(lib_dir)) {
        std::cerr << "Erro: Sysroot libs nao encontradas em " << lib_dir << std::endl;
        return false;
    }

    // CRT objects do Android
    std::string crtbegin = lib_dir + "/crtbegin_static.o";
    std::string crtend   = lib_dir + "/crtend_android.o";

    if (!is_static) {
        crtbegin = lib_dir + "/crtbegin_dynamic.o";
    }

    // Montar comando
    std::string cmd = "\"" + ld_lld + "\"";

    // Target
    if (ndk.arch == "aarch64") {
        cmd += " -m aarch64linux";
    } else {
        cmd += " -m elf_x86_64";
    }

    // Linkagem estatica (sem dependencia de .so no Android)
    if (is_static) {
        cmd += " -static";
    }

    // Output
    cmd += " -o \"" + exe_path + "\"";

    // CRT startup
    cmd += " \"" + crtbegin + "\"";

    // Objeto principal
    cmd += " \"" + obj_path + "\"";

    // Objetos extras
    for (auto& obj : extra_objs) {
        if (fs::exists(obj)) {
            cmd += " \"" + obj + "\"";
        }
    }

    // Paths de busca de libs
    cmd += " -L \"" + lib_dir + "\"";
    cmd += " -L \"" + lib_dir_base + "\"";

    for (auto& lpath : extra_lib_paths) {
        cmd += " -L \"" + lpath + "\"";
    }

    // Libs extras
    for (auto& lib : extra_libs) {
        cmd += " -l" + lib;
    }

    // DLLs/.so extras
    for (auto& dll : extra_dlls) {
        if (fs::exists(dll)) {
            cmd += " \"" + dll + "\"";
        }
    }

    // Libs padrao do Android (bionic)
    cmd += " -lc";

    if (is_static) {
        // Link estatico precisa de libcompiler_rt ou libgcc
        // O NDK fornece libclang_rt.builtins
        std::string builtins_dir = ndk.toolchain_bin + "/../lib/clang";
        // Procurar versao do clang
        std::string builtins_lib;
        if (fs::exists(builtins_dir)) {
            for (auto& entry : fs::directory_iterator(builtins_dir)) {
                if (entry.is_directory()) {
                    std::string candidate;
                    if (ndk.arch == "aarch64") {
                        candidate = entry.path().string() + "/lib/linux/libclang_rt.builtins-aarch64-android.a";
                    } else {
                        candidate = entry.path().string() + "/lib/linux/libclang_rt.builtins-x86_64-android.a";
                    }
                    if (fs::exists(candidate)) {
                        builtins_lib = candidate;
                        break;
                    }
                }
            }
        }
        if (!builtins_lib.empty()) {
            cmd += " \"" + builtins_lib + "\"";
        }
    }

    // CRT finalization
    cmd += " \"" + crtend + "\"";

    // Entry point
    cmd += " -e main";

    // Executar
    int ret = std::system(("\"" + cmd + "\"").c_str());

    if (ret != 0) {
        std::cerr << "Erro: Linkagem Android falhou (codigo " << ret << ")" << std::endl;
        return false;
    }

    return true;
}

// ============================================================================
// CONFIGURACAO DO SDK (emulador + adb)
// ============================================================================

struct SdkConfig {
    std::string sdk_root;       // ex: G:\distribuicao_JP_BR\jp_android\sdk
    std::string adb_path;       // .../platform-tools/adb
    std::string emulator_path;  // .../emulator/emulator
    std::string avd_name;       // nome do AVD (default: teste_jp)

    bool valid() const {
        return !sdk_root.empty() && fs::exists(adb_path);
    }
};

static SdkConfig detect_sdk(const std::string& base_dir) {
    SdkConfig cfg;
    cfg.avd_name = "teste_jp";

    std::vector<std::string> candidates = {
        base_dir + "/sdk",
        base_dir + "/../sdk",
        base_dir + "/android/sdk",
        base_dir + "/sdk",
    };

    for (auto& c : candidates) {
        std::string adb = c + "/platform-tools/adb";
        std::string emu = c + "/emulator/emulator";
#ifdef _WIN32
        adb += ".exe";
        emu += ".exe";
#endif
        if (fs::exists(adb)) {
            cfg.sdk_root = c;
            cfg.adb_path = adb;
            cfg.emulator_path = emu;
            break;
        }
    }

    return cfg;
}

// Configura PATH do sistema pra incluir adb e emulator
static void setup_sdk_path(const SdkConfig& sdk) {
    if (!sdk.valid()) return;
    std::string platform_tools = sdk.sdk_root + "/platform-tools";
    std::string emulator_dir = sdk.sdk_root + "/emulator";

#ifdef _WIN32
    char* old_path = getenv("PATH");
    std::string new_path = platform_tools + ";" + emulator_dir;
    if (old_path) new_path += ";" + std::string(old_path);
    SetEnvironmentVariableA("PATH", new_path.c_str());
#else
    std::string old_path = getenv("PATH") ? getenv("PATH") : "";
    std::string new_path = platform_tools + ":" + emulator_dir + ":" + old_path;
    setenv("PATH", new_path.c_str(), 1);
#endif
}

// ============================================================================
// GERENCIAMENTO DE DISPOSITIVOS
// ============================================================================

struct DeviceInfo {
    std::string serial;
    bool is_emulator;
};

// Lista dispositivos conectados via adb
static std::vector<DeviceInfo> list_devices() {
    std::vector<DeviceInfo> devices;
    FILE* pipe = popen("adb devices 2>&1", "r");
    if (!pipe) return devices;

    std::string output;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        // Formato: "SERIAL\tdevice" ou "emulator-5554\tdevice"
        if (line.find("\tdevice") != std::string::npos) {
            std::string serial = line.substr(0, line.find('\t'));
            DeviceInfo dev;
            dev.serial = serial;
            dev.is_emulator = (serial.find("emulator") != std::string::npos);
            devices.push_back(dev);
        }
    }
    pclose(pipe);
    return devices;
}

// Escolhe o melhor dispositivo: prioriza celular real sobre emulador
static std::string choose_device() {
    auto devices = list_devices();
    if (devices.empty()) return "";

    // Prioriza celular real
    for (auto& d : devices) {
        if (!d.is_emulator) return d.serial;
    }
    // Senao, usa emulador
    return devices[0].serial;
}

// Inicia o emulador em background e espera ficar pronto
static bool start_emulator(const SdkConfig& sdk) {
    if (!fs::exists(sdk.emulator_path)) {
        std::cerr << "Erro: Emulador nao encontrado em " << sdk.emulator_path << std::endl;
        return false;
    }

    std::cout << "[JPLang Android] Iniciando emulador '" << sdk.avd_name << "'..." << std::endl;

    std::string cmd = "\"" + sdk.emulator_path + "\" -avd " + sdk.avd_name + " -no-metrics -no-snapshot-save";

#ifdef _WIN32
    cmd = "start /B \"\" " + cmd + " >nul 2>&1";
#else
    cmd = cmd + " >/dev/null 2>&1 &";
#endif
    std::system(cmd.c_str());

    std::cout << "[JPLang Android] Aguardando emulador iniciar";
    std::cout.flush();

#ifdef _WIN32
    std::system("adb wait-for-device >nul 2>&1");
#else
    std::system("adb wait-for-device >/dev/null 2>&1");
#endif

    for (int i = 0; i < 120; i++) {
        FILE* pipe = popen("adb shell getprop sys.boot_completed 2>&1", "r");
        if (pipe) {
            char buf[64] = {0};
            fgets(buf, sizeof(buf), pipe);
            pclose(pipe);
            std::string result(buf);
            if (result.find("1") != std::string::npos) {
                std::cout << " pronto!" << std::endl;
                return true;
            }
        }
        std::cout << ".";
        std::cout.flush();
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    std::cerr << "\nErro: Timeout esperando emulador iniciar." << std::endl;
    return false;
}

// Detecta a arquitetura do dispositivo via adb
static std::string detect_device_arch(const std::string& serial) {
    std::string cmd = "adb -s " + serial + " shell getprop ro.product.cpu.abi 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buf[128] = {0};
    fgets(buf, sizeof(buf), pipe);
    pclose(pipe);

    std::string abi(buf);
    // Limpar whitespace
    while (!abi.empty() && (abi.back() == '\n' || abi.back() == '\r' || abi.back() == ' '))
        abi.pop_back();

    // Mapear ABI pra arch do NDK
    if (abi == "arm64-v8a") return "aarch64";
    if (abi == "x86_64")    return "x86_64";
    if (abi == "armeabi-v7a") return "armv7a";
    if (abi == "x86")       return "i686";
    return abi;
}

// Garante que tem um dispositivo disponivel
// Retorna o serial do dispositivo escolhido, ou "" se falhou
static std::string ensure_device(const SdkConfig& sdk) {
    // Primeiro: ver se ja tem algum dispositivo conectado
    std::string serial = choose_device();
    if (!serial.empty()) {
        auto devices = list_devices();
        for (auto& d : devices) {
            if (d.serial == serial) {
                if (d.is_emulator)
                    std::cout << "[JPLang Android] Usando emulador: " << serial << std::endl;
                else
                    std::cout << "[JPLang Android] Usando dispositivo: " << serial << std::endl;
                break;
            }
        }
        return serial;
    }

    // Nenhum dispositivo: tentar iniciar emulador
    if (!start_emulator(sdk)) return "";

    serial = choose_device();
    return serial;
}

// ============================================================================
// PUSH E EXECUCAO via adb (com serial especifico)
// ============================================================================

static bool push_and_run(const std::string& exe_path, const std::string& serial,
                          const std::string& remote_name = "") {
    std::string name = remote_name;
    if (name.empty()) {
        name = fs::path(exe_path).filename().string();
    }

    std::string remote_path = "/data/local/tmp/" + name;
    std::string adb = "adb -s " + serial;

    // Push
    std::string push_cmd = adb + " push \"" + exe_path + "\" " + remote_path;
    int ret = std::system(push_cmd.c_str());
    if (ret != 0) {
        std::cerr << "Erro: adb push falhou." << std::endl;
        return false;
    }

    // Chmod
    std::string chmod_cmd = adb + " shell chmod +x " + remote_path;
    std::system(chmod_cmd.c_str());

    // Executar (com locale UTF-8)
    std::string run_cmd = adb + " shell \"export LANG=en_US.UTF-8; " + remote_path + "\"";
    ret = std::system(run_cmd.c_str());

    return (ret == 0);
}

} // namespace jplang

#endif // JPLANG_LINKER_ANDROID_HPP