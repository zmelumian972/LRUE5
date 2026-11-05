#pragma once

#include "CoreMinimal.h"

// The game's log: what it found, built and couldn't do.
DECLARE_LOG_CATEGORY_EXTERN(LogLRCore, Log, All);

// A table or file the core has already read (World::setup and the menus' load check them): a failure here means a broken
// cache, logged, and the drawing goes on without it.
inline void WarnUnless(bool bLoaded, const char* What)
{
	if (!bLoaded) {
		UE_LOG(LogLRCore, Warning, TEXT("%hs doesn't decode"), What);
	}
}
