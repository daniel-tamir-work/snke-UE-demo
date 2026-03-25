/**
 * Stubs for editor/desktop compilation.
 * On Android the pre-built .so provides all symbols.
 */

#if !PLATFORM_ANDROID

#include "lib_marker_tracker_client.h"
#include <cstring>
#include <cmath>

int initializeClient(const char*) noexcept { return -1; }
int enableMarkerTracking() noexcept { return -1; }
int disableMarkerTracking() noexcept { return -1; }
int startMarkerTracking(int) noexcept { return -1; }
int stopMarkerTracking() noexcept { return -1; }
int getStatus() noexcept { return -1; }
void cleanupClient() noexcept {}
void freeMarkerStructuresInfo(MarkerStructuresInfo*) noexcept {}
int generalStart(const char*, int) noexcept { return 0; }
MarkerStructuresInfo streamMarkersDirectLegacy() noexcept { MarkerStructuresInfo r{}; return r; }

int streamMarkersDirect(MarkerStructuresInfo* m) noexcept
{
	if (!m) return 0;
	m->marker_structures = nullptr;
	m->marker_count = 0;
	m->timestamp_seconds = 0;
	m->timestamp_nanos = 0;
	m->general_error = 0;
	m->detected_ellipses = nullptr;
	m->ellipse_count = 0;
	return 0;
}

int transformMarkerPose(const MarkerTrackerPose*, TransformedMarkerPose* out) noexcept
{
	if (out) { out->valid = 0; }
	return 0;
}

#endif // !PLATFORM_ANDROID
