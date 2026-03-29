#ifndef VODOMER_H
#define VODOMER_H

#pragma once
#include <Arduino.h>

// Konstantní textové hodnoty stavu
constexpr const char* VODOMER_STATE_AP           = "AP";
constexpr const char* VODOMER_STATE_DISCONNECTED = "Disconnected";
constexpr const char* VODOMER_STATE_RUNNING      = "Running";
constexpr const char* VODOMER_STATE_INIT         = "Init";

// Proměnná ukazující na aktuální stav (jednu z výše uvedených konstant)
extern const char* cVodomerState;

extern String cVodomerID;
extern bool cDisplayIsOn;

#endif // VODOMER_H
