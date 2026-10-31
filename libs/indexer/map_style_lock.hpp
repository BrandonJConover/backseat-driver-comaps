#pragma once

#include <shared_mutex>

/// Tile readers look up a feature's drawing rules in the classificator and then in the rules of the
/// current style, which must be the same style. Readers hold this shared; the style switch takes it
/// exclusively, for just the moment the current style changes.
inline std::shared_mutex & MapStyleSwitchMutex()
{
  static std::shared_mutex mutex;
  return mutex;
}
