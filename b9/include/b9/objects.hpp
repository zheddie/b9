#if !defined(B9_OBJECTS_HPP_)
#define B9_OBJECTS_HPP_

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>

//
// Context and Byte Allocator
//

namespace b9 {

class Cell;
class Map;
class MapMap;
class EmptyObjectMap;
class ObjectMap;
class Object;

using Id = std::uint32_t;

struct Allocator {};

struct Context {
 public:
  Allocator& allocator() { return allocator_; }

 private:
  Allocator allocator_;
  MapMap* mapMap_;
  EmptyObjectMap* emptyObjectMap_;
};

}  // namespace b9

/// Can GC
void* operator new(std::size_t size, b9::Context& cx) {
  std::cerr << "> allocating: " << size << "B\n";
  return malloc(size);  // TODO
}

//
// Maps maps maps
//

namespace b9 {

class Map;

class Cell {
 public:
  constexpr Cell(Map* map) : map_(map) {}

  constexpr Map* map() const { return map_; }

  Cell& map(Map* m) {
    map_ = m;
    return *this;
  }

 private:
  Map* map_;
};

class MapMap;

enum class MapKind { OBJECT_MAP, MAP_MAP, EMPTY_OBJECT_MAP };

struct Map : public Cell {
 public:
  Map(MapMap* map, MapKind kind) noexcept;

  MapMap* mapMap() const;

  MapKind kind() const { return kind_; }

 private:
  MapKind kind_;
};

class MapMap : public Map {
 public:
  MapMap() : Map(this, MapKind::MAP_MAP) {}
};

inline Map::Map(MapMap* map, MapKind kind) noexcept : Cell(map), kind_(kind) {}

MapMap* Map::mapMap() const { return (MapMap*)map(); }

class EmptyObjectMap : public Map {
 public:
  EmptyObjectMap(MapMap* map) : Map(map, MapKind::EMPTY_OBJECT_MAP) {}
};

using Index = std::uint8_t;

class ObjectMap : public Map {
 public:
  ObjectMap(EmptyObjectMap* parent, Id id)
      : Map(parent->mapMap(), MapKind::OBJECT_MAP),
        parent_(parent),
        id_(id),
        index_(0) {}

  ObjectMap(ObjectMap* parent, Id id)
      : Map(parent->mapMap(), MapKind::OBJECT_MAP),
        parent_(parent),
        id_(id),
        index_(parent->index() + 1) {}

  constexpr Map* parent() const noexcept { return parent_; }

  constexpr Id id() const noexcept { return id_; }

  Map& id(Id id) noexcept {
    id_ = id;
    return *this;
  }

  constexpr Index index() const noexcept { return index_; }

  Map& index(Index index) noexcept {
    index_ = index;
    return *this;
  }

 private:
  Map* parent_;
  // MapTable* children_;
  Id id_;
  Index index_;
};

using Value = std::int32_t;

class Object : public Cell {
 public:
  Object(ObjectMap* map) : Cell(map) {
    memset(slots_, 0, MAX_SLOTS * sizeof(Value));
  }

  Object(EmptyObjectMap* map) : Cell(map) {
    memset(slots_, 0, MAX_SLOTS * sizeof(Value));
  }

  Object(Object& other) : Cell(other.map()) {
    memcpy(slots_, other.slots_, MAX_SLOTS * sizeof(Value));
  }

  Value* slots() { return slots_; }

  const Value* slots() const { return slots_; }

  /// Returns {index, true} on success, or {0, false} on failure.
  /// Note that {0, true} is the first slot in the object.
  std::pair<Index, bool> index(Id id) {
    for (auto m = map(); m->kind() != MapKind::EMPTY_OBJECT_MAP;) {
      // assert(m->kind() == MapKind::OBJECT_MAP);
      auto om = (ObjectMap*)m;
      if (om->id() == id) {
        return {om->index(), true};
      }
      m = om->parent();
    }
    return std::make_pair(0, false);
  }

  std::pair<Value, bool> get(Context& cx, Id id) {
    auto lookup = index(id);
    if (lookup.second) {
      Value value = slots_[lookup.first];
      return std::make_pair(value, true);
    } else {
      return std::make_pair(0, false);
    }
  }

  /// Set the slot that corresponds to the id. If the slot doesn't exist,
  /// allocate the slot and assign it. The result is the address of the slot.
  /// !CAN_GC!
  Index set(Context& cx, Id id, Value value) {
    auto lookup = index(id);
    if (std::get<bool>(lookup)) {
      auto index = std::get<Index>(lookup);
      slots_[index] = value;
      return index;
    } else {
      auto index = newSlot(cx, id);
      slots_[index] = value;
      return index;
    }
  }

  /// Allocate a new slot corresponding to the id. The object may not already
  /// have a slot with this Id matching. !CAN_GC!
  std::size_t newSlot(Context& cx, Id id) {
    ObjectMap* m;
    switch (map()->kind()) {
      case MapKind::EMPTY_OBJECT_MAP:
        m = new (cx) ObjectMap((EmptyObjectMap*)map(), id);
        break;
      case MapKind::OBJECT_MAP:
        m = new (cx) ObjectMap((ObjectMap*)map(), id);
        break;
      default:
        throw std::runtime_error(
            "An object has a map that is neither an ObjectMap nor "
            "EmptyObjectMap");
        m = nullptr;
        break;
    }
    map(m);
    return m->index();
  }

 private:
  static constexpr std::size_t MAX_SLOTS = 32;

  Value slots_[MAX_SLOTS];
};

//
// ID Generation and Mapping
//

class IdGenerator {
 public:
  Id newId() { return nextId_++; }

 private:
  Id nextId_ = 0;
};

class SymbolTable {
 public:
  Id lookup(std::string& string) {
    auto it = lookupTable_.find(string);
    if (it != lookupTable_.end()) {
      return it->second;
    } else {
      auto id = idGenerator_.newId();
      lookupTable_.insert({string, id});
      return id;
    }
  }

 private:
  IdGenerator idGenerator_;
  std::map<std::string, Id> lookupTable_;
};

//
// Misc Object allocators
//

#if 0
  newObjectMap(Context& cx, ObjectMap* parent = nullptr) {
    return new (cx->allocator()) ObjectMap(cx->mapMap(), parent);
  }

  newObject(Context& cx, ObjectMap* map) { return new (cx) Object(map); }

  newObject(Context& cx, Object* object) { return new (cx) Object(*object); }
#endif

}  // namespace b9

#endif  // B9_OBJECTS_HPP_