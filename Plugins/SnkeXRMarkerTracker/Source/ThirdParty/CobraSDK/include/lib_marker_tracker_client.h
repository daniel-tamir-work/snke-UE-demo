#ifndef LIB_MARKER_TRACKER_CLIENT_H
#define LIB_MARKER_TRACKER_CLIENT_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Marker structure definitions
struct MarkerTrackerPose {
    float x;
    float y;
    float z;
    float qw;
    float qx;
    float qy;
    float qz;
};

struct MarkerStructureInfo {
    uint32_t marker_id;
    MarkerTrackerPose pose;
    float rms_position_error;
    int32_t error;
};

// Maximum number of markers supported (pre-allocated buffer size)
#define MAX_MARKERS 6

// 2D ellipse in image space (MARKER_ALGORITHM_MODE_2D / BOTH)
struct EllipseInfo {
    float center_x;
    float center_y;
    float major_radius;
    float minor_radius;
    float angle;
};

#define MAX_DETECTED_ELLIPSES 64

struct MarkerStructuresInfo {
    MarkerStructureInfo* marker_structures;
    int marker_count;
    int64_t timestamp_seconds;
    int32_t timestamp_nanos;
    int32_t general_error;
    // Pre-allocated buffer (caller should allocate MAX_MARKERS elements)
    MarkerStructureInfo marker_buffer[MAX_MARKERS];
    EllipseInfo* detected_ellipses;
    int ellipse_count;
    EllipseInfo ellipse_buffer[MAX_DETECTED_ELLIPSES];
};

// Transformed marker data structure (engine-agnostic)
struct TransformedMarkerPose {
    float position[3];      // Position in meters (x, y, z)
    float rotation[4];      // Rotation quaternion (x, y, z, w)
    int valid;              // 1 if transformation succeeded, 0 if failed
};

// Client API functions
// Return convention: 1 = success, 0 = failure for generalStart/streamMarkersDirect/transformMarkerPose;
// 0 = success, -1 = failure for initializeClient, enable/disable/start/stop tracking; getStatus returns status enum or -1 on error.

int initializeClient(const char* server_address) noexcept;  // 0 = success, -1 = failure
int enableMarkerTracking() noexcept;   // 0 = success, -1 = failure
// algorithm_mode: 0 = POSE, 1 = 2D ellipses, 2 = BOTH (see MarkerAlgorithmMode in API proto)
int startMarkerTracking(int algorithm_mode) noexcept;    // 0 = success, -1 = failure
int stopMarkerTracking() noexcept;    // 0 = success, -1 = failure
int disableMarkerTracking() noexcept; // 0 = success, -1 = failure
int getStatus() noexcept;             // MarkerTrackerStatus enum value, or -1 on error
int streamMarkers(MarkerStructuresInfo* markers_info) noexcept;
// Stream markers using pre-allocated buffer in markers_info
// Returns 1 on success, 0 on error, -1 if marker_count exceeds MAX_MARKERS
// markers_info must have marker_buffer pre-allocated (part of MarkerStructuresInfo structure)
int streamMarkersDirect(MarkerStructuresInfo* markers_info) noexcept;
// Deprecated: Use streamMarkersDirect with pre-allocated buffer instead
// This function is kept for backward compatibility but allocates memory each call
MarkerStructuresInfo streamMarkersDirectLegacy() noexcept;
void freeMarkerStructuresInfo(MarkerStructuresInfo* markers_info) noexcept;
void cleanupClient() noexcept;
// General start function that handles all connection/initialization/starting details
// Returns 1 if successful, 0 if failed
int generalStart(const char* server_address, int algorithm_mode = 0) noexcept;

// Utility function to transform marker pose from server format to engine format
// Converts marker pose (in mm) to transformed pose (in meters) with coordinate system conversion
// Returns 1 if transformation succeeded, 0 if failed (invalid input data)
int transformMarkerPose(const MarkerTrackerPose* input_pose, TransformedMarkerPose* output_pose) noexcept;

#ifdef __cplusplus
}
#endif

#endif // LIB_MARKER_TRACKER_CLIENT_H


