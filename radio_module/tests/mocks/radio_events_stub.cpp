// Mock for the LogosAPI surface radio_module touches, so Tier-1 tests link without the
// real platform. Issue #5 expands this to fake `delivery_module` (subscribe + messageReceived)
// and Issue #4 to fake the MediaMTX HTTP responses.
//
// Scaffold: empty translation unit reserved for the stubs. Keeping the file in the build now
// so tests/CMakeLists.txt is valid from #1 and later issues only add to it.
