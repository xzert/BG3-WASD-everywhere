// Runs the mod's signature search on a bg3 binary on disk, so a new game build can be checked
// without starting the game.
//
// Usage: selftest <path to bg3>
#include <elf.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "signatures.hpp"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <path to bg3>\n", argv[0]);
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

    Elf64_Ehdr header;
    std::memcpy(&header, data.data(), sizeof header);
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_ident[EI_CLASS] != ELFCLASS64) {
        std::fprintf(stderr, "%s is not a 64-bit ELF file\n", argv[1]);
        return 2;
    }

    for (int i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr ph;
        std::memcpy(&ph, data.data() + header.e_phoff + i * header.e_phentsize, sizeof ph);
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_X)) {
            continue;
        }

        wasd::GameAddresses game;
        std::string error;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = wasd::FindGameAddresses(data.data() + ph.p_offset, ph.p_filesz, ph.p_vaddr, game, error);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

        std::printf("code segment %#lx, %.0f MiB, searched in %ld ms\n", static_cast<unsigned long>(ph.p_vaddr),
            ph.p_filesz / 1048576.0, static_cast<long>(ms.count()));
        if (!ok) {
            std::printf("FAILED: %s\n", error.c_str());
            return 1;
        }
        std::printf("WASD unlock          %#lx\n", static_cast<unsigned long>(game.wasd_unlock));
        std::printf("IsInControllerMode   %#lx\n", static_cast<unsigned long>(game.is_in_controller_mode));
        std::printf("GetInputValue        %#lx\n", static_cast<unsigned long>(game.get_input_value));
        for (const uintptr_t call : game.get_input_value_calls) {
            std::printf("  called from        %#lx\n", static_cast<unsigned long>(call));
        }

        uintptr_t camera = 0;
        if (!wasd::FindExecuteCameraCommand(data.data() + ph.p_offset, ph.p_filesz, ph.p_vaddr, camera, error)) {
            std::printf("FAILED (optional camera block): %s\n", error.c_str());
            return 1;
        }
        std::printf("ExecuteCameraCommand %#lx\n", static_cast<unsigned long>(camera));

        wasd::RotationAddresses rotation;
        if (!wasd::FindRotationAddresses(data.data() + ph.p_offset, ph.p_filesz, ph.p_vaddr, camera, rotation, error)) {
            std::printf("FAILED (optional camera rotation): %s\n", error.c_str());
            return 1;
        }
        std::printf("Rotation cursor push %#lx, called from", static_cast<unsigned long>(rotation.cursor_push));
        for (const uintptr_t call : rotation.push_calls) {
            std::printf(" %#lx", static_cast<unsigned long>(call));
        }
        std::printf("\nRotation cursor pop  %#lx, called from", static_cast<unsigned long>(rotation.cursor_pop));
        for (const uintptr_t call : rotation.pop_calls) {
            std::printf(" %#lx", static_cast<unsigned long>(call));
        }
        std::printf("\nSetVirtualCursorPos  call at %#lx, vtable slot %#x\n",
            static_cast<unsigned long>(rotation.set_virtual_cursor_call), rotation.set_virtual_cursor_slot);
        return 0;
    }
    std::fprintf(stderr, "no executable segment found\n");
    return 1;
}
