#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "Claire/Assert.h"

#define LO_ROM_OFFSET 0x7FC0
#define HI_ROM_OFFSET 0xFFC0

struct RomFile {
    uint8_t* data;
    size_t size;
    uint16_t header_offset;
} rom_file;

struct Registers {
    uint32_t PC;
} registers;

void load_rom(const char* path) {
    FILE* fp = fopen(path, "rb");
    ASSERT(fp, "Couldn't load ROM");

    fseek(fp, 0, SEEK_END);
    rom_file.size = ftell(fp);

    if (rom_file.size % 1024 == 512) {
        // We need to skip a header prepended by a copier device or the like. Oughta be 512 bytes
        fseek(fp, 512, SEEK_SET);
        rom_file.size -= 512;
        printf("Note: Headered rom\n");
    } else {
        fseek(fp, 0, SEEK_SET);
    }

    rom_file.data = malloc(rom_file.size);
    size_t bytes_read = fread(rom_file.data, 1, rom_file.size, fp);
    ASSERT(bytes_read == rom_file.size, "Didn't read full rom.. what's up with that..?");

    fclose(fp);
}

uint16_t read_u16(uint8_t* source) {
    uint8_t a = *(source++);
    uint8_t b = *source;

    return (b << 8) | a;
}

int get_heuristic_score_for_header_candidate(size_t offset) {
    int score = 0;

    uint8_t* header = rom_file.data + offset;

    uint8_t speed_and_map_mode = *(header + 0x15);
    uint8_t map_mode = speed_and_map_mode & 0b00001111;

    if (map_mode == 0b00) {
        score += (offset == LO_ROM_OFFSET) ? 1 : -10;
    } else if (map_mode == 0b01) {
        score += (offset == HI_ROM_OFFSET) ? 1 : -10;
    } else if (map_mode == 0b11) {
        ASSERT_NOT_REACHED("Unimplemented: ExHiROM");
    } else {
        score -= 100;
        //ASSERT_NOT_REACHED("Unknown map_mode %d", map_mode);
    }

    uint16_t reset_vector = read_u16((uint8_t*)(rom_file.data + offset + 0x3C));

    if (offset == LO_ROM_OFFSET) {
        if (reset_vector < 0x8000) score -= 10;
    } else if (offset == HI_ROM_OFFSET) {
        if (reset_vector < 0xC000) score -= 10;
    } else {
        ASSERT_NOT_REACHED("Unknown offset");
    }

    return score;
}

void locate_header() {
    size_t winning_offset = LO_ROM_OFFSET;
    size_t winning_score = get_heuristic_score_for_header_candidate(LO_ROM_OFFSET);

    size_t other_score = get_heuristic_score_for_header_candidate(HI_ROM_OFFSET);
    if (other_score > winning_score) {
        winning_offset = HI_ROM_OFFSET;
        winning_score = other_score;
    }

    printf("Determined winning offset: %lx\n", winning_offset);

    rom_file.header_offset = winning_offset;

    char* game_name = malloc(22);
    memcpy(game_name, rom_file.data + rom_file.header_offset, 21);
    game_name[21] = '\0';
    printf("Hello '%s'\n", game_name);
}

void execute_opcode(uint8_t opcode) {
    switch (opcode) {
        default:
            ASSERT_NOT_REACHED("Undefined opcode: 0x%x", opcode);
            break;
    }
}

uint8_t read_mem(uint32_t address) {
    uint8_t bank = address >> 16;
    uint16_t offset = address & 0xFFFF;

    if (rom_file.header_offset == LO_ROM_OFFSET) {
        if (offset >= 0x8000) {
            // LoROM
            uint32_t rom_read = offset - 0x8000 + (bank * 0x8000);
            return *(rom_file.data + rom_read);
        }

    } else if (rom_file.header_offset == HI_ROM_OFFSET) {
        // HiROM

    } else {
        ASSERT_NOT_REACHED("Bad header offset");
    }

    ASSERT_NOT_REACHED("Unsure how to read %x", address);
}

void run() {
    uint16_t reset_vector = read_u16((uint8_t*)(rom_file.data + rom_file.header_offset + 0x3C));
    registers.PC = 0x000000 | (uint32_t)reset_vector;
    printf("PC: %x\n", registers.PC);

    while (true) {
        uint8_t opcode = read_mem(registers.PC++);
        execute_opcode(opcode);
    }
}

int main() {
    printf("Hello world\n");
    load_rom("mairo.smc");
    locate_header();
    run();

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(300, 300, "Claire's SNES Emulator");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(WHITE);
        EndDrawing();
    }

    free(rom_file.data);
    return 0;
}
