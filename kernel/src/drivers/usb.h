#pragma once

namespace usb {
void init();
void poll();          // llamado desde el bucle principal
int device_count();
const char* describe(int i);
}
