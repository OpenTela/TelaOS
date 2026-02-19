/**
 * External NimBLE configuration
 * This file overrides settings in nimconfig.h
 * Place in include/ folder of your project
 */

#ifndef EXT_NIMBLE_CONFIG_H
#define EXT_NIMBLE_CONFIG_H

// Use external PSRAM for NimBLE host memory allocation
// Saves ~10-50KB of internal DRAM
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1

#endif // EXT_NIMBLE_CONFIG_H
