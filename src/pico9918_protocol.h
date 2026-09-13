// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#define HB_LINE_SIZE 640
#define HB_REPLY_SIZE 1536
// Mutates the line in-place. Caller serializes hardware access.
size_t hb_command(char *line, char *reply, size_t capacity);
