#include "internal.h"
#include <stdlib.h>

static size_t hash(BSValue key) {
    key ^= key >> 16; key *= (uintptr_t)0x45d9f3bu; key ^= key >> 16;
    return (size_t)key;
}

BSMapEntry *bs_map_get(BSMap *map, BSValue key, int create) {
    if (!key) return NULL;
    if (create && (!map->capacity || map->count >= map->capacity / 2)) {
        size_t capacity = map->capacity ? map->capacity * 2 : 64;
        if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(BSMapEntry)) return NULL;
        BSMapEntry *entries = calloc(capacity, sizeof(*entries));
        if (!entries) return NULL;
        for (size_t i = 0; i < map->capacity; ++i) if (map->entries[i].key) {
            size_t j = hash(map->entries[i].key) & (capacity - 1);
            while (entries[j].key) j = (j + 1) & (capacity - 1);
            entries[j] = map->entries[i];
        }
        free(map->entries); map->entries = entries; map->capacity = capacity;
    }
    if (!map->capacity) return NULL;
    size_t i = hash(key) & (map->capacity - 1);
    while (map->entries[i].key && map->entries[i].key != key) i = (i + 1) & (map->capacity - 1);
    if (!map->entries[i].key) {
        if (!create) return NULL;
        map->entries[i].key = key; ++map->count;
    }
    return &map->entries[i];
}

void bs_map_dispose(BSMap *map) { free(map->entries); *map = (BSMap){0}; }
