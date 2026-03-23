// platform_defs_android.hpp
// Definicoes de plataforma Android aarch64 — registradores ARM64, convencao AAPCS64, simbolos bionic

#ifndef JPLANG_PLATFORM_DEFS_ANDROID_HPP
#define JPLANG_PLATFORM_DEFS_ANDROID_HPP

#include "arm64_emitter.hpp"

namespace jplang {

// ============================================================================
// REGISTRADORES ARM64 (AArch64)
// ============================================================================
//
// X0-X30: registradores de proposito geral 64-bit
// W0-W30: mesmos registradores, acesso 32-bit (metade inferior)
// SP:     stack pointer (nao eh X31, eh separado)
// XZR:    zero register (codificado como 31 em algumas instrucoes)
// LR:     link register (X30) — endereco de retorno
// FP:     frame pointer (X29) — equivalente ao RBP do x86
//
// D0-D31 / V0-V31: registradores SIMD/float 64-bit / 128-bit
//
// ============================================================================

namespace reg {
    constexpr uint8_t X0  = 0;
    constexpr uint8_t X1  = 1;
    constexpr uint8_t X2  = 2;
    constexpr uint8_t X3  = 3;
    constexpr uint8_t X4  = 4;
    constexpr uint8_t X5  = 5;
    constexpr uint8_t X6  = 6;
    constexpr uint8_t X7  = 7;
    constexpr uint8_t X8  = 8;   // indirect result (tambem scratch)
    constexpr uint8_t X9  = 9;   // scratch (caller-saved)
    constexpr uint8_t X10 = 10;
    constexpr uint8_t X11 = 11;
    constexpr uint8_t X12 = 12;
    constexpr uint8_t X13 = 13;
    constexpr uint8_t X14 = 14;
    constexpr uint8_t X15 = 15;
    constexpr uint8_t X16 = 16;  // IP0 — intra-procedure scratch
    constexpr uint8_t X17 = 17;  // IP1 — intra-procedure scratch
    constexpr uint8_t X18 = 18;  // platform register (reservado no Android)
    constexpr uint8_t X19 = 19;  // callee-saved
    constexpr uint8_t X20 = 20;
    constexpr uint8_t X21 = 21;
    constexpr uint8_t X22 = 22;
    constexpr uint8_t X23 = 23;
    constexpr uint8_t X24 = 24;
    constexpr uint8_t X25 = 25;
    constexpr uint8_t X26 = 26;
    constexpr uint8_t X27 = 27;
    constexpr uint8_t X28 = 28;
    constexpr uint8_t FP  = 29;  // frame pointer (X29)
    constexpr uint8_t LR  = 30;  // link register (X30)
    constexpr uint8_t SP  = 31;  // stack pointer (contexto SP)
    constexpr uint8_t XZR = 31;  // zero register (contexto de dados)
}

namespace dreg {
    constexpr uint8_t D0  = 0;   // float arg1 / retorno float
    constexpr uint8_t D1  = 1;
    constexpr uint8_t D2  = 2;
    constexpr uint8_t D3  = 3;
    constexpr uint8_t D4  = 4;
    constexpr uint8_t D5  = 5;
    constexpr uint8_t D6  = 6;
    constexpr uint8_t D7  = 7;
    constexpr uint8_t D8  = 8;   // callee-saved (D8-D15)
    constexpr uint8_t D9  = 9;
    constexpr uint8_t D10 = 10;
    constexpr uint8_t D11 = 11;
    constexpr uint8_t D12 = 12;
    constexpr uint8_t D13 = 13;
    constexpr uint8_t D14 = 14;
    constexpr uint8_t D15 = 15;
}

// ============================================================================
// DEFINICOES DA PLATAFORMA ANDROID AARCH64
// ============================================================================

struct PlatformDefs {

    // --- Identificacao ---
    static constexpr bool is_windows = false;
    static constexpr bool is_linux   = false;
    static constexpr bool is_android = true;

    // --- Tipo do emitter ---
    using Emitter = Arm64Emitter;

    // --- Calling Convention (AAPCS64) ---
    //
    // Parametros inteiros: X0, X1, X2, X3, X4, X5, X6, X7 (8 regs!)
    // Parametros float:    D0, D1, D2, D3, D4, D5, D6, D7 (separados dos int)
    // Retorno:             X0 (int/ptr), D0 (float/double)
    // Frame pointer:       X29 (FP)
    // Link register:       X30 (LR) — salvo pelo callee
    // Stack:               16-byte aligned sempre
    // Sem shadow space (diferente do Windows x64)
    //
    // Variadica (printf): floats vao nos D regs normalmente, nao precisa mirror
    // Nao precisa setar nenhum contador de args float (sem AL como no SysV x86-64)
    //
    static constexpr uint8_t ARG1 = reg::X0;
    static constexpr uint8_t ARG2 = reg::X1;
    static constexpr uint8_t ARG3 = reg::X2;
    static constexpr uint8_t ARG4 = reg::X3;
    static constexpr uint8_t ARG5 = reg::X4;
    static constexpr uint8_t ARG6 = reg::X5;
    static constexpr uint8_t ARG7 = reg::X6;
    static constexpr uint8_t ARG8 = reg::X7;

    static constexpr uint8_t FLOAT_ARG1 = dreg::D0;
    static constexpr uint8_t FLOAT_ARG2 = dreg::D1;
    static constexpr uint8_t FLOAT_ARG3 = dreg::D2;
    static constexpr uint8_t FLOAT_ARG4 = dreg::D3;
    static constexpr uint8_t FLOAT_ARG5 = dreg::D4;
    static constexpr uint8_t FLOAT_ARG6 = dreg::D5;
    static constexpr uint8_t FLOAT_ARG7 = dreg::D6;
    static constexpr uint8_t FLOAT_ARG8 = dreg::D7;

    // Registradores scratch (caller-saved, livres pra usar sem salvar)
    static constexpr uint8_t SCRATCH1 = reg::X9;
    static constexpr uint8_t SCRATCH2 = reg::X10;
    static constexpr uint8_t SCRATCH3 = reg::X11;

    // Stack minima no prologo (sem shadow space no ARM64, mas 16-byte aligned)
    static constexpr int32_t MIN_STACK = 16;

    // --- Variadica (printf) ---
    static constexpr bool VARIADIC_NEEDS_AL         = false;
    static constexpr bool VARIADIC_FLOAT_MIRROR_GPR = false;

    // --- Tipos de relocation ARM64 ---
    static constexpr uint32_t REL_CALL    = R_AARCH64_CALL26;
    static constexpr uint32_t REL_JUMP    = R_AARCH64_JUMP26;
    static constexpr uint32_t REL_ADRP    = R_AARCH64_ADR_PREL_PG_HI21;
    static constexpr uint32_t REL_ADD_LO  = R_AARCH64_ADD_ABS_LO12_NC;
    static constexpr uint32_t REL_LDR64   = R_AARCH64_LDST64_ABS_LO12_NC;
    static constexpr uint32_t REL_ABS64   = R_AARCH64_ABS64;

    // --- Relocation: ELF usa addend (RELA) ---
    static constexpr bool HAS_RELOC_ADDEND = true;
    static constexpr int64_t DEFAULT_CALL_ADDEND = 0;
    static constexpr int64_t DEFAULT_RIP_ADDEND  = 0;

    // --- Simbolos externos comuns (bionic libc do Android) ---
    static void add_common_externs(Emitter& emitter) {
        emitter.add_extern_symbol("printf");
        emitter.add_extern_symbol("sprintf");
        emitter.add_extern_symbol("snprintf");
        emitter.add_extern_symbol("exit");
        emitter.add_extern_symbol("putchar");
        emitter.add_extern_symbol("malloc");
        emitter.add_extern_symbol("realloc");
        emitter.add_extern_symbol("free");
        emitter.add_extern_symbol("memmove");
        emitter.add_extern_symbol("memcpy");
        emitter.add_extern_symbol("memset");
        emitter.add_extern_symbol("fgets");
        emitter.add_extern_symbol("strlen");
        emitter.add_extern_symbol("strcmp");
        emitter.add_extern_symbol("atoi");
        emitter.add_extern_symbol("atof");
        emitter.add_extern_symbol("stdin");
    }

    // --- Simbolos externos especificos do Android ---
    static void add_platform_externs(Emitter& emitter) {
        // Android/bionic nao precisa de __main, SetConsoleOutputCP, etc.
        // dlopen/dlsym para bibliotecas nativas .so
        emitter.add_extern_symbol("dlopen");
        emitter.add_extern_symbol("dlsym");
        emitter.add_extern_symbol("dlclose");
        emitter.add_extern_symbol("dlerror");
    }

    // --- Secoes extras ---
    static void create_extra_sections(Emitter& /*emitter*/) {
        // Android/ELF nao precisa de secoes extras por enquanto
        // (poderia adicionar .note.android.ident no futuro)
    }
};

} // namespace jplang

#endif // JPLANG_PLATFORM_DEFS_ANDROID_HPP
