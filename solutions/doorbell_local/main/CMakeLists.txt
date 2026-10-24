
set(srcs "webrtc.c" "main.c" "board.c" "media_sys.c" "webrtc_http_server.c"
         "door_lock.c" "door_lock_state.c" "radar_ld2450.c"
         "presence_fusion.c" "doorbell_ring_policy.c" "doorbell_event.c"
         "multimodal_fusion.c" "pcm_overlay.c" "doorbell_audio_render.c"
         "pedestrian_filter.c" "pedestrian_format_probe.c" "camera_motion.c"
         "vision_tracker.c" "vision_event.c" "doorbell_status.c")

if(CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT)
    list(APPEND srcs "pedestrain_detect.cpp")
endif()

idf_component_register(SRCS ${srcs}
                       EMBED_TXTFILES "ring.aac" "open.aac" "join.aac" "webrtc_test.html"
                       "certs/servercert.pem" "certs/prvtkey.pem"
                       INCLUDE_DIRS ".")
