Backup of the model that was deployed in ai_stream/components/edge_impulse before switching to v3 (96x96).
Model: FOMO int8, input 128x128 gray (squash), labels car/human, TFLite arena 314060 B.
Edge Impulse project TKHTN (1165012), Impulse #1, file dates 2026-11-15.
Restore: copy model-parameters/ and tflite-model/ from here back over ai_stream/components/edge_impulse/,
then set the static_assert in ai_stream/main/detector.cpp back to 128 x 128.
