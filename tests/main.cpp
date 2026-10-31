#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <atomic>
#include <cstdlib>
#include <new>

// Compte les allocations, pour vérifier que le rendu n'en fait aucune.
std::atomic<long> g_allocations{0};

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
