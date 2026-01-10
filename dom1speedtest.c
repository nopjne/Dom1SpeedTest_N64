/**
 * @file dom1speedtest.c
 * @brief N64 test ROM for testing Domain 1 (cartridge ROM) read speeds with cartridge hotswap
 */

#include <string.h>
#include <libdragon.h>
#include "pif.h"

// Default Domain 1 speed parameters (can be overridden by Makefile defines)
#ifndef DEFAULT_DOM1_LAT
#define DEFAULT_DOM1_LAT 0xFF
#endif
#ifndef DEFAULT_DOM1_PWD
#define DEFAULT_DOM1_PWD 0xFF
#endif

// Emulator mode: when defined, skip PIF hang and loop at end of test
// Can be defined via Makefile: N64_CFLAGS += -DRUN_ON_EMULATOR
//#define RUN_ON_EMULATOR
#ifdef RUN_ON_EMULATOR
#define RUN_ON_EMULATOR_MODE 1
#else
#define RUN_ON_EMULATOR_MODE 0
#endif

// PI registers structure
typedef struct PI_regs_s {
    volatile void * ram_address;
    uint32_t pi_address;
    uint32_t read_length;
    uint32_t write_length;
} PI_regs_t;
static volatile PI_regs_t * const PI_regs = (PI_regs_t *)0xA4600000;

// Domain 1 (cartridge ROM) address space
#define CART_DOM1_START     0x10000000
#define CART_DOM1_SIZE       0x00800000  // 8MB

// Test configuration
#define NUM_TEST_LOCATIONS  4
#define BYTES_PER_LOCATION  128
#define ADDRESS_SPACING     128  // 128 bytes spacing

// State machine
typedef enum {
    STATE_INIT = 0,
    STATE_SAFE_REMOVE,
    STATE_DETECT,
    STATE_TEST
} test_state_t;

// Speed level definitions
typedef enum {
    SPEED_LEVEL_TOTAL_POS = 0,
    SPEED_LEVEL_ABSOLUTE_POS,
    SPEED_LEVEL_BASICALLY_POS,
    SPEED_LEVEL_MINI_POS,
    SPEED_LEVEL_SLIGHTLY_POS,
    SPEED_LEVEL_COULD_WORK,
    SPEED_LEVEL_DOES_WORK,
    SPEED_LEVEL_OVERACHIEVER,
    SPEED_LEVEL_PERFECTIONIST,
    NUM_SPEED_LEVELS
} speed_level_t;

static const char * SpeedLevelNames[] = {
    "is a total POS",
    "is an absolute POS",
    "is basically aPOS",
    "is a mini POS",
    "is slightly a POS",
    "could work",
    "does work",
    "is an overachiever",
    "is a perfectionist"
};

// Speed level to LAT/PWD mapping
static const uint8_t SpeedLevelLAT[] = {
    0xFF,  // Level 0: Total POS
    0xE0,  // Level 1: Absolute POS
    0xC0,  // Level 2: Basically POS
    0xA0,  // Level 3: Mini POS
    0x80,  // Level 4: Slightly POS
    0x60,  // Level 5: Could work
    0x40,  // Level 6: Does work (anchored)
    0x20,  // Level 7: Overachiever
    0x00   // Level 8: Perfectionist
};

static const uint8_t SpeedLevelPWD[] = {
    0xFF,  // Level 0: Total POS
    0xD4,  // Level 1: Absolute POS
    0xA9,  // Level 2: Basically POS
    0x7E,  // Level 3: Mini POS
    0x53,  // Level 4: Slightly POS
    0x28,  // Level 5: Could work
    0x12,  // Level 6: Does work (anchored)
    0x09,  // Level 7: Overachiever
    0x00   // Level 8: Perfectionist
};

// Global state
static test_state_t CurrentState = STATE_INIT;
static uint8_t ReferenceData[NUM_TEST_LOCATIONS][BYTES_PER_LOCATION] __attribute__ ((aligned(16)));
static char CartridgeName[21];  // 20 bytes + null terminator
static bool FirstInit = true;  // Track if this is the first initialization
static uint8_t MinPWDForLAT[256];  // Minimum working PWD for each LAT (0-255), 0xFF if none found

/**
 * @brief Read from Domain 1 (cartridge ROM)
 */
void CartDom1Read(void * Dest, uint32_t Offset, uint32_t Len) {
    assert(Dest != NULL);
    assert(Offset < CART_DOM1_SIZE);
    assert(Len > 0);
    assert(Offset + Len <= CART_DOM1_SIZE);

    disable_interrupts();
    dma_wait();

    MEMORY_BARRIER();
    PI_regs->ram_address = UncachedAddr(Dest);
    MEMORY_BARRIER();
    PI_regs->pi_address = Offset | CART_DOM1_START;
    MEMORY_BARRIER();
    PI_regs->write_length = Len - 1;
    MEMORY_BARRIER();

    enable_interrupts();
    dma_wait();
}

/**
 * @brief Set Domain 1 speed parameters
 */
void SetDom1Speed(uint8_t LAT, uint8_t PWD, uint8_t PGS, uint8_t RLS) {
    #define PI_BASE_REG          0x04600000
    #define PI_BSD_DOM1_LAT_REG  (PI_BASE_REG+0x14)
    #define PI_BSD_DOM1_PWD_REG  (PI_BASE_REG+0x18)
    #define PI_BSD_DOM1_PGS_REG  (PI_BASE_REG+0x1C)
    #define PI_BSD_DOM1_RLS_REG  (PI_BASE_REG+0x20)
    #define KSEG1 0xA0000000
    #define PHYS_TO_K1(x)       ((uint32_t)(x)|KSEG1)
    #define IO_WRITE(addr,data) (*(volatile uint32_t *)PHYS_TO_K1(addr)=(uint32_t)(data))
    
    IO_WRITE(PI_BSD_DOM1_LAT_REG, LAT);
    IO_WRITE(PI_BSD_DOM1_PWD_REG, PWD);
    IO_WRITE(PI_BSD_DOM1_PGS_REG, PGS);
    IO_WRITE(PI_BSD_DOM1_RLS_REG, RLS);
}

/**
 * @brief Check if cartridge is present using open bus detection
 * 
 * Open bus: when no cartridge, reading returns lower 16 bits of address
 */
bool CartDetectPresence(void) {
    uint32_t TestAddresses[] = {0x10000000, 0x10000004, 0x10000008, 0x1000000C};
    uint32_t ReadValues[4] __attribute__ ((aligned(16)));
    
    // Read from test addresses
    data_cache_hit_invalidate(ReadValues, sizeof(ReadValues));
    for (int i = 0; i < 4; i++) {
        CartDom1Read(&ReadValues[i], TestAddresses[i] - CART_DOM1_START, sizeof(uint32_t));
    }
    data_cache_hit_invalidate(ReadValues, sizeof(ReadValues));
    
    // Display read values
#if 0
    printf("Cart detection reads:\n");
    for (int i = 0; i < 4; i++) {
        printf("  0x%08lX -> 0x%08lX\n", (unsigned long)TestAddresses[i], (unsigned long)ReadValues[i]);
    }
    console_render();
#endif

    // Check if values match open bus pattern (lower 16 bits of address)
    // On N64 (big-endian), the 16-bit value may appear in different positions
    // Only check array indices 0 and 2
    int CheckIndices[] = {0, 2};
    for (int j = 0; j < 2; j++) {
        int i = CheckIndices[j];
        uint32_t Address = TestAddresses[i];
        uint32_t Lower16Bits = Address & 0xFFFF;
        uint32_t ReadValue = ReadValues[i];
        
        // Check if read value matches lower 16 bits
        // On big-endian, check both lower and upper 16 bits
        uint16_t ReadLower16 = (uint16_t)(ReadValue & 0xFFFF);
        uint16_t ReadUpper16 = (uint16_t)((ReadValue >> 16) & 0xFFFF);
        
        if (ReadLower16 == (uint16_t)Lower16Bits || ReadUpper16 == (uint16_t)Lower16Bits) {
            // Matches open bus pattern - continue checking
            continue;
        } else {
            // Doesn't match open bus - cartridge likely present
            return true;
        }
    }
    
    // All addresses matched open bus pattern
    return false;
}

/**
 * @brief Read cartridge name from ROM header
 */
bool CartReadName(char * NameBuffer, size_t BufferSize) {
    if (NameBuffer == NULL || BufferSize < 21) {
        return false;
    }
    
    uint8_t NameData[32] __attribute__ ((aligned(16)));
    data_cache_hit_invalidate(NameData, sizeof(NameData));
    CartDom1Read(NameData, 0x20, sizeof(NameData));
    data_cache_hit_invalidate(NameData, sizeof(NameData));
    
    // Copy and validate name
    bool HasValidChars = false;
    for (int i = 0; i < 20; i++) {
        if (NameData[i] >= 0x20 && NameData[i] <= 0x7E) {
            HasValidChars = true;
        }
        NameBuffer[i] = NameData[i];
    }
    NameBuffer[20] = '\0';  // Ensure null termination
    
    return HasValidChars;
}

/**
 * @brief Read reference data at slowest speed
 */
void ReadReferenceData(void) {
    // Set to slowest speed
    SetDom1Speed(0xFF, 0xFF, 0x07, 0x03);
    
    // Read 128 bytes from 4 locations across the 8MB span
    // Read length must be a multiple of 16 bytes (128 is a multiple of 16)
    for (int i = 0; i < NUM_TEST_LOCATIONS; i++) {
        uint32_t Address = CART_DOM1_START + (i * ADDRESS_SPACING);
        uint32_t Offset = Address - CART_DOM1_START;
        
        data_cache_hit_invalidate(ReferenceData[i], BYTES_PER_LOCATION);
        CartDom1Read(ReferenceData[i], Offset, BYTES_PER_LOCATION);
        data_cache_hit_invalidate(ReferenceData[i], BYTES_PER_LOCATION);
    }
}

/**
 * @brief Test a specific LAT/PWD speed combination
 */
bool TestSpeed(uint8_t LAT, uint8_t PWD) {
    // Set speed
    SetDom1Speed(LAT, PWD, 0x07, 0x03);
    
    // Read 128 bytes from 4 locations and compare with reference data
    // Read length must be a multiple of 16 bytes (128 is a multiple of 16)
    uint8_t ReadBuffer[BYTES_PER_LOCATION] __attribute__ ((aligned(16)));
    
    for (int i = 0; i < NUM_TEST_LOCATIONS; i++) {
        uint32_t Address = CART_DOM1_START + (i * ADDRESS_SPACING);
        uint32_t Offset = Address - CART_DOM1_START;
        
        data_cache_hit_writeback_invalidate(ReadBuffer, BYTES_PER_LOCATION);
        CartDom1Read(ReadBuffer, Offset, BYTES_PER_LOCATION);
        data_cache_hit_writeback_invalidate(ReadBuffer, BYTES_PER_LOCATION);
        
        // Compare all 128 bytes with reference data (read at slowest speed)
        if (memcmp(ReadBuffer, ReferenceData[i], BYTES_PER_LOCATION) != 0) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Map LAT/PWD values to speed level
 */
speed_level_t MapSpeedToLevel(uint8_t LAT, uint8_t PWD) {
    // Find the closest matching speed level
    // We'll match based on which level's LAT/PWD values are closest
    speed_level_t BestLevel = SPEED_LEVEL_TOTAL_POS;
    int BestDistance = 0xFFFF;
    
    for (speed_level_t Level = SPEED_LEVEL_TOTAL_POS; Level < NUM_SPEED_LEVELS; Level++) {
        // Calculate distance (sum of absolute differences)
        int LatDiff = (LAT > SpeedLevelLAT[Level]) ? (LAT - SpeedLevelLAT[Level]) : (SpeedLevelLAT[Level] - LAT);
        int PwdDiff = (PWD > SpeedLevelPWD[Level]) ? (PWD - SpeedLevelPWD[Level]) : (SpeedLevelPWD[Level] - PWD);
        int Distance = LatDiff + PwdDiff;
        
        // Prefer exact match or closest match
        if (Distance < BestDistance) {
            BestDistance = Distance;
            BestLevel = Level;
        }
    }
    
    return BestLevel;
}

/**
 * @brief Calculate speed metric (lower is better)
 * Uses LAT + PWD sum to minimize both parameters equally
 */
static uint32_t CalculateSpeedMetric(uint8_t LAT, uint8_t PWD) {
    return (uint32_t)LAT + (uint32_t)PWD * 2;
}

/**
 * @brief Render the 16x16 speed matrix (256 LAT values displayed as 16x16 grid)
 */
void RenderSpeedMatrix(void) {
    // Clear screen and show header
    console_clear();
    printf("Domain 1 Speed Test\n");
    printf("\nCartridge: %s\n", CartridgeName);
    printf("\nSpeed Matrix (LAT 0-255, showing min PWD):\n");
    printf("      ");
    // Print column header (LAT offset within row: 0x0, 0x1, ..., 0xF)
    // Use 3 spaces per column to align with 2-digit hex values
    for (int Col = 0; Col < 16; Col++) {
        printf(" %X ", Col);
    }
    printf("\n");
    
    // Print matrix rows: each row represents 16 LAT values
    // Row 0: LAT 0x00-0x0F, Row 1: LAT 0x10-0x1F, etc.
    for (int Row = 0; Row < 16; Row++) {
        int BaseLAT = Row * 16;
        printf("LAT%02X: ", BaseLAT);
        for (int Col = 0; Col < 16; Col++) {
            int LAT = BaseLAT + Col;
            if (MinPWDForLAT[LAT] != 0xFF) {
                // Show the minimum PWD value for this LAT
                printf("%02X ", MinPWDForLAT[LAT]);
            } else {
                printf("-- ");  // No working PWD found
            }
        }
        printf("\n");
    }
    console_render();
}

/**
 * @brief Run speed test - find minimum working PWD for each LAT (0-255), displayed as 16x16 grid
 * @param OutLAT Output parameter for fastest working LAT value (best overall)
 * @param OutPWD Output parameter for fastest working PWD value (best overall)
 * @return Speed level corresponding to the fastest working combination
 */
speed_level_t RunSpeedTest(uint8_t * OutLAT, uint8_t * OutPWD) {
    // Read reference data at slowest speed
    ReadReferenceData();
    
    // Initialize matrix - all 256 LAT values
    for (int LAT = 0; LAT < 256; LAT++) {
        MinPWDForLAT[LAT] = 0xFF;  // 0xFF means no working PWD found
    }
    
    // Clear screen and show initial matrix
    console_clear();
    printf("Domain 1 Speed Test\n");
    printf("\nCartridge: %s\n", CartridgeName);
    printf("\nTesting speeds...\n");
    RenderSpeedMatrix();
    
    uint8_t BestLAT = 0xFF;
    uint8_t BestPWD = 0xFF;
    uint32_t BestMetric = 0xFFFFFFFF;
    
    // Test all 256 LAT values (0-255)
    for (int LAT = 0; LAT < 256; LAT++) {
        // For each LAT, find the minimum working PWD (0-255)
        for (int PWD = 0; PWD < 256; PWD++) {
            // Test this combination
            bool Works = TestSpeed((uint8_t)LAT, (uint8_t)PWD);
            
            if (Works) {
                // Update minimum PWD for this LAT
                if (MinPWDForLAT[LAT] == 0xFF || PWD < MinPWDForLAT[LAT]) {
                    // New minimum PWD found - update and render
                    MinPWDForLAT[LAT] = (uint8_t)PWD;
                    RenderSpeedMatrix();
                }
                
                // Check if this is the best overall combination
                uint32_t Metric = CalculateSpeedMetric((uint8_t)LAT, (uint8_t)PWD);
                if (Metric < BestMetric) {
                    BestMetric = Metric;
                    BestLAT = (uint8_t)LAT;
                    BestPWD = (uint8_t)PWD;
                }
            }
        }
        
        // After completing a full row (16 consecutive LAT values), check if they all have the same PWD
        if ((LAT % 16) == 15) {
            // We just completed a row (e.g., LAT 0-15, 16-31, etc.)
            int RowStart = LAT - 15;  // Start of this row
            uint8_t FirstPWD = MinPWDForLAT[RowStart];
            bool AllSame = (FirstPWD != 0xFF);
            
            if (AllSame) {
                // Check if all 16 LAT values in this row have the same PWD
                for (int i = 1; i < 16; i++) {
                    if (MinPWDForLAT[RowStart + i] != FirstPWD) {
                        AllSame = false;
                        break;
                    }
                }
            }
            
            // If all 16 LAT values in this row have the same PWD, assume the rest will too
            if (AllSame && LAT < 255) {
                // Fill remaining LAT values with the same PWD
                for (int RemainingLAT = LAT + 1; RemainingLAT < 256; RemainingLAT++) {
                    MinPWDForLAT[RemainingLAT] = FirstPWD;
                }
                RenderSpeedMatrix();
                break;  // Exit the LAT loop
            }
        }
    }
    
    // Map the best working LAT/PWD to a speed level
    if (BestLAT != 0xFF && BestPWD != 0xFF) {
        if (OutLAT != NULL) {
            *OutLAT = BestLAT;
        }
        if (OutPWD != NULL) {
            *OutPWD = BestPWD;
        }
        return MapSpeedToLevel(BestLAT, BestPWD);
    } else {
        // No working speed found (shouldn't happen, but handle it)
        if (OutLAT != NULL) {
            *OutLAT = 0xFF;
        }
        if (OutPWD != NULL) {
            *OutPWD = 0xFF;
        }
        return SPEED_LEVEL_TOTAL_POS;
    }
}

/**
 * @brief Reset callback for PIF hang
 */
static void ResetCallback(void) {
    // Reset callback - can be used for cleanup if needed
    // For now, just return
}

/**
 * @brief Handle state machine
 */
void HandleStateMachine(void) {
    switch (CurrentState) {
        case STATE_INIT: {
            // Initialize display
            display_init(
                RESOLUTION_320x240,
                DEPTH_32_BPP,
                2,
                GAMMA_NONE,
                ANTIALIAS_RESAMPLE
            );
            rdpq_init();
            console_init();
            debug_init_isviewer();
            
            // Set default Domain 1 speed
            SetDom1Speed(DEFAULT_DOM1_LAT, DEFAULT_DOM1_PWD, 0x07, 0x03);
            
            printf("Domain 1 Speed Test\n");
            
            // Show reset button message only on first initialization
            if (FirstInit) {
                if (!RUN_ON_EMULATOR_MODE) {
                    printf("\nPress RESET button to enable\n");
                    printf("cartridge hotswap support\n");
                    printf("\nWaiting for RESET...\n");
                } else {
                    printf("\nEmulator mode: PIF hang disabled\n");
                }
                console_render();
            } else {
                printf("Initializing...\n");
                console_render();
            }
            
            // Initialize PIF hang for hotswap with reset callback (skip in emulator mode)
#ifndef RUN_ON_EMULATOR
            hang_pif(ResetCallback, NULL);
#endif
            
            // After hang_pif returns (after reset is pressed on first init)
            if (FirstInit) {
                FirstInit = false;
            }
            
            CurrentState = STATE_SAFE_REMOVE;
            break;
        }
        
        case STATE_SAFE_REMOVE: {
            printf("\nSafe to remove cartridge\n");
            console_render();
            
#ifndef RUN_ON_EMULATOR
            // Wait until cartridge is actually removed
            while (CartDetectPresence()) {
                // Cartridge still present, keep waiting
                for (volatile int i = 0; i < 100000; i++);  // Small delay between checks
            }
#endif
            
            // Cartridge removed, transition to detection mode
            CurrentState = STATE_DETECT;
            break;
        }
        
        case STATE_DETECT: {
            if (!CartDetectPresence()) {
                // No cartridge
                console_clear();
                printf("Domain 1 Speed Test\n");
                printf("\nNo cartridge inserted\n");
                console_render();
            } else {
                // Cartridge detected
                if (CartReadName(CartridgeName, sizeof(CartridgeName))) {
                    console_clear();
                    printf("Domain 1 Speed Test\n");
                    printf("\nNew cartridge detected\n");
                    printf("Name: %s\n", CartridgeName);
                    console_render();
                    
                    // Wait a bit before starting test
                    for (volatile int i = 0; i < 2000000; i++);
                    
                    CurrentState = STATE_TEST;
                }
            }
            break;
        }
        
        case STATE_TEST: {
            // Check if cartridge still present
            if (!CartDetectPresence()) {
                // Cartridge removed during test
                console_clear();
                printf("Domain 1 Speed Test\n");
                printf("\nCartridge removed during test\n");
                console_render();
                CurrentState = STATE_DETECT;
                break;
            }
            
            uint8_t FastestLAT, FastestPWD;
            speed_level_t Result = RunSpeedTest(&FastestLAT, &FastestPWD);
            
            // Read 128 bytes using the fastest working speed
            SetDom1Speed(FastestLAT, FastestPWD, 0x07, 0x03);
            uint8_t DisplayData[128] __attribute__ ((aligned(16)));
            data_cache_hit_invalidate(DisplayData, sizeof(DisplayData));
            CartDom1Read(DisplayData, 0, 128);
            data_cache_hit_invalidate(DisplayData, sizeof(DisplayData));
            
            // Display final results with matrix
            console_clear();
            printf("Domain 1 Speed Test\n");
            printf("\nCartridge: %s\n", CartridgeName);
            printf("\nSpeed Matrix (LAT 0-255, showing min PWD):\n");
            printf("      ");
            // Print column header (LAT offset within row: 0x0, 0x1, ..., 0xF)
            // Use 3 spaces per column to align with 2-digit hex values
            for (int Col = 0; Col < 16; Col++) {
                printf(" %X ", Col);
            }
            printf("\n");
            
            // Print matrix rows: each row represents 16 LAT values
            // Row 0: LAT 0x00-0x0F, Row 1: LAT 0x10-0x1F, etc.
            for (int Row = 0; Row < 16; Row++) {
                int BaseLAT = Row * 16;
                printf("LAT%02X: ", BaseLAT);
                for (int Col = 0; Col < 16; Col++) {
                    int LAT = BaseLAT + Col;
                    if (MinPWDForLAT[LAT] != 0xFF) {
                        // Show the minimum PWD value for this LAT
                        printf("%02X ", MinPWDForLAT[LAT]);
                    } else {
                        printf("-- ");  // No working PWD found
                    }
                }
                printf("\n");
            }
            
#ifdef SHOW_REF_BYTES
            printf("\nExpected 128 bytes (reference):\n");
            
            // Display expected 128 bytes in hex format (16 bytes per line)
            for (int i = 0; i < 128; i += 16) {
                printf("%04X: ", i);
                for (int j = 0; j < 16; j++) {
                    if (i + j < 128) {
                        printf("%02X ", ReferenceData[0][i + j]);
                    }
                }
                printf("\n");
            }
            
            printf("\n128 bytes read at fastest speed:\n");
            
            // Display 128 bytes in hex format (16 bytes per line)
            for (int i = 0; i < 128; i += 16) {
                printf("%04X: ", i);
                for (int j = 0; j < 16; j++) {
                    if (i + j < 128) {
                        printf("%02X ", DisplayData[i + j]);
                    }
                }
                printf("\n");
            }
#endif
            
            printf("\nBest overall speed:\n");
            printf("LAT=0x%02X, PWD=0x%02X\n", FastestLAT, FastestPWD);
            printf("Your cart %s\n", SpeedLevelNames[Result]);
            console_render();
            
            // Set Domain 1 speed back to slowest after test completes
            SetDom1Speed(0xFF, 0xFF, 0x07, 0x03);
            
#ifdef RUN_ON_EMULATOR
            // In emulator mode, loop infinitely to keep results visible
            printf("\nEmulator mode: Entering infinite loop\n");
            console_render();
            while (1) {
                // Infinite loop - keep results visible
            }
#endif
            // Wait before returning to detection
            for (volatile int i = 0; i < 5000000; i++);
            
            CurrentState = STATE_SAFE_REMOVE;

            break;
        }
    }
}

int main(void) {
    while (1) {
        HandleStateMachine();
        // Small delay to prevent excessive CPU usage
        for (volatile int i = 0; i < 10000; i++);
    }
    
    return 0;
}

