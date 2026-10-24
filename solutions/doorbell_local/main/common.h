/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"
#include "esp_capture_sink.h"
#include "doorbell_ring_policy.h"
#include "vision_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize for board
 */
void init_board(void);

/**
 * @brief  Initialize WebRTC synchronization before starting callbacks/tasks
 */
int doorbell_webrtc_init(void);

/**
 * @brief  Start WebRTC
 *
 * @param[in]  url  Signaling URL
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int start_webrtc(char *url);

/**
 * @brief  Query WebRTC status
 */
void query_webrtc(void);

/**
 * @brief  Stop WebRTC
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to stop
 */
int stop_webrtc(void);

/**
 * @brief  Send command to peer
 */
void send_cmd(char *cmd);

/**
 * @brief  Request a ring without disturbing an active or unavailable call
 */
doorbell_ring_request_result_t doorbell_request_ring(const char *source);

/**
 * @brief Report one camera pedestrian detection to the sensor-fusion layer.
 */
void doorbell_report_vision_detection(void);

/**
 * @brief Report the latest semantic camera result and browser snapshot.
 */
void doorbell_report_vision_snapshot(const vision_snapshot_t *snapshot);

/**
 * @brief  Close data channel according channel index
 */
int close_data_channel(int idx);

/**
 * @brief  Get capture sink handle by index
 */
esp_capture_sink_handle_t get_detect_sink(void);

/**
 * @brief  pedestrian detection configuration
 */
typedef struct {
   int                       (*detected)(esp_capture_rgn_t *rgn, void *ctx);
   void                      (*snapshot)(const vision_snapshot_t *snapshot,
                                         void *ctx);
   void                      *ctx;
} pedestrian_detect_cfg_t;

/**
 * @brief  Start pedestrian detection
 */
int start_pedestrian_detection(pedestrian_detect_cfg_t *cfg);

/**
 * @brief  Stop pedestrian detection
 */
void stop_pedestrian_detection();

/**
 * @brief Tell the vision scheduler whether radar currently sees the door zone.
 */
void set_vision_radar_focus(bool focused);


#ifdef __cplusplus
}
#endif
