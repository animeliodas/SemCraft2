/* SemCraft 2 - the plugin's heap is the engine's.
 *
 * The plugin has its own C runtime, the engine another; a block allocated by one and freed by the other corrupts the
 * heap. Building brushes hands arrays back and forth (we fill a CObject3D, the engine optimizes and frees it), so every
 * new/delete here goes through the engine's AllocMemory/FreeMemory: memory either side frees came from the same heap.
 */
#include "StdH.h"

#include <new>

void *operator new(size_t ctBytes) {
  return AllocMemory((SLONG)(ctBytes > 0 ? ctBytes : 1));
}

void *operator new[](size_t ctBytes) {
  return AllocMemory((SLONG)(ctBytes > 0 ? ctBytes : 1));
}

void *operator new(size_t ctBytes, const std::nothrow_t &) noexcept {
  return AllocMemory((SLONG)(ctBytes > 0 ? ctBytes : 1));
}

void *operator new[](size_t ctBytes, const std::nothrow_t &) noexcept {
  return AllocMemory((SLONG)(ctBytes > 0 ? ctBytes : 1));
}

void operator delete(void *p) noexcept {
  if (p != NULL) FreeMemory(p);
}

void operator delete[](void *p) noexcept {
  if (p != NULL) FreeMemory(p);
}

void operator delete(void *p, size_t) noexcept {
  if (p != NULL) FreeMemory(p);
}

void operator delete[](void *p, size_t) noexcept {
  if (p != NULL) FreeMemory(p);
}
