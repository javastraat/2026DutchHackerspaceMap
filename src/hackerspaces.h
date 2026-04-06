#pragma once

typedef struct {
  int ledNumber;
  const char *name;
  const char *url;
} HackerspaceEntry;

extern const HackerspaceEntry hackerspaces[];
extern const int HACKERSPACE_COUNT;
