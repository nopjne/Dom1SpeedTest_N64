/**
 * @file pif.h
 * @brief PIF hang code for cartridge hotswap support
 * 
 * Adapted from sharksaver64 by Jhynjhiruu
 * Original source: https://github.com/Jhynjhiruu/sharksaver64/blob/main/src/pif.c
 */

#ifndef PIF_H
#define PIF_H

#include <libdragon.h>

/**
 * @brief Hang the PIF to enable cartridge hotswap
 * 
 * This function sets up a watchpoint on SP_STATUS to trap Watch exceptions,
 * allowing the system to continue running while cartridges are swapped.
 * 
 * Adapted from sharksaver64 by Jhynjhiruu
 * 
 * @param ResetCallback Function to call on RESET interrupt (can be NULL)
 * @param SetupCallback Function to call after setting up watchpoint (can be NULL)
 */
void hang_pif(void (*ResetCallback)(), void (*SetupCallback)(void));

#endif // PIF_H

