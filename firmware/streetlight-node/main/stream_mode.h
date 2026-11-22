/**
 * @file stream_mode.h
 * @brief ai-stream mode of streetlight-node: the ai_stream program (ported), entered by a double click on IO0.
 *
 *   WiFi "StreetLight-AI" (tune_config.h) -> http://192.168.4.1 -> live frame, FOMO boxes, 4 lamp sliders.
 *
 * Nothing of the normal node runs here: no MQTT, no BH1750/INA219, no energy bookkeeping, no lighting
 * rules. Only the camera, the model, the lamps and the web page.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Init camera, lamps, model, pipeline, access point and web server. Returns; the tasks keep running. */
void stream_mode_run(void);

#ifdef __cplusplus
}
#endif
