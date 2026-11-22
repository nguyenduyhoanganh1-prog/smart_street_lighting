/**
 * @file web_server.h
 * @brief HTTP server of ai_stream: the page, the latest frame with its detections, lamp controls.
 *
 *   GET  /            the page (embedded index.html)
 *   GET  /frame.jpg   latest JPEG; headers X-Info and X-Det carry the numbers of that frame
 *   GET  /api/state   lamp levels, modes, timing, free heap (polled about once a second)
 *   POST /api/lamp    {"lamp":0..4,"pct":0..100}  (0 = all lamps; switches auto off)
 *   POST /api/auto    {"on":true|false}           lamps follow the detections
 *   POST /api/exposure {"lock":true|false}        freeze camera auto exposure/gain
 *   GET  /api/road    road, kerbs and zone cuts of the last calibration (pixels of the frame)
 *   POST /api/recalibrate                         find the road again (lamps at full ~2 s, then restored)
 *
 * Not a never-ending MJPEG stream: that loop would hold the only server task and block the
 * lamp controls. The page asks for the next frame itself.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
