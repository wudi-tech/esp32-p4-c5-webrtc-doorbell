/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Board name setting refer to `codec_board` README.md for more details
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_BOARD_NAME "WT99P4C5_S1"
#else
#define TEST_BOARD_NAME "S3_Korvo_V2"
#endif

/**
 * @brief  Video resolution settings
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH  640
#define VIDEO_HEIGHT 480
#define VIDEO_FPS    15
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
#endif

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
#if CONFIG_IDF_TARGET_ESP32P4
#define DETECT_WIDTH  320
#define DETECT_HEIGHT 180
#define DETECT_FPS    2
#else
#define DETECT_WIDTH  320
#define DETECT_HEIGHT 240
#define DETECT_FPS    1
#endif
#endif

/*
 * Keep local Wi-Fi credentials outside Git. Copy settings.local.h.example to
 * settings.local.h and edit the ignored copy before the first network test.
 */
#if __has_include("settings.local.h")
#include "settings.local.h"
#else
#define WIFI_SSID     "YOUR_2_4_GHZ_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (true)

#if CONFIG_IDF_TARGET_ESP32P4
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32P4-Fuction-Ev-Board, GPIO35(boot button) is connected RMII_TXD1
 *        When enable `NETWORK_USE_ETHERNET` will cause socket error
 *        User must replace it to a unused GPIO instead (like GPIO27)
 */
#define DOOR_BELL_RING_BUTTON  35
#else
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32S3-KORVO-V3 Use ADC button as ring button
 */
#define DOOR_BELL_RING_BUTTON  5
#endif

#ifdef __cplusplus
}
#endif
