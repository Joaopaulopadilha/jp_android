// arm64_emitter.hpp
// Emissor de arquivos objeto ELF aarch64 para Android — gera .o compativel com ld.lld do NDK

#ifndef JPLANG_ARM64_EMITTER_HPP
#define JPLANG_ARM64_EMITTER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <stdexcept>

namespace jplang {

// ============================================================================
// CONSTANTES ELF
// ============================================================================

// ELF identification
constexpr uint8_t ELFMAG0 = 0x7F;
constexpr uint8_t ELFMAG1 = 'E';
constexpr uint8_t ELFMAG2 = 'L';
constexpr uint8_t ELFMAG3 = 'F';
constexpr uint8_t ELFCLASS64   = 2;
constexpr uint8_t ELFDATA2LSB  = 1;  // little-endian
constexpr uint8_t EV_CURRENT   = 1;
constexpr uint8_t ELFOSABI_NONE = 0;

// ELF types
constexpr uint16_t ET_REL = 1;  // relocatable

// Machine
constexpr uint16_t EM_AARCH64 = 183;

// Section types
constexpr uint32_t SHT_NULL     = 0;
constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB   = 2;
constexpr uint32_t SHT_STRTAB   = 3;
constexpr uint32_t SHT_RELA     = 4;
constexpr uint32_t SHT_NOBITS   = 8;

// Section flags
constexpr uint64_t SHF_WRITE     = 0x1;
constexpr uint64_t SHF_ALLOC     = 0x2;
constexpr uint64_t SHF_EXECINSTR = 0x4;
constexpr uint64_t SHF_INFO_LINK = 0x40;

// Symbol binding
constexpr uint8_t STB_LOCAL  = 0;
constexpr uint8_t STB_GLOBAL = 1;

// Symbol type
constexpr uint8_t STT_NOTYPE  = 0;
constexpr uint8_t STT_FUNC    = 2;
constexpr uint8_t STT_SECTION = 3;

// Special section indices
constexpr uint16_t SHN_UNDEF = 0;

// AArch64 relocation types
constexpr uint32_t R_AARCH64_CALL26         = 283;  // BL imm26
constexpr uint32_t R_AARCH64_JUMP26         = 282;  // B imm26
constexpr uint32_t R_AARCH64_ADR_PREL_PG_HI21 = 275;  // ADRP imm21
constexpr uint32_t R_AARCH64_ADD_ABS_LO12_NC   = 277;  // ADD imm12
constexpr uint32_t R_AARCH64_LDST64_ABS_LO12_NC = 286; // LDR/STR 64-bit imm12
constexpr uint32_t R_AARCH64_ABS64          = 257;  // 64-bit absolute

// Helper: ELF64_ST_INFO(bind, type)
constexpr uint8_t ELF64_ST_INFO(uint8_t bind, uint8_t type) {
    return (bind << 4) | (type & 0xF);
}

// Flag interna: IDs com esse bit sao referencias a secoes
constexpr uint32_t SECTION_SYM_FLAG = 0x80000000;

// ============================================================================
// ESTRUTURAS ELF64 (layout binario exato)
// ============================================================================

#pragma pack(push, 1)

struct Elf64_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

#pragma pack(pop)

// Helper: ELF64_R_INFO(sym, type)
static inline uint64_t ELF64_R_INFO(uint32_t sym, uint32_t type) {
    return ((uint64_t)sym << 32) | type;
}

// ============================================================================
// SECAO (mesma interface do CoffEmitter)
// ============================================================================

struct Arm64Section {
    std::string name;
    uint32_t    type;
    uint64_t    flags;
    uint64_t    alignment;
    std::vector<uint8_t> data;

    struct Relocation {
        uint32_t offset;
        uint32_t symbol_index;
        uint32_t type;
        int64_t  addend;
    };
    std::vector<Relocation> relocations;

    size_t size() const { return data.size(); }

    void emit(uint8_t byte) { data.push_back(byte); }
    void emit(const uint8_t* bytes, size_t count) {
        data.insert(data.end(), bytes, bytes + count);
    }

    void emit_u8(uint8_t v)   { emit(v); }
    void emit_u16(uint16_t v) { emit(reinterpret_cast<uint8_t*>(&v), 2); }
    void emit_u32(uint32_t v) { emit(reinterpret_cast<uint8_t*>(&v), 4); }
    void emit_u64(uint64_t v) { emit(reinterpret_cast<uint8_t*>(&v), 8); }
    void emit_i8(int8_t v)    { emit(static_cast<uint8_t>(v)); }
    void emit_i32(int32_t v)  { emit(reinterpret_cast<uint8_t*>(&v), 4); }

    // Emite uma instrucao ARM64 (sempre 4 bytes, little-endian)
    void emit_instr(uint32_t instr) { emit_u32(instr); }

    void emit_string(const std::string& s) {
        emit(reinterpret_cast<const uint8_t*>(s.c_str()), s.size() + 1);
    }

    void emit_zeros(size_t count) {
        data.insert(data.end(), count, 0);
    }

    void align(size_t alignment) {
        size_t remainder = data.size() % alignment;
        if (remainder != 0) emit_zeros(alignment - remainder);
    }

    void patch_u32(size_t offset, uint32_t v) {
        if (offset + 4 > data.size())
            throw std::runtime_error("patch_u32: offset fora dos limites");
        std::memcpy(&data[offset], &v, 4);
    }

    void patch_i32(size_t offset, int32_t v) {
        if (offset + 4 > data.size())
            throw std::runtime_error("patch_i32: offset fora dos limites");
        std::memcpy(&data[offset], &v, 4);
    }

    size_t pos() const { return data.size(); }

    void add_relocation(uint32_t offset, uint32_t symbol_index, uint32_t rel_type, int64_t addend = 0) {
        relocations.push_back({offset, symbol_index, rel_type, addend});
    }
};

// ============================================================================
// SIMBOLO (dados internos)
// ============================================================================

struct Arm64SymbolInfo {
    std::string name;
    uint32_t    value;
    int16_t     section_number;  // 0 = UNDEF, >0 = section index (1-based)
    uint8_t     bind;            // STB_LOCAL, STB_GLOBAL
    uint8_t     type;            // STT_NOTYPE, STT_FUNC, STT_SECTION
};

// ============================================================================
// ARM64 ELF EMITTER
// ============================================================================

class Arm64Emitter {
public:
    Arm64Emitter() = default;

    // ------------------------------------------------------------------
    // Secoes
    // ------------------------------------------------------------------

    Arm64Section& create_text_section() {
        return create_section(".text", SHT_PROGBITS,
            SHF_ALLOC | SHF_EXECINSTR, 4);
    }

    Arm64Section& create_data_section() {
        return create_section(".data", SHT_PROGBITS,
            SHF_ALLOC | SHF_WRITE, 8);
    }

    Arm64Section& create_rdata_section() {
        return create_section(".rodata", SHT_PROGBITS,
            SHF_ALLOC, 8);
    }

    Arm64Section& create_section(const std::string& name, uint32_t type,
                                  uint64_t flags, uint64_t alignment) {
        Arm64Section sec;
        sec.name = name;
        sec.type = type;
        sec.flags = flags;
        sec.alignment = alignment;
        sections_.push_back(std::move(sec));
        return sections_.back();
    }

    Arm64Section& section(size_t index) { return sections_[index]; }
    size_t section_count() const { return sections_.size(); }

    // ------------------------------------------------------------------
    // Simbolos
    // ------------------------------------------------------------------

    uint32_t add_global_symbol(const std::string& name, size_t section_index,
                               uint32_t offset, bool is_function = false) {
        Arm64SymbolInfo sym;
        sym.name = name;
        sym.value = offset;
        sym.section_number = static_cast<int16_t>(section_index + 1);
        sym.bind = STB_GLOBAL;
        sym.type = is_function ? STT_FUNC : STT_NOTYPE;
        return register_symbol(sym);
    }

    uint32_t add_extern_symbol(const std::string& name) {
        auto it = symbol_index_map_.find(name);
        if (it != symbol_index_map_.end()) return it->second;

        Arm64SymbolInfo sym;
        sym.name = name;
        sym.value = 0;
        sym.section_number = 0;  // SHN_UNDEF
        sym.bind = STB_GLOBAL;
        sym.type = STT_FUNC;
        return register_symbol(sym);
    }

    uint32_t add_static_symbol(const std::string& name, size_t section_index,
                                uint32_t offset) {
        Arm64SymbolInfo sym;
        sym.name = name;
        sym.value = offset;
        sym.section_number = static_cast<int16_t>(section_index + 1);
        sym.bind = STB_LOCAL;
        sym.type = STT_NOTYPE;
        return register_symbol(sym);
    }

    uint32_t symbol_index(const std::string& name) const {
        auto it = symbol_index_map_.find(name);
        if (it == symbol_index_map_.end())
            throw std::runtime_error("Simbolo nao encontrado: " + name);
        return it->second;
    }

    bool has_symbol(const std::string& name) const {
        return symbol_index_map_.count(name) > 0;
    }

    // ------------------------------------------------------------------
    // Serializacao ELF aarch64
    // ------------------------------------------------------------------

    bool write(const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // ============================================================
        // Construir tabelas auxiliares
        // ============================================================

        // String table (.strtab) — nomes dos simbolos
        std::vector<uint8_t> strtab;
        strtab.push_back(0);  // indice 0 = string vazia

        auto add_str = [&](const std::string& s) -> uint32_t {
            uint32_t off = static_cast<uint32_t>(strtab.size());
            strtab.insert(strtab.end(), s.begin(), s.end());
            strtab.push_back(0);
            return off;
        };

        // Section header string table (.shstrtab) — nomes das secoes
        std::vector<uint8_t> shstrtab;
        shstrtab.push_back(0);

        auto add_shstr = [&](const std::string& s) -> uint32_t {
            uint32_t off = static_cast<uint32_t>(shstrtab.size());
            shstrtab.insert(shstrtab.end(), s.begin(), s.end());
            shstrtab.push_back(0);
            return off;
        };

        // ============================================================
        // Planejar layout das secoes no ELF:
        //
        // Section headers (no final do arquivo):
        //   [0] NULL
        //   [1..N] secoes de usuario (.text, .rodata, .data)
        //   [N+1..N+K] secoes .rela.X (uma por secao com relocations)
        //   [N+K+1] .symtab
        //   [N+K+2] .strtab
        //   [N+K+3] .shstrtab
        // ============================================================

        size_t num_user_sec = sections_.size();

        // Contar secoes .rela
        std::vector<size_t> rela_for_sec;  // indices das secoes que tem relocs
        for (size_t i = 0; i < num_user_sec; i++) {
            if (!sections_[i].relocations.empty()) {
                rela_for_sec.push_back(i);
            }
        }
        size_t num_rela_sec = rela_for_sec.size();

        // Total de section headers:
        // 1 (NULL) + num_user_sec + num_rela_sec + 1 (symtab) + 1 (strtab) + 1 (shstrtab)
        size_t total_shdrs = 1 + num_user_sec + num_rela_sec + 3;
        size_t idx_symtab  = 1 + num_user_sec + num_rela_sec;
        size_t idx_strtab  = idx_symtab + 1;
        size_t idx_shstrtab = idx_strtab + 1;

        // ============================================================
        // Construir symbol table (.symtab)
        // ============================================================

        std::vector<Elf64_Sym> symtab;

        // [0] NULL symbol
        {
            Elf64_Sym null_sym;
            std::memset(&null_sym, 0, sizeof(null_sym));
            symtab.push_back(null_sym);
        }

        // Section symbols (LOCAL) — um por secao de usuario
        for (size_t i = 0; i < num_user_sec; i++) {
            Elf64_Sym ssym;
            std::memset(&ssym, 0, sizeof(ssym));
            ssym.st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
            ssym.st_shndx = static_cast<uint16_t>(i + 1);
            symtab.push_back(ssym);
        }

        uint32_t first_global = static_cast<uint32_t>(symtab.size());

        // Mapear user symbols pra indices finais no symtab
        // Primeiro: locals, depois globals (ELF exige locals antes de globals)
        std::vector<size_t> local_syms, global_syms;
        for (size_t i = 0; i < symbol_order_.size(); i++) {
            auto& info = symbols_[symbol_order_[i]];
            if (info.bind == STB_LOCAL) local_syms.push_back(i);
            else global_syms.push_back(i);
        }

        std::unordered_map<uint32_t, uint32_t> sym_remap;

        for (auto idx : local_syms) {
            uint32_t final_idx = static_cast<uint32_t>(symtab.size());
            sym_remap[static_cast<uint32_t>(idx)] = final_idx;

            auto& info = symbols_[symbol_order_[idx]];
            Elf64_Sym esym;
            std::memset(&esym, 0, sizeof(esym));
            esym.st_name = add_str(info.name);
            esym.st_info = ELF64_ST_INFO(info.bind, info.type);
            esym.st_shndx = (info.section_number > 0) ?
                static_cast<uint16_t>(info.section_number) : SHN_UNDEF;
            esym.st_value = info.value;
            symtab.push_back(esym);
        }

        first_global = static_cast<uint32_t>(symtab.size());

        for (auto idx : global_syms) {
            uint32_t final_idx = static_cast<uint32_t>(symtab.size());
            sym_remap[static_cast<uint32_t>(idx)] = final_idx;

            auto& info = symbols_[symbol_order_[idx]];
            Elf64_Sym esym;
            std::memset(&esym, 0, sizeof(esym));
            esym.st_name = add_str(info.name);
            esym.st_info = ELF64_ST_INFO(info.bind, info.type);
            esym.st_shndx = (info.section_number > 0) ?
                static_cast<uint16_t>(info.section_number) : SHN_UNDEF;
            esym.st_value = info.value;
            symtab.push_back(esym);
        }

        // ============================================================
        // Construir secoes .rela
        // ============================================================

        struct RelaSection {
            size_t target_sec_idx;  // indice da secao alvo (1-based no ELF)
            std::vector<Elf64_Rela> entries;
        };
        std::vector<RelaSection> rela_sections;

        for (auto sec_idx : rela_for_sec) {
            RelaSection rs;
            rs.target_sec_idx = sec_idx;

            for (auto& reloc : sections_[sec_idx].relocations) {
                Elf64_Rela rela;
                rela.r_offset = reloc.offset;

                // Remapear symbol index
                uint32_t final_sym;
                if (reloc.symbol_index & SECTION_SYM_FLAG) {
                    // Referencia a secao: indice no symtab eh 1 + sec_idx
                    uint32_t s = reloc.symbol_index & ~SECTION_SYM_FLAG;
                    final_sym = static_cast<uint32_t>(1 + s);  // section symbols comecam em 1
                } else {
                    auto it = sym_remap.find(reloc.symbol_index);
                    if (it != sym_remap.end()) final_sym = it->second;
                    else final_sym = 0;
                }

                rela.r_info = ELF64_R_INFO(final_sym, reloc.type);
                rela.r_addend = reloc.addend;
                rs.entries.push_back(rela);
            }
            rela_sections.push_back(std::move(rs));
        }

        // ============================================================
        // Registrar nomes de secoes no shstrtab
        // ============================================================

        std::vector<uint32_t> shdr_names;

        // [0] NULL — nome vazio
        shdr_names.push_back(add_shstr(""));

        // Secoes de usuario
        for (size_t i = 0; i < num_user_sec; i++) {
            shdr_names.push_back(add_shstr(sections_[i].name));
        }

        // Secoes .rela
        for (auto& rs : rela_sections) {
            std::string rname = ".rela" + sections_[rs.target_sec_idx].name;
            shdr_names.push_back(add_shstr(rname));
        }

        // .symtab, .strtab, .shstrtab
        uint32_t name_symtab   = add_shstr(".symtab");
        uint32_t name_strtab   = add_shstr(".strtab");
        uint32_t name_shstrtab = add_shstr(".shstrtab");
        shdr_names.push_back(name_symtab);
        shdr_names.push_back(name_strtab);
        shdr_names.push_back(name_shstrtab);

        // ============================================================
        // Calcular offsets no arquivo
        // ============================================================

        uint64_t offset = sizeof(Elf64_Ehdr);

        // Dados das secoes de usuario
        struct SecLayout { uint64_t offset; };
        std::vector<SecLayout> user_layouts(num_user_sec);
        for (size_t i = 0; i < num_user_sec; i++) {
            // Alinhar
            uint64_t al = sections_[i].alignment;
            if (al > 1) {
                uint64_t rem = offset % al;
                if (rem) offset += al - rem;
            }
            user_layouts[i].offset = offset;
            offset += sections_[i].size();
        }

        // Dados das secoes .rela
        std::vector<uint64_t> rela_offsets(num_rela_sec);
        for (size_t i = 0; i < num_rela_sec; i++) {
            uint64_t rem = offset % 8;
            if (rem) offset += 8 - rem;
            rela_offsets[i] = offset;
            offset += rela_sections[i].entries.size() * sizeof(Elf64_Rela);
        }

        // .symtab
        {
            uint64_t rem = offset % 8;
            if (rem) offset += 8 - rem;
        }
        uint64_t symtab_offset = offset;
        offset += symtab.size() * sizeof(Elf64_Sym);

        // .strtab
        uint64_t strtab_offset = offset;
        offset += strtab.size();

        // .shstrtab
        uint64_t shstrtab_offset = offset;
        offset += shstrtab.size();

        // Section header table (alinhado a 8)
        {
            uint64_t rem = offset % 8;
            if (rem) offset += 8 - rem;
        }
        uint64_t shdr_table_offset = offset;

        // ============================================================
        // Escrever ELF header
        // ============================================================

        Elf64_Ehdr ehdr;
        std::memset(&ehdr, 0, sizeof(ehdr));
        ehdr.e_ident[0] = ELFMAG0;
        ehdr.e_ident[1] = ELFMAG1;
        ehdr.e_ident[2] = ELFMAG2;
        ehdr.e_ident[3] = ELFMAG3;
        ehdr.e_ident[4] = ELFCLASS64;
        ehdr.e_ident[5] = ELFDATA2LSB;
        ehdr.e_ident[6] = EV_CURRENT;
        ehdr.e_ident[7] = ELFOSABI_NONE;
        ehdr.e_type    = ET_REL;
        ehdr.e_machine = EM_AARCH64;
        ehdr.e_version = EV_CURRENT;
        ehdr.e_ehsize  = sizeof(Elf64_Ehdr);
        ehdr.e_shoff   = shdr_table_offset;
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum   = static_cast<uint16_t>(total_shdrs);
        ehdr.e_shstrndx = static_cast<uint16_t>(idx_shstrtab);
        file.write(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));

        // ============================================================
        // Escrever dados das secoes
        // ============================================================

        // Funcao auxiliar pra preencher com zeros ate o offset desejado
        auto pad_to = [&](uint64_t target) {
            uint64_t cur = static_cast<uint64_t>(file.tellp());
            if (cur < target) {
                std::vector<uint8_t> zeros(target - cur, 0);
                file.write(reinterpret_cast<const char*>(zeros.data()), zeros.size());
            }
        };

        // Secoes de usuario
        for (size_t i = 0; i < num_user_sec; i++) {
            pad_to(user_layouts[i].offset);
            if (!sections_[i].data.empty()) {
                file.write(reinterpret_cast<const char*>(sections_[i].data.data()),
                           sections_[i].data.size());
            }
        }

        // Secoes .rela
        for (size_t i = 0; i < num_rela_sec; i++) {
            pad_to(rela_offsets[i]);
            for (auto& rela : rela_sections[i].entries) {
                file.write(reinterpret_cast<const char*>(&rela), sizeof(Elf64_Rela));
            }
        }

        // .symtab
        pad_to(symtab_offset);
        for (auto& sym : symtab) {
            file.write(reinterpret_cast<const char*>(&sym), sizeof(Elf64_Sym));
        }

        // .strtab
        pad_to(strtab_offset);
        file.write(reinterpret_cast<const char*>(strtab.data()), strtab.size());

        // .shstrtab
        pad_to(shstrtab_offset);
        file.write(reinterpret_cast<const char*>(shstrtab.data()), shstrtab.size());

        // ============================================================
        // Escrever section header table
        // ============================================================

        pad_to(shdr_table_offset);
        size_t shdr_name_idx = 0;

        // [0] NULL
        {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name = shdr_names[shdr_name_idx++];
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        // Secoes de usuario
        for (size_t i = 0; i < num_user_sec; i++) {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name      = shdr_names[shdr_name_idx++];
            shdr.sh_type      = sections_[i].type;
            shdr.sh_flags     = sections_[i].flags;
            shdr.sh_offset    = user_layouts[i].offset;
            shdr.sh_size      = sections_[i].size();
            shdr.sh_addralign = sections_[i].alignment;
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        // Secoes .rela
        for (size_t i = 0; i < num_rela_sec; i++) {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name      = shdr_names[shdr_name_idx++];
            shdr.sh_type      = SHT_RELA;
            shdr.sh_flags     = SHF_INFO_LINK;
            shdr.sh_offset    = rela_offsets[i];
            shdr.sh_size      = rela_sections[i].entries.size() * sizeof(Elf64_Rela);
            shdr.sh_link      = static_cast<uint32_t>(idx_symtab);
            shdr.sh_info      = static_cast<uint32_t>(rela_sections[i].target_sec_idx + 1);
            shdr.sh_addralign = 8;
            shdr.sh_entsize   = sizeof(Elf64_Rela);
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        // .symtab
        {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name      = shdr_names[shdr_name_idx++];
            shdr.sh_type      = SHT_SYMTAB;
            shdr.sh_offset    = symtab_offset;
            shdr.sh_size      = symtab.size() * sizeof(Elf64_Sym);
            shdr.sh_link      = static_cast<uint32_t>(idx_strtab);
            shdr.sh_info      = first_global;
            shdr.sh_addralign = 8;
            shdr.sh_entsize   = sizeof(Elf64_Sym);
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        // .strtab
        {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name      = shdr_names[shdr_name_idx++];
            shdr.sh_type      = SHT_STRTAB;
            shdr.sh_offset    = strtab_offset;
            shdr.sh_size      = strtab.size();
            shdr.sh_addralign = 1;
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        // .shstrtab
        {
            Elf64_Shdr shdr;
            std::memset(&shdr, 0, sizeof(shdr));
            shdr.sh_name      = shdr_names[shdr_name_idx++];
            shdr.sh_type      = SHT_STRTAB;
            shdr.sh_offset    = shstrtab_offset;
            shdr.sh_size      = shstrtab.size();
            shdr.sh_addralign = 1;
            file.write(reinterpret_cast<const char*>(&shdr), sizeof(shdr));
        }

        file.close();
        return true;
    }

private:
    std::vector<Arm64Section> sections_;
    std::vector<Arm64SymbolInfo> symbols_;
    std::vector<size_t> symbol_order_;
    std::unordered_map<std::string, uint32_t> symbol_index_map_;

    uint32_t register_symbol(Arm64SymbolInfo& sym) {
        symbols_.push_back(sym);
        uint32_t index = static_cast<uint32_t>(symbol_order_.size());
        symbol_order_.push_back(symbols_.size() - 1);
        symbol_index_map_[sym.name] = index;
        return index;
    }
};

} // namespace jplang

#endif // JPLANG_ARM64_EMITTER_HPP
