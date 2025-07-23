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
    uint16_t S;
    uint16_t A;
    uint16_t X;
    uint16_t Y;
    uint16_t D;

    uint8_t E_flag;
    uint8_t DBR;

    union {
        struct {
            uint8_t C : 1; // Carry
            uint8_t Z : 1; // Zero
            uint8_t I : 1; // IRQ Disable
            uint8_t D : 1; // Decimal Mode
            uint8_t X : 1; // Index Register Select
            uint8_t M : 1; // Accumulator Select
            uint8_t V : 1; // Overflow
            uint8_t N : 1; // Negative
        } flags;
        uint8_t byte;
    } status;

} registers;

struct Memory {
    uint8_t WRAM[0xFFFF * 2];

    union {
        struct {
            uint8_t JOYPAD_ENABLE : 1;
            uint8_t _UNUSED_0 : 3;
            uint8_t H_V_IRQ : 1;
            uint8_t _UNUSED_1 : 1;
            uint8_t VBLANK_NMI_ENABLE : 1;
        } flags;
        uint8_t byte;
    } NMITIMEN;

    union {
        struct {
            uint8_t CHANNEL_0 : 1;
            uint8_t CHANNEL_1 : 1;
            uint8_t CHANNEL_2 : 1;
            uint8_t CHANNEL_3 : 1;
            uint8_t CHANNEL_4 : 1;
            uint8_t CHANNEL_5 : 1;
            uint8_t CHANNEL_6 : 1;
            uint8_t CHANNEL_7 : 1;
        } flags;
        uint8_t byte;
    } MDMAEN_GENERAL_PURPOSE;

    union {
        struct {
            uint8_t CHANNEL_0 : 1;
            uint8_t CHANNEL_1 : 1;
            uint8_t CHANNEL_2 : 1;
            uint8_t CHANNEL_3 : 1;
            uint8_t CHANNEL_4 : 1;
            uint8_t CHANNEL_5 : 1;
            uint8_t CHANNEL_6 : 1;
            uint8_t CHANNEL_7 : 1;
        } flags;
        uint8_t byte;
    } MDMAEN_HBLANK_DMA;

    uint8_t APUIO0;
    uint8_t APUIO1;
    uint8_t APUIO2;
    uint8_t APUIO3;

    union {
        struct {
            uint8_t MASTER_BRIGHTNESS : 3;
            uint8_t _UNUSED : 2;
            uint8_t FORCED_BLANKING : 1;
        } flags;
        uint8_t byte;
    } INIDISP;

    union {
        struct {
            uint8_t OBJ_SIZE : 3;
            uint8_t OBJ_GAP : 1;
            uint8_t TILE_BASE : 2;
        } flags;
        uint8_t byte;
    } OBSEL;
} memory;

void breakpoint() {
    getchar();
}

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

uint16_t read_u16_raw(uint8_t* source) {
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
        printf("[%lx] Weird map mode\n", offset);
        score -= 100;
    }

    uint16_t reset_vector = read_u16_raw((uint8_t*)(rom_file.data + offset + 0x3C));

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
    int winning_score = get_heuristic_score_for_header_candidate(LO_ROM_OFFSET);

    int other_score = get_heuristic_score_for_header_candidate(HI_ROM_OFFSET);
    if (other_score > winning_score) {
        printf("%d > %d\n", other_score, winning_score);
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

bool is_acc_16() {
    return (!registers.status.flags.M) && (!registers.E_flag);
}

bool is_index_16() {
    return (!registers.status.flags.X) && (!registers.E_flag);
}


void handle_io_write(uint16_t addr, uint8_t value) {
    switch (addr) {
        case 0x2100: memory.INIDISP.byte = value; break;
        case 0x2101: memory.OBSEL.byte = value; break;
        case 0x2140:
             memory.APUIO0 = value;
             printf("[0] WROTE %x\n", value);
             break;
        case 0x2141:
             memory.APUIO1 = value;
             printf("[1] WROTE %x\n", value);
             break;
        case 0x2142:
             memory.APUIO2 = value;
             printf("[2] WROTE %x\n", value);
             break;
        case 0x2143:
             memory.APUIO3 = value;
             printf("[3] WROTE %x\n", value);
             break;
        case 0x4200: memory.NMITIMEN.byte = value; break;
        case 0x420B: memory.MDMAEN_GENERAL_PURPOSE.byte = value; break;
        case 0x420C: memory.MDMAEN_HBLANK_DMA.byte = value; break;
        default:
            ASSERT_NOT_REACHED("Unsure how to handle write to I/O register %x (val %x)", addr, value);
    }
}

uint8_t handle_io_read(uint16_t addr) {
    if (is_acc_16()) {
        switch (addr) {
            case 0x2140: return (memory.APUIO1 << 8) | memory.APUIO0;
            case 0x2141: return (memory.APUIO2 << 8) | memory.APUIO1;
            default:
                ASSERT_NOT_REACHED("[ACC16] Unsure how to handle read from I/O register %x)", addr);
        }
    }

    switch (addr) {
        case 0x2140: return memory.APUIO0;
        case 0x2141: return memory.APUIO1;
        case 0x2142: return memory.APUIO2;
        case 0x2143: return memory.APUIO3;
        default:
            ASSERT_NOT_REACHED("Unsure how to handle read from I/O register %x)", addr);
    }
}

uint8_t read_mem(uint32_t loc) {
    uint8_t bank = loc >> 16;
    uint16_t addr = loc & 0xFFFF;

    if (rom_file.header_offset == LO_ROM_OFFSET) {
        if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
            if (addr < 0x2000) {
                return memory.WRAM[addr];
            } else if (addr < 0x6000) {
                return handle_io_read(addr);
            }
        }

        if (addr >= 0x8000) {
            // LoROM
            uint32_t rom_read = addr - 0x8000 + (bank * 0x8000);
            return *(rom_file.data + rom_read);
        }
    } else if (rom_file.header_offset == HI_ROM_OFFSET) {
        // HiROM
        printf("HIROM\n");

    } else {
        ASSERT_NOT_REACHED("Bad header offset");
    }

    ASSERT_NOT_REACHED("Unsure how to read %x", loc);
}

uint16_t read_u16(uint32_t addr) {
    uint8_t a = read_mem(addr);
    uint8_t b = read_mem(addr + 1);

    return (b << 8) | a;
}

void write_u8(uint32_t loc, uint8_t value) {
    ASSERT(rom_file.header_offset == LO_ROM_OFFSET, "Unsure how to write to HiROM");
    uint8_t bank = loc >> 16;
    uint16_t addr = loc & 0xFFFF;

    if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) {
        if (addr < 0x2000) {
            memory.WRAM[addr] = value;
            return;
        } else if (addr < 0x6000) {
            handle_io_write(addr, value);
            return;
        }
    }

    if (bank == 0x7E) {
        memory.WRAM[addr] = value;
        return;
    } else if (bank == 0x7F) {
        memory.WRAM[addr + 0x10000] = value;
        return;
    }

    ASSERT_NOT_REACHED("Unimplemented write to %x", loc);
}

void write_u16(uint32_t loc, uint16_t value) {
    write_u8(loc, value >> 8);
    write_u8(loc + 1, value & 0xFF);
}

uint8_t eat_u8() {
    uint8_t out = read_mem(registers.PC++);
    printf(" %x", out);
    return out;
}

uint16_t eat_u16() {
    uint8_t a = read_mem(registers.PC++);
    uint8_t b = read_mem(registers.PC++);

    uint16_t out = (b << 8) | a;
    printf(" %x", out);
    return out;
}

uint32_t eat_u24() {
    uint8_t a = eat_u8();
    uint8_t b = eat_u8();
    uint8_t c = eat_u8();

    return 0x000000 | (c << 16) | (b << 8) | a;
}

void eat_cycles(int count) {
    // ...
}

void set_register(uint16_t* reg, uint16_t value) {
    if (is_acc_16()) {
        *(reg) = value;
        registers.status.flags.N = !!(value >> 15);
        registers.status.flags.Z = value == 0;
    } else {
        uint8_t val_8 = value & 0xFF;
        *(reg) = (*reg & 0xFF00) | val_8;
        registers.status.flags.N = !!(val_8 & 0b10000000);
        registers.status.flags.Z = val_8 == 0;
    }
}

uint32_t addr_from_absolute(uint16_t addr) {
    return (registers.DBR << 16) | addr;
}

void set_low_byte(uint16_t* loc, uint8_t value) {
    (*loc) = (*loc & 0xFF00) | (value & 0xFF);
}

void set_high_byte(uint16_t* loc, uint8_t value) {
    (*loc) = (value << 16) | (*loc & 0xFF);
}

bool auto_negative(uint16_t value) {
    int bits = is_acc_16() ? 15 : 7;
    return !!(value & (0b1 << bits));
}

bool auto_zero(uint16_t value) {
    if (!is_acc_16()) value = value & 0x00FF;
    return value == 0;
}

void push_u8_to_stack(uint8_t value) {
    write_u8(registers.S--, value);
}

void push_u16_to_stack(uint16_t value) {
    write_u8(registers.S--, value >> 8);
    write_u8(registers.S--, value & 0xFF);
}

void increment(uint16_t* reg, int32_t sign) {
    eat_cycles(2);
    if (is_index_16()) {
        (*reg) = *reg+ sign;
        registers.status.flags.Z = !(*reg);
        registers.status.flags.N = !!(*reg>> 15);
    } else {
        uint8_t val = (*reg& 0xFF) + sign;
        set_low_byte(reg, val);
        registers.status.flags.Z = !val;
        registers.status.flags.N = !!(val >> 7);
    }
}

void execute_opcode(uint8_t opcode) {
    switch (opcode) {
       case 0x08: {
            eat_cycles(3);
            push_u8_to_stack(registers.status.byte);
            break;
       } case 0x48: {
            eat_cycles(3);
            if (is_acc_16()) {
                eat_cycles(1);
                push_u16_to_stack(registers.A);
            } else {
                push_u8_to_stack(registers.A);
            }
            break;
       } case 0x8B: {
            eat_cycles(3);
            push_u8_to_stack(registers.DBR);
            break;
       } case 0x0B: {
            eat_cycles(4);
            push_u8_to_stack(registers.D);
            break;
       } case 0x4B: {
            eat_cycles(3);
            // PBR
            push_u8_to_stack(read_mem(0x3034));
            break;
       } case 0xDA: {
            eat_cycles(3);
            if (is_index_16()) {
                eat_cycles(1);
                push_u16_to_stack(registers.X);
            } else {
                push_u8_to_stack(registers.X);
            }
            break;
       } case 0x5A: {
            eat_cycles(3);
            if (is_index_16()) {
                eat_cycles(1);
                push_u16_to_stack(registers.Y);
            } else {
                push_u8_to_stack(registers.Y);
            }
            break;
       } case 0x10: {
            bool take_branch = !registers.status.flags.N;
            eat_cycles(2);

            int8_t relative = (int8_t)eat_u8();

            if (registers.E_flag) eat_cycles(1);
            if (take_branch) {
                eat_cycles(1);
                registers.PC += relative;
            }
            break;
       } case 0xD0: {
            bool take_branch = !registers.status.flags.Z;
            eat_cycles(2);

            int8_t relative = (int8_t)eat_u8();

            if (registers.E_flag) eat_cycles(1);
            if (take_branch) {
                eat_cycles(1);
                registers.PC += relative;
            }
            break;
       } case 0x80: {
            // TODO: Make brnaching generic
            eat_cycles(registers.E_flag ? 4 : 3);
            int8_t relative = (int8_t)eat_u8();
            registers.PC += relative;
            break;
       } case 0x20: {
            eat_cycles(6);
            uint32_t loc = addr_from_absolute(eat_u16());
            uint16_t return_addr = registers.PC - 1;

            push_u16_to_stack(return_addr);

            registers.PC = loc;
            break;
       } case 0x18: {
            eat_cycles(2);
            registers.status.flags.C = 0;
            break;
       } case 0x58: {
            eat_cycles(2);
            registers.status.flags.I = 0;
            break;
       } case 0xB8: {
            eat_cycles(2);
            registers.status.flags.V = 0;
            break;
       } case 0xD8: {
            eat_cycles(2);
            registers.status.flags.D = 0;
            break;
       } case 0x38: { // SEC
            eat_cycles(2);
            registers.status.flags.C = 1;
            break;
       } case 0x78: { // SEI
            eat_cycles(2);
            registers.status.flags.I = 1;
            break;
       } case 0xF8: { // SED
            eat_cycles(2);
            registers.status.flags.D = 1;
            break;
       } case 0xCD: {
            eat_cycles(is_acc_16() ? 5 : 4);
            uint32_t addr = addr_from_absolute(eat_u16());
            uint16_t value = is_acc_16() ? read_u16(addr) : read_mem(addr);
            uint16_t a = registers.A & (is_acc_16() ? 0xFFFF : 0xFF);
            uint16_t out = a - value;
            printf("CD with %x in A, have val%x \n", registers.A, value);

            registers.status.flags.N = auto_negative(out);
            registers.status.flags.Z = a == value;
            registers.status.flags.C = a >= value;

            break;
       } case 0xE2: {
            eat_cycles(3);
            registers.status.byte |= eat_u8();
            if (registers.status.flags.X) {
                set_high_byte(&registers.X, 0x00);
                set_high_byte(&registers.Y, 0x00);
            }
            break;
       } case 0xE9: { // SBC #const
            eat_cycles(is_acc_16() ? 3 : 2);
            uint16_t val = is_acc_16() ? eat_u16() : (0 | eat_u8());

            if (registers.status.flags.D && !is_acc_16()) {
                // ...
                ASSERT_NOT_REACHED("Decimal subtraction not implemented");
            } else {
                uint16_t max_mask = is_acc_16() ? 0xFFFF : 0xFF;
                uint16_t old_a = registers.A;

                uint32_t unclamped = registers.A + (~val) + registers.status.flags.C;
                registers.A = unclamped & max_mask;
                
                registers.status.flags.C = unclamped > (uint32_t)max_mask;

                // Totally stole this. Basically determines if for C = A - B, where sign(A) != sign(B), sign(C) == sign(B)
                registers.status.flags.V = !!(((old_a ^ val) & (old_a ^ registers.A)) & (is_acc_16() ? 0x8000 : 0x80));
            }

            registers.status.flags.N = auto_negative(registers.A);
            registers.status.flags.Z = auto_zero(registers.A);

            break;
       } case 0xAA: {
            eat_cycles(2);
            if (registers.status.flags.X) {
                set_low_byte(&registers.X, registers.A);
                registers.status.flags.N = !!(registers.A & (0b1 << 7));
                registers.status.flags.Z = !(registers.A & 0xFF);
            } else {
                registers.X = registers.A;
                registers.status.flags.N = !!(registers.A & (0b1 << 15));
                registers.status.flags.Z = registers.A == 0;
            }
            break;
       } case 0xA8: {
            eat_cycles(2);
            if (registers.status.flags.X) {
                set_low_byte(&registers.Y, registers.A);
                registers.status.flags.N = !!(registers.A & (0b1 << 7));
                registers.status.flags.Z = !(registers.A & 0xFF);
            } else {
                registers.Y = registers.A;
                registers.status.flags.N = !!(registers.A & (0b1 << 15));
                registers.status.flags.Z = registers.A == 0;
            }
            break;
       } case 0x5B: {
            eat_cycles(2);
            registers.D = registers.A;
            registers.status.flags.N = registers.A >> 15;
            registers.status.flags.Z = registers.A == 0;
            break;
       } case 0x1B: {
            eat_cycles(2);
            registers.S = registers.A;
            registers.status.flags.N = registers.A >> 15;
            registers.status.flags.Z = registers.A == 0;
            break;
       } case 0xCA: {
           increment(&registers.X, -1);
            break;
       } case 0x88: {
           increment(&registers.Y, -1);
            break;
       } case 0xE8: {
           increment(&registers.X, 1);
            break;
       } case 0xC8: {
           increment(&registers.Y, 1);
            break;
       } case 0x8D: {
            eat_cycles(is_acc_16() ? 5 : 4);
            uint32_t loc = addr_from_absolute(eat_u16());
            write_u16(loc, registers.A & (is_acc_16() ? 0xFFFF : 0xFF));
            break;
       } case 0x8F: {
            eat_cycles(is_acc_16() ? 6 : 5);
            uint32_t loc = eat_u24();
            write_u16(loc, registers.A & (is_acc_16() ? 0xFFFF : 0xFF));
            break;
       } case 0x98: {
            eat_cycles(2);
            if (is_acc_16()) {
                registers.A = registers.Y;
            } else {
                registers.A = (registers.A & 0xFF00) | (registers.Y & 0xFF);
            }
            registers.status.flags.N = auto_negative(registers.A);
            registers.status.flags.Z = auto_zero(registers.A);
            break;
       } case 0x9C: { // STZ addr
            eat_cycles(is_acc_16() ? 5 : 4);
            uint32_t loc = addr_from_absolute(eat_u16());
            if (is_acc_16()) {
                write_u16(loc, 0x0000);
            } else {
                write_u8(loc, 0x00);
            }
            break;
       } case 0x9F: {
            eat_cycles(is_acc_16() ? 6 : 5);
            uint32_t loc = eat_u24() + registers.X;
            if (is_acc_16()) {
                write_u16(loc, registers.X);
            } else {
                write_u8(loc, registers.X & 0xFF);
            }
            break;
       } case 0xA0: {
            eat_cycles(is_acc_16() ? 3 : 2);
            uint16_t value = is_acc_16() ? eat_u16() : eat_u8();
            set_register(&registers.Y, value);
            break;
       } case 0xA2: {
            eat_cycles(is_acc_16() ? 3 : 2);
            uint16_t value = is_acc_16() ? eat_u16() : eat_u8();
            set_register(&registers.X, value);
            break;
       } case 0xA9: {
            eat_cycles(is_acc_16() ? 3 : 2);
            uint16_t value = is_acc_16() ? eat_u16() : eat_u8();
            set_register(&registers.A, value);
            break;
       } case 0xB7: {
            eat_cycles(is_acc_16() ? 7 : 6);
            uint32_t loc = addr_from_absolute(eat_u8());
            uint16_t y = is_acc_16() ? registers.Y : (registers.Y & 0xFF);
            loc += y;

            uint16_t value = is_acc_16() ? read_u16(loc) : read_mem(loc);
            set_register(&registers.A, value);
            break;
       } case 0xC2: {
            eat_cycles(3);
            registers.status.byte = registers.status.byte & (~eat_u8());
            if (registers.E_flag) {
                registers.status.flags.X = 1;
                registers.status.flags.M = 1;
            }
            break;
       } case 0xFB: {
            eat_cycles(2);
            uint8_t old_e = registers.E_flag;
            registers.E_flag = registers.status.flags.C;
            registers.status.flags.C = old_e;

            if (registers.E_flag) {
                registers.status.flags.M = 1;
                registers.status.flags.X = 1;
                registers.S = 0x0100 | (registers.S & 0xFF);
                registers.X = 0x0000 | (registers.X & 0xFF);
                registers.Y = 0x0000 | (registers.Y & 0xFF);
            }
            break;
        break;
       } default:
            ASSERT_NOT_REACHED("Undefined opcode: 0x%x", opcode);
            break;
    }
}

void run() {
    uint16_t reset_vector = read_u16_raw((uint8_t*)(rom_file.data + rom_file.header_offset + 0x3C));
    registers.PC = 0x000000 | (uint32_t)reset_vector;
    printf("PC: %x\n", registers.PC);
    size_t op_count = 0;

    while (true) {
        op_count++;
        printf("[x::%x] ::", registers.PC);
        
        // TOTAL HACK FOR SMW
        if (op_count == 10) {
            memory.APUIO0 = 0xAA;
            memory.APUIO1 = 0xBB;
        }

        uint8_t opcode = eat_u8();
        execute_opcode(opcode);
        printf("\n");
    }
}

void setup_cpu() {
    registers.DBR = 0x00;
    registers.status.flags.M = 1;
    registers.status.flags.X = 1;
    registers.status.flags.D = 0;
    registers.status.flags.I = 1;
    registers.E_flag = 1;
}

int main() {
    printf("Hello world\n");
    setup_cpu();
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
