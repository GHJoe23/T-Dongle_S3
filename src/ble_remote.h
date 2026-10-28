#pragma once
#include <stdint.h>

typedef void (*BleLineCallback)(const char *line, void *ctx);

void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx);
void blePoll();
void bleSend(const char *msg);
void bleUpdatePasskey(uint32_t passkey);
bool bleConnected();
