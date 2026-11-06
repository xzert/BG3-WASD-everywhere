#pragma once

#include <cstddef>
#include <cstdint>

// Reads the symbol table of a Mach-O image, loaded in memory or from a file. The structures are the
// subset of <mach-o/loader.h> and <mach-o/nlist.h> used here, so this also builds on Linux for the
// host test.
namespace wasd::macho {

inline constexpr uint32_t kMagic64 = 0xFEEDFACF;
inline constexpr uint32_t kFatMagic = 0xCAFEBABE;  // stored big-endian
inline constexpr uint32_t kCpuX86_64 = 0x01000007;
inline constexpr uint32_t kCpuArm64 = 0x0100000C;
inline constexpr uint32_t kExecutable = 0x2;  // MH_EXECUTE
inline constexpr uint32_t kLcSegment64 = 0x19;
inline constexpr uint32_t kLcSymtab = 0x2;

struct Header64 {
    uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};

struct LoadCommand {
    uint32_t cmd, cmdsize;
};

struct SegmentCommand64 {
    uint32_t cmd, cmdsize;
    char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    int32_t maxprot, initprot;
    uint32_t nsects, flags;
};

struct SymtabCommand {
    uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};

struct Nlist64 {
    uint32_t n_strx;
    uint8_t n_type, n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

// An image's symbol table, wherever it is mapped. `slide` is added to every symbol value.
struct Image {
    const Header64* header = nullptr;
    const Nlist64* symbols = nullptr;
    uint32_t symbol_count = 0;
    const char* strings = nullptr;
    uint32_t strings_size = 0;
    intptr_t slide = 0;
};

// A loaded image: `header` is where dyld mapped it and `slide` its ASLR slide.
bool ImageFromMemory(const Header64* header, intptr_t slide, Image& image);

// A slice of a file (see FindSlice). Symbol values stay unslid.
bool ImageFromFile(const uint8_t* slice, size_t size, Image& image);

// The slice for `cputype` in a fat or thin Mach-O file, or nullptr.
const uint8_t* FindSlice(const uint8_t* file, size_t size, uint32_t cputype, size_t& slice_size);

// The segment named `name`, or nullptr.
const SegmentCommand64* FindSegment(const Header64* header, const char* name);

// The file offset of virtual address `address` (unslid) in a slice, or 0 if no segment maps it.
uint64_t FileOffset(const Header64* header, uint64_t address);

// The address of the first defined symbol after `address`, or 0. In a binary that keeps its
// symbols, this is where the function at `address` ends.
uintptr_t NextSymbolAddress(const Image& image, uintptr_t address);

// Looks up defined symbols by name. addresses[i] gets the slid address of names[i], or 0 if it is
// missing. Returns how many were found.
size_t FindSymbols(const Image& image, const char* const* names, uintptr_t* addresses, size_t count);

}  // namespace wasd::macho
