#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
// #include <cstdio>


#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];

//  10 16-bit registers
enum
{
    R_R0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,     // condition flags tell us info about the previous calculation
    R_COUNT
};


enum
{
    MR_KBSR = 0XFE00,   // keyboard status register
    MR_KBDR = 0XFE02    // keyboard data register
};


uint16_t reg[R_COUNT];      // 10 registers in an array each holding 16 bits

enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};

enum
{
    FL_POS = 1 << 0, /* P = 001 */
    FL_ZRO = 1 << 1, /* Z = 010 */
    FL_NEG = 1 << 2, /* N = 100 */
};

enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};


// platform specific code to access the keyboard 
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
//////////////////////////////////////////////

/* used to sign extend values when doing arithmetic in immediate mode */
uint16_t sign_extend(uint16_t x, int bit_count) 
{
    // because x is a 16-bit uint16_t number, it already has 0's padded in the front when we pass it
    // as an argument to this function
    if ((x >> (bit_count - 1)) & 1) {
        // the line below performs a bitwise OR to sign-extend bunch of 1's to a negative x
        x |= (0xFFFF << bit_count);
    }
    return x;
}


/* Update flag register to indicate the sign of the previous calculation's result. 
    The result in most cases is stored in dr */
void update_flags(uint16_t r) 
{
    // r in this case is the reigster that we write the result of our calculation to 
    if (reg[r] == 0) {
        // if the result of the addition is zero, then we set the cond flag to zero
        reg[R_COND] = FL_ZRO;
    } else if (reg[r] >> 15) {
        // in this case, the MSB is 1 so the number is -ve
        reg[R_COND] = FL_NEG;
    } else {
        reg[R_COND] = FL_POS;
    }
}

uint16_t swap16(uint16_t x) {
    return (x << 8) | (x >> 8);
}


void read_image_file(FILE* file) {
    // first 16 bits of the program file give us the origin i.e. where in memory to place the image
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    // we swap the bits to convert to big endian as most modern computers are little endian by default
    origin = swap16(origin);

    // we know the max file size 
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;  // equivalent to &memory[origin]
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // we swap to little endian as that's the format on most computers
    // this loop decrements value of read for the next loop iteration by 1 
    // while (read-- > 0) {
    //     *p = swap16(*p);
    //     ++p;
    // }

    for (size_t i = 0; i < read; ++i) {
        uint16_t original = p[i];
        p[i] = swap16(p[i]);
        // printf("swapped 0x%04X -> 0x%04X\n", original, p[i]);
    }
}

int read_image(const char* image_path) {
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0 ; };
    read_image_file(file);
    fclose(file);
    return 1;
}





// Function declaration (before usage)
// writes contents at memory in the second argument to the memory location specified by the first argument
void mem_write(uint16_t address, uint16_t value) {
    memory[address] = value;
}

// Function declaration (before usage)
// existence of MR (memory mapped registers) means that we need getter and setter functions because we cannot read 
// and write to the memory array directly 
uint16_t mem_read(uint16_t address) {
    if (address == MR_KBSR) {
        if (check_key()) {
            memory[MR_KBSR] = (1 << 15);
            printf("Waiting for keypress...\n");
            memory[MR_KBDR] = getchar();
            printf("Got: %c\n", memory[MR_KBDR]);
        } else {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

// for debugging purposes only:
const char* opcode_name(uint16_t instr) {
    static const char* names[] = {
        "BR", "ADD", "LD", "ST",
        "JSR", "AND", "LDR", "STR",
        "RTI", "NOT", "LDI", "STI",
        "JMP", "RES", "LEA", "TRAP"
    };
    return names[instr >> 12];
}
/////////////

int main(int argc, const char* argv[]) {
    // handling command line inputs
    // VM image: files that contain binary code for the LC-3 VM to execute
    if (argc < 2)
    {
        // we expect one or more path to the VM images
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        // TODO
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }


    // setup
    printf("Disabling input buffering...\n");
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();


    /* Since exactly one condition flag should be set, we set the Z flag */
    reg[R_COND] = FL_ZRO;       // setting the condition flag in the register

    // set the PC to starting position. Default starting position is 0x3000
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;       // setting program counter to the starting position 

    int running = 1;
    while (running) {
        /* FETCH instruction */
        uint16_t pc = reg[R_PC];
        uint16_t instr = mem_read(reg[R_PC]++);    
        // after fetching instruction, we also increment the program counter to the next instruction
        uint16_t op = instr >> 12;      // getting 4 most sig bits 

        // printing each instruction before executing it:
        // printf("[PC=0x%04X] INSTR=0x%04X  OP=%-5s\n", pc, instr, opcode_name(instr));

        switch (op) 
        {
            case OP_ADD:
                // all addresses are stored in uint16_t type variables because our register conist of those 
                // addresses only

                {// destination address is from bits 11-9 (3 bits)
                    uint16_t dr = (instr >> 9) & 0x7;
                    /* Source register 1 */
                    uint16_t sr1 = (instr >> 6) & 0x7;   
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag) {
                        // in this case, we are in immediate mode and can add the value in the last 5 bits 
                        // We need to sign-extend that value first 
                        uint16_t imm5 = sign_extend((instr & 0x1F), 5);
                        // each register holds a 16-bit value
                        reg[dr] = reg[sr1] + imm5;
                    } else {
                        // in this case, we need value stored in sr2
                        uint16_t sr2 = (instr & 0x7);
                        reg[dr] = reg[sr1] + reg [sr2];
                    }
                    update_flags(dr);
                }
                break;
            case OP_AND:
                {
                    uint16_t dr = (instr >> 9) & 0x7;
                    uint16_t sr1 = (instr >> 6) & 0x7;
                    uint16_t imm_flag = (instr >> 5) & 0x1;

                    if (imm_flag) {
                        uint16_t imm5 = sign_extend((instr & 0x1F), 5);
                        reg[dr] = reg[sr1] & imm5;
                    } else {
                        uint16_t sr2 = (instr & 0x7);
                        reg[dr] = reg[sr1] & reg [sr2];
                    }
                    update_flags(dr);
                }
                break;
            case OP_NOT:
                {
                    uint16_t sr = (instr >> 6) & 0x7;
                    uint16_t dr = (instr >> 9) & 0x7;

                    reg[dr] = ~reg[sr];
                    update_flags(dr);
                }
                break;
            case OP_BR:
                {   
                    // in this operation, we test the condition codes which are stored in 
                    // R_COND register and their values are defined above as follows:
                    //  FL_POS = 1 << 0, /* P = 001 */
                    // FL_ZRO = 1 << 1, /* Z = 010 */
                    // FL_NEG = 1 << 2, /* N = 100 */
                    uint16_t pc_offset = sign_extend((instr & 0x1FF), 9);

                    //////////////////////////////////////////////
                    // int n = instr & 0x0800;
                    // int z = instr & 0x0400;
                    // int p = instr & 0x0200;

                    // uint16_t last_sign = reg[R_COND];

                    // if ((n && last_sign == FL_NEG) || (z && last_sign == FL_ZRO) || (p && last_sign == FL_POS)) {
                    //     reg[R_PC] = reg[R_PC] + pc_offset;
                    // }
                    //////////////////////////////////////////////

                    // the code above accomplishes the same job as the code below but its quite 
                    // redundant. we can use the fact that flags are stored as 3 bit combinations
                    // and the R_COND register also stores the flags as 3 bits number representing 1, 2, or 4

                    uint16_t cond_flags = (instr >> 9) & 0x7;
                    if (cond_flags & reg[R_COND]) {
                        // the if condition matches the flags enabled with the flag in the r_cond register
                        // thus it will always be positive (because cond flags are 1, 2 or 4)
                        reg[R_PC] = reg[R_PC] + pc_offset;
                    }
                    // no need to update condition flags here because no register value was changed
                }
                break;
            case OP_JMP:
                {
                    uint16_t base_r = (instr >> 6) & 0x7;
                    // since base_r is just 3 bits we are guaranteed that it is one of the 7 registers that exists
                    reg[R_PC] = reg[base_r];
                }
                break;
            case OP_JSR:
                {
                    // we store current pc in r7. This is out linkage back to the calling routine
                    reg[R_R7] = reg[R_PC];
                    // JSR now contains the 11th bit. 

                    // Blud why would you assign JSR to be 8 bits :(
                    // uint16_t JSR = (instr & 0x0800);
                    //////////////

                    uint16_t JSR = (instr >> 11) & 1;
                    if (JSR) {
                        // if the 12th bit is 1 then we sign extend the offset and add it to PC
                        reg[R_PC] += sign_extend((instr & 0x07FF), 11);
                    } else {
                        uint16_t base_r = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[base_r];
                    }
                }
                break;
            case OP_LD:
                {
                    uint16_t dr = (instr >> 9) & 0x7;
                    uint16_t offset = sign_extend(instr & 0x01FF, 9);

                    reg[dr] = mem_read(reg[R_PC] + offset);
                    update_flags(dr);
                }
                break;
            case OP_LDI:
                {  
                    uint16_t dr = (instr >> 9) & 0x7;

                    // isolating the last 9 bits 
                    uint16_t pc_offset = sign_extend((instr & 0x1FF), 9);
                    uint16_t addr = reg[R_PC] + pc_offset;
                    // This is not needed because in the execution loop, the PC is incremented automatically
                    // reg[R_PC] = reg[R_PC] + pc_offset + 1;

                    // addr: memory address of a value. This value is another address (addr2) and we 
                    // need to fetch the value at addr2

                    reg[dr] = mem_read(mem_read(addr));
                    update_flags(dr);
                }
                break;
            case OP_LDR:
                {
                    uint16_t offset = sign_extend(instr & 0x003F, 6);
                    uint16_t base_r = (instr >> 6) & 0x7;
                    uint16_t dr = (instr >> 9) & 0x7;

                    reg[dr] = mem_read(reg[base_r] + offset);
                    update_flags(dr);
                }
                break;
            case OP_LEA:
                {
                    uint16_t offset = sign_extend(instr & 0x01FF, 9);
                    uint16_t dr = (instr >> 9) & 0x7;

                    reg[dr] = reg[R_PC] + offset;
                    update_flags(dr);
                }
                break;
            case OP_ST:
                {
                    uint16_t offset = sign_extend(instr & 0x01FF, 9);
                    uint16_t sr = (instr >> 9) & 0x7;

                    // mem_write writes the contents of the second argument to the address specified by the 
                    // first argument 
                    mem_write(reg[R_PC] + offset, reg[sr]);
                }
                break;
            case OP_STI:
                {
                    // offset in this case is 6 bits
                    uint16_t offset = sign_extend(instr & 0x01FF, 6);
                    uint16_t sr = (instr >> 9) & 0x7;

                    mem_write(mem_read(reg[R_PC] + offset), reg[sr]);
                }
                break;
            case OP_STR:
                {
                    uint16_t offset = sign_extend(instr & 0x003F, 6);
                    uint16_t base_r = (instr >> 6) & 0x7;
                    uint16_t sr = (instr >> 9) & 0x7;

                    mem_write(reg[base_r] + offset, reg[sr]);
                }
                break;
            case OP_TRAP:
                reg[R_R7] = reg[R_PC];

                // the switch statement below allows us to execute the sys call specified by trapvector8 (the last 8 bits)
                switch (instr & 0x00FF)
                {
                    case TRAP_GETC:
                        {
                            // read a single ASCII character
                            reg[R_R0] = (uint16_t) getchar();
                            update_flags(R_R0);
                            break;
                        }
                    case TRAP_OUT:
                        {
                            // write a single character in R0 to the console display
                            putc((char)reg[R_R0], stdout);
                            fflush(stdout);
                            break;
                        }
                    case TRAP_PUTS:
                        {
                            {
                                uint16_t* c = memory + reg[R_R0];
                                while (*c) {
                                    putc((char)*c, stdout);
                                    // line below advances one word (16-bit) forward because lc-3 stores 1 char per 16 bit word
                                    ++c;
                                }
                                fflush(stdout);
                                break;
                            }
                        }
                    case TRAP_IN:
                        {
                            // print a prompt for an input character and read a single character from the keyboard
                            printf("Enter a character: ");
                            char c = getchar();
                            // character is echoed onto the console monitor 
                            putc(c, stdout);
                            fflush(stdout);

                            // its ASCII code is then copied into R0
                            reg[R_R0] = (uint16_t) c;
                            update_flags(R_R0);
                            break;
                        }
                    case TRAP_PUTSP:
                        {
                            uint16_t* c = memory + reg[R_R0];
                            while (*c) {
                                char char1 = (*c) & 0xFF;
                                // char in first 8 bits is written first
                                putc(char1, stdout);
                                char char2 = (*c) >> 8;
                                if (char2) putc(char2, stdout);
                                ++c;
                            }
                            fflush(stdout);
                            // writing terminates with the occurence of x0000 in a memory location
                            break;
                        }
                    case TRAP_HALT:
                        {
                            puts("HALT");
                            fflush(stdout);
                            running = 0;
                        }
                        break;
                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                // @{BAD OPCODE}
                break;

        }
        restore_input_buffering();
    }
    

    return 0;
}


