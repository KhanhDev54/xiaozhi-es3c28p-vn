#ifndef KENDEV_ALARM_TONES_H
#define KENDEV_ALARM_TONES_H

// Built-in alarm tones (see tools/make_alarm_tones.py), Ogg Opus for Application::PlaySound

#include <cstddef>
#include <string_view>

int AlarmToneCount();
const char* AlarmToneName(int index);
std::string_view AlarmToneOgg(int index);

#endif  // KENDEV_ALARM_TONES_H
