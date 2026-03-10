#pragma once
#include <Arduino.h>

// Cek ke Firestore: collection "userCodes", document = code.
// Jika dokumen ada dan punya field "name", outName akan diisi,
// dan fungsi mengembalikan true. Kalau tidak ada / error, return false.
// bool firebase_lookupUserName(const String &code, String &outName);
bool firebase_lookupUserName(const String &code, String &outName, String &outUid);

void firebase_logWorkoutAsync(
    const String &firebaseUid, // sementara bisa pakai userInputID dulu
    const String &userInputId, // code yang user masukin
    uint32_t durationMs,       // durasi dalam ms
    const String &activity,    // "rest"/"walk"/"jog"
    float kcalPerMin           // nilai kcal/min terakhir dari IC
);
