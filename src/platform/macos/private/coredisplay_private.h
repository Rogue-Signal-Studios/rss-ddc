#ifndef RSS_DDC_COREDISPLAY_PRIVATE_H
#define RSS_DDC_COREDISPLAY_PRIVATE_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

/* Returned dictionary follows Create ownership and is released by discovery.m. */
extern CFDictionaryRef CoreDisplay_DisplayCreateInfoDictionary(CGDirectDisplayID display);

#endif
