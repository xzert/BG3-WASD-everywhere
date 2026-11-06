#include "macho.hpp"

#include <cstring>

namespace wasd::macho {
namespace {

uint32_t BigEndian32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

// Calls `visit` for each load command until it returns false.
template <typename Visit>
void ForEachCommand(const Header64* header, Visit visit)
{
    auto p = reinterpret_cast<const uint8_t*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        auto command = reinterpret_cast<const LoadCommand*>(p);
        if (command->cmdsize < sizeof(LoadCommand) || !visit(command)) {
            return;
        }
        p += command->cmdsize;
    }
}

const SymtabCommand* FindSymtab(const Header64* header)
{
    const SymtabCommand* symtab = nullptr;
    ForEachCommand(header, [&](const LoadCommand* command) {
        if (command->cmd != kLcSymtab) {
            return true;
        }
        symtab = reinterpret_cast<const SymtabCommand*>(command);
        return false;
    });
    return symtab;
}

// `base` is where file offset 0 of the image would be.
void FillImage(const Header64* header, const SymtabCommand& symtab, uintptr_t base, intptr_t slide,
    Image& image)
{
    image.header = header;
    image.symbols = reinterpret_cast<const Nlist64*>(base + symtab.symoff);
    image.symbol_count = symtab.nsyms;
    image.strings = reinterpret_cast<const char*>(base + symtab.stroff);
    image.strings_size = symtab.strsize;
    image.slide = slide;
}

}  // namespace

const SegmentCommand64* FindSegment(const Header64* header, const char* name)
{
    const SegmentCommand64* found = nullptr;
    ForEachCommand(header, [&](const LoadCommand* command) {
        if (command->cmd != kLcSegment64) {
            return true;
        }
        auto segment = reinterpret_cast<const SegmentCommand64*>(command);
        if (std::strncmp(segment->segname, name, sizeof segment->segname) != 0) {
            return true;
        }
        found = segment;
        return false;
    });
    return found;
}

uint64_t FileOffset(const Header64* header, uint64_t address)
{
    uint64_t offset = 0;
    ForEachCommand(header, [&](const LoadCommand* command) {
        if (command->cmd != kLcSegment64) {
            return true;
        }
        auto segment = reinterpret_cast<const SegmentCommand64*>(command);
        if (address < segment->vmaddr || address >= segment->vmaddr + segment->filesize) {
            return true;
        }
        offset = segment->fileoff + (address - segment->vmaddr);
        return false;
    });
    return offset;
}

bool ImageFromMemory(const Header64* header, intptr_t slide, Image& image)
{
    if (!header || header->magic != kMagic64) {
        return false;
    }
    const SegmentCommand64* linkedit = FindSegment(header, "__LINKEDIT");
    const SymtabCommand* symtab = FindSymtab(header);
    if (!linkedit || !symtab) {
        return false;
    }
    // The symbol table lives in __LINKEDIT, which dyld maps at its vmaddr. Offsets in LC_SYMTAB are
    // file offsets, so they count from where file offset 0 would be in memory.
    const uintptr_t base = uintptr_t(linkedit->vmaddr + slide - linkedit->fileoff);
    FillImage(header, *symtab, base, slide, image);
    return true;
}

bool ImageFromFile(const uint8_t* slice, size_t size, Image& image)
{
    if (!slice || size < sizeof(Header64)) {
        return false;
    }
    auto header = reinterpret_cast<const Header64*>(slice);
    if (header->magic != kMagic64 || sizeof(Header64) + uint64_t(header->sizeofcmds) > size) {
        return false;
    }
    const SymtabCommand* symtab = FindSymtab(header);
    if (!symtab || symtab->symoff + uint64_t(symtab->nsyms) * sizeof(Nlist64) > size ||
        symtab->stroff + uint64_t(symtab->strsize) > size) {
        return false;
    }
    FillImage(header, *symtab, reinterpret_cast<uintptr_t>(slice), 0, image);
    return true;
}

const uint8_t* FindSlice(const uint8_t* file, size_t size, uint32_t cputype, size_t& slice_size)
{
    if (size >= 8 && BigEndian32(file) == kFatMagic) {
        const uint32_t count = BigEndian32(file + 4);
        for (uint32_t i = 0; i < count && 8 + 20 * (uint64_t(i) + 1) <= size; ++i) {
            const uint8_t* arch = file + 8 + 20 * i;
            const uint32_t offset = BigEndian32(arch + 8);
            const uint32_t length = BigEndian32(arch + 12);
            if (BigEndian32(arch) == cputype && uint64_t(offset) + length <= size) {
                slice_size = length;
                return file + offset;
            }
        }
        return nullptr;
    }
    if (size >= sizeof(Header64)) {
        auto header = reinterpret_cast<const Header64*>(file);
        if (header->magic == kMagic64 && header->cputype == cputype) {
            slice_size = size;
            return file;
        }
    }
    return nullptr;
}

uintptr_t NextSymbolAddress(const Image& image, uintptr_t address)
{
    constexpr uint8_t kStab = 0xE0, kTypeMask = 0x0E, kDefinedInSection = 0x0E;
    uintptr_t next = 0;
    for (uint32_t s = 0; s < image.symbol_count; ++s) {
        const Nlist64& symbol = image.symbols[s];
        if ((symbol.n_type & kStab) || (symbol.n_type & kTypeMask) != kDefinedInSection) {
            continue;
        }
        const uintptr_t value = uintptr_t(symbol.n_value + image.slide);
        if (value > address && (!next || value < next)) {
            next = value;
        }
    }
    return next;
}

size_t FindSymbols(const Image& image, const char* const* names, uintptr_t* addresses, size_t count)
{
    constexpr uint8_t kStab = 0xE0, kTypeMask = 0x0E, kDefinedInSection = 0x0E;
    for (size_t i = 0; i < count; ++i) {
        addresses[i] = 0;
    }
    size_t found = 0;
    for (uint32_t s = 0; s < image.symbol_count && found < count; ++s) {
        const Nlist64& symbol = image.symbols[s];
        if ((symbol.n_type & kStab) || (symbol.n_type & kTypeMask) != kDefinedInSection ||
            symbol.n_strx >= image.strings_size) {
            continue;
        }
        const char* name = image.strings + symbol.n_strx;
        for (size_t i = 0; i < count; ++i) {
            if (!addresses[i] && std::strcmp(name, names[i]) == 0) {
                addresses[i] = uintptr_t(symbol.n_value + image.slide);
                ++found;
                break;
            }
        }
    }
    return found;
}

}  // namespace wasd::macho
