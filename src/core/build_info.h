#pragma once

/**
 * BuildInfo - which build of the firmware this is.
 *
 * id(): the commit it is built from, "fc686e0", with "+" when built with uncommitted changes
 * (scripts/build_id.py, at each PlatformIO build). "dev" where PlatformIO did not build it:
 * the simulator, TelaCI.
 */
namespace BuildInfo {

const char* id();

} // namespace BuildInfo
