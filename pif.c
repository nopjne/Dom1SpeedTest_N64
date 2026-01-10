/**
 * @file pif.c
 * @brief PIF hang code for cartridge hotswap support
 * 
 * Adapted from sharksaver64 by Jhynjhiruu
 * Original source: https://github.com/Jhynjhiruu/sharksaver64/blob/main/src/pif.c
 */

#include <libdragon.h>

#include "pif.h"

// PIF shenanigans

// the type that this should be isn't really representable in C - it's valid
// to take the address of it, but not to take the value
// specifically, it is *not* a function - calling it would be very very bad
extern char __nasty_label_hack;

static exception_handler_t old_exception_handler;

static uint32_t sr;

static void exception_handler(exception_t *ex) {
    // pass anything that's not a Watch exception to the libdragon handler
    if (ex->type != EXCEPTION_TYPE_CRITICAL) {
        return old_exception_handler(ex);
    }

    if (ex->code != EXCEPTION_CODE_WATCH) {
        return old_exception_handler(ex);
    }

    // IPL1 clobbers Status, so restore it here
    ex->regs->sr = sr;
    // return to the label defined in assembly
    ex->regs->epc = (uint32_t)&__nasty_label_hack;
    // IPL1 clobbers t1, so set it to an obvious sentinel value that'll show up in the crash handler
    ex->regs->t1 = 0xA5A5A5A55A5A5A5A;
}

// Original comment from Jhynjhiruu (sharksaver64):
// I (Jhynjhiruu) make no guarantees about the state of libdragon, your N64 or your house's electrical
// system after calling this function! It's undefined behaviour in so many ways, and is likely the bit
// that's broken if this program stops working at some point in the future. It's also critical.
__attribute__((noinline)) void nasty_hack(void) {
    // very naughty! also probably undefined behaviour but That's Fine™
    // we go into a loop, but not in C so that GCC doesn't optimise the rest away,
    // then put a label here so we can jump here from the exception handler,
    // set t1 as clobbered because IPL1 clobbers it,
    // and set cc and memory as clobbered just in case (probably not necessary)
    __asm__ volatile(
        ".global __nasty_label_hack\n"
        "j .\n"
        "__nasty_label_hack:\n"
        :
        :
        : "t1", "cc", "memory"
    );
}

void hang_pif(void (*ResetCallback)(), void (*SetupCallback)(void)) {
    // IPL1 clobbers Status, so save it
    sr = C0_STATUS();

    MEMORY_BARRIER();

    // install our custom exception handler so we can trap the Watch exception
    old_exception_handler = register_exception_handler(exception_handler);

    if (ResetCallback != NULL) {
        set_RESET_interrupt(true);
        register_RESET_handler(ResetCallback);
    } else {
        set_RESET_interrupt(false);
    }

    MEMORY_BARRIER();

    // set the watchpoint for reads (1 << 1) to SP_STATUS
    C0_WRITE_WATCHLO(PhysicalAddr(SP_STATUS) | (1 << 1));

    if (SetupCallback != NULL) {
        SetupCallback();
    }

    MEMORY_BARRIER();

    nasty_hack();

    MEMORY_BARRIER();

    // disable the watchpoint so we don't get any spurious exceptions later
    C0_WRITE_WATCHLO(0);

    // reinstall libdragon exception handler
    register_exception_handler(old_exception_handler);
}

