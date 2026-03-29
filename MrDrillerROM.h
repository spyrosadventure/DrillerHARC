#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct RomEntry {
    char     name[0x41] = {};   
    uint32_t length     = 0;    
    uint32_t offset     = 0;    
};

struct RomArchive {
    char     arc_name[0x41] = {};  
    uint32_t rom_size       = 0;   
    uint32_t file_count     = 0;   
    std::vector<RomEntry> entries;
    FILE* fh = nullptr;  
};

inline uint32_t read_uint32_le(FILE* file) {
    uint32_t value = 0;
    fread(&value, 4, 1, file);  

    return value;
}

inline bool load_rom(const char* path, RomArchive& rom) {
    rom.fh = fopen(path, "rb");
    if (!rom.fh) {
        printf("Error: Failed to open file '%s'\n", path);  
        return false;
    }

    printf("Reading header...\n");
    fread(rom.arc_name, 1, 0x40, rom.fh);  

    rom.arc_name[0x40] = '\0';  

    rom.rom_size = read_uint32_le(rom.fh);  

    rom.file_count = read_uint32_le(rom.fh);  

    fseek(rom.fh, 0x18, SEEK_CUR);  

    printf("ROM size: 0x%08X, File count: %u\n", rom.rom_size, rom.file_count);

    rom.entries.reserve(rom.file_count);  

    for (uint32_t i = 0; i < rom.file_count; i++) {
        RomEntry e{};

        if (fread(e.name, 1, 0x40, rom.fh) != 0x40) {
            printf("Error: Failed to read name for entry %u\n", i);
            fclose(rom.fh);
            return false;
        }
        e.name[0x40] = '\0';  

        e.length = read_uint32_le(rom.fh);

        e.offset = read_uint32_le(rom.fh) + 0x10;

        fseek(rom.fh, 0x8, SEEK_CUR);

        rom.entries.push_back(e);  

        printf("Entry %d: Name: '%s', Length: 0x%08X, Offset: 0x%08X\n", i, e.name, e.length, e.offset);
    }

    fseek(rom.fh, rom.file_count * 4, SEEK_CUR);  

    printf("File pointer after skipping offsets: 0x%08X\n", ftell(rom.fh));

    return true;  

}

static std::vector<uint8_t> z_decompress(const uint8_t* src, size_t src_len) {
    std::vector<uint8_t> out(src_len * 4);

    z_stream zs{};
    zs.next_in   = (Bytef*)src;
    zs.avail_in  = (uInt)src_len;
    zs.next_out  = out.data();              // must be set before first inflate
    zs.avail_out = (uInt)out.size();

    if (inflateInit(&zs) != Z_OK) return {};  // plain inflateInit for zlib wrapper (78 xx)

    int r;
    do {
        r = inflate(&zs, Z_NO_FLUSH);
        if (r == Z_STREAM_ERROR || r == Z_DATA_ERROR || r == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return {};
        }
        if (r != Z_STREAM_END && zs.avail_out == 0) {
            size_t old_size = out.size();
            out.resize(old_size * 2);
            zs.next_out  = out.data() + old_size;
            zs.avail_out = (uInt)old_size;
        }
    } while (r != Z_STREAM_END);

    inflateEnd(&zs);
    out.resize(zs.total_out);
    return out;
}

inline std::vector<uint8_t> read_rom_entry(RomArchive& rom, const RomEntry& e) {
    if (!rom.fh || e.length == 0) {
        printf("Error: Invalid length or file handle for entry\n");
        return {};
    }
    if (e.offset >= rom.rom_size) {
        printf("Error: Entry offset (0x%08X) is beyond the file size (0x%08X)\n", e.offset, rom.rom_size);
        return {};
    }
    if (fseek(rom.fh, (long)e.offset, SEEK_SET) != 0) {
        printf("Error: Failed to seek to offset 0x%08X\n", e.offset);
        return {};
    }

    printf("Reading '%s': offset=0x%08X length=0x%08X\n", e.name, e.offset, e.length);

    std::vector<uint8_t> buf(e.length);
    size_t bytes_read = fread(buf.data(), 1, e.length, rom.fh);
    if (bytes_read == 0) {
        printf("Error: Failed to read anything for '%s'\n", e.name);
        return {};
    }
    if (bytes_read != e.length) {
        printf("Warning: Short read for '%s'. Expected 0x%08X bytes, got 0x%08X — continuing\n",
            e.name, e.length, (uint32_t)bytes_read);
        buf.resize(bytes_read);
    }

    printf("  first bytes: %02X %02X %02X %02X\n",
        buf[0], buf[1], buf[2], buf[3]);

    // Only decompress if zlib magic bytes are present
    if (buf.size() >= 2 && buf[0] == 0x78 &&
        (buf[1] == 0x01 || buf[1] == 0x9C || buf[1] == 0xDA)) {
        auto out = z_decompress(buf.data(), e.length);
        if (out.empty()) {
            printf("  decompression failed, returning raw\n");
            return buf;  // fall back to raw instead of failing
        }
        printf("  decompressed %zu bytes, first: %02X %02X %02X %02X\n",
            out.size(), out[0], out[1], out[2], out[3]);
        return out;
    }

    return buf;
}