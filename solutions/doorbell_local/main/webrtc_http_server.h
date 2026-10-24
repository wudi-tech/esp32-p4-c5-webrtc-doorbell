/* WebRTC HTTPS server signaling

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_peer_signaling.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Get the HTTP signaling implementation
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  HTTPS server signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_http_impl(void);

/**
 * @brief Queue one preformatted JSON event for the browser SSE stream.
 *
 * The event is copied into a bounded telemetry queue. When the queue is full,
 * the oldest telemetry item is discarded so WebRTC signaling is never blocked.
 */
bool webrtc_http_server_publish_event(const char *json);

/**
 * @brief Replace the latest high-rate vision snapshot for the browser.
 *
 * Unlike semantic events, snapshots are coalesced and never consume the
 * bounded event-history queue. A newly connected SSE client receives the
 * current snapshot once.
 */
bool webrtc_http_server_publish_latest_vision(const char *json);

#ifdef __cplusplus
}
#endif
