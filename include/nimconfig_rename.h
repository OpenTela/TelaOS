/**
 * NimBLE 2.x configuration overrides
 * This file is automatically included by nimconfig.h
 * 
 * IMPORTANT: After adding this file, do a FULL rebuild:
 *   pio run -t clean && pio run
 */

#pragma once

// ============================================
// PSRAM ALLOCATION - CRITICAL FOR LOW DRAM
// ============================================
// Move NimBLE buffers to external PSRAM
// Saves ~10-50KB of internal DRAM
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1

// ============================================
// MEMORY OPTIMIZATION
// ============================================
// Reduce max connections (default is 3)
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1

// Reduce max bonds (default is 3)  
#define CONFIG_BT_NIMBLE_MAX_BONDS 1

// Disable unused roles to save memory
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 0
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 0

// Keep only peripheral (server) role
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 1
#define CONFIG_BT_NIMBLE_ROLE_BROADCASTER 1

// ============================================
// OPTIONAL: Debug (uncomment if needed)
// ============================================
// #define CONFIG_BT_NIMBLE_DEBUG 1
// #define CONFIG_BT_NIMBLE_LOG_LEVEL 4
