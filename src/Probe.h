#pragma once
#include <string>

unsigned long WindowsBuild();
bool MidiServiceRunning();
std::wstring FileVersion(const std::wstring& path);
std::wstring SystemFile(const wchar_t* name);
int RunNativeProbe();
