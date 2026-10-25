// Device protocol v1: one JSON object per line, "t" is the command.
//
// Daemon -> sphere
//   {"t":"expression","name":"thinking","intensity":0.8}   intensity is optional, 0..1
//   {"t":"gaze","x":0.3,"y":-0.2}                          -1..1
//   {"t":"blink"}
//   {"t":"scene","name":"rain","mood":"grumpy","label":"62° · rain 3pm","duration_ms":4000}
//   {"t":"card","icon":"mail","label":"Demo → Fri","duration_ms":4000}
//   {"t":"card","avatar":true,"label":"Sarah J.","duration_ms":4000}   avatar = last image sent
//   {"t":"listening","on":true}
//   binary frame: avatar image, 120x120 RGB565 little-endian (28800 bytes)
//
// Sphere -> daemon
//   {"t":"hello","v":1,"firmware":"0.1","display":360,"caps":["mic"]}
//   binary frames: mic audio, 16 kHz mono 16-bit PCM, sent only while listening
#pragma once

void protocol_handle_line(const char *line);
