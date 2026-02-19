# EOS Source Bundle

## Structure

```
src/
├── core/
│   ├── console.h          # Console v3 - Result with binary support
│   ├── console.cpp        # Transport-agnostic command processor
│   ├── transport.h        # Abstract transport interface
│   ├── ble_transport.h    # BLE transport implementation
│   └── serial_transport.h # Serial transport implementation
└── ble/
    └── ble_bridge.cpp     # BLE bridge using transports

tests/
├── externals.cpp          # External dependency stubs
├── test_bf.cpp            # Brainfuck engine test
├── test_html_to_kdl.cpp   # HTML → KDL converter
├── test_ui_calculator.cpp # Calculator UI test (11 assertions)
├── test_ui_ids.cpp        # HTML ID test (7 assertions)
└── test_ui_styles.cpp     # CSS styles test (9 assertions)
```

## Console v3 Architecture

```
Console::exec(subsystem, cmd, args)
    │
    ├── Returns Result with optional binary payload
    │
    └── Transport layer handles delivery:
        ├── BleTransport: JSON→TX, binary→BIN_CHAR
        └── SerialTransport: text output, "[use BLE]" for binary
```

## Test Results

- test_calc: 11/11 ✓
- test_ids: 7/7 ✓
- test_styles: 9/9 ✓
- test_bf: 3/3 ✓

**Total: 30 assertions, ALL PASSED**
