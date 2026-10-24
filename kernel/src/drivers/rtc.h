#pragma once
#include <stdint.h>

struct DateTime {
    int year, month, day, hour, minute, second;
    int weekday() const;   // 0 = domingo
};

namespace rtc {
// Hora local (aplica la zona horaria si el reloj del equipo guarda UTC)
DateTime now();
// Hora tal como está en el chip CMOS
DateTime hardware_now();
// Ajusta el reloj del equipo a una hora local dada
void set_local(const DateTime& local);
void configure(bool hardware_is_utc, int utc_offset_minutes);
bool hardware_is_utc();
int utc_offset();
DateTime add_minutes(DateTime t, int minutes);
int days_in_month(int year, int month);
// Formato FAT de fecha y hora
uint16_t fat_date(const DateTime& t);
uint16_t fat_time(const DateTime& t);
DateTime from_fat(uint16_t date, uint16_t time);
}

const char* weekday_name(int d);
const char* month_name(int m);
