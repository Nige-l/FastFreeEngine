# Build an ECS from Scratch

In this tutorial, you will build a minimal **Entity Component System** (ECS) in about 150 lines of C++. When you are done, you will have a working program that creates entities, attaches data to them, and runs systems that process that data every frame -- the same fundamental pattern that powers FastFreeEngine and most modern game engines.

No libraries required. No FFE headers. Just you, a C++20 compiler, and a single `.cpp` file.

---

## What You Will Build

A mini-ECS that can:

- Create and destroy entities (just integer IDs)
- Attach typed components (plain data structs) to entities
- Query for entities that have specific components
- Run systems (functions) that process components every frame

By the end, you will have a tiny game loop with a `Position` component, a `Velocity` component, and a movement system that updates positions every tick. It will print the results to the console so you can watch it work.

---

## Why ECS?

If you have written any object-oriented code, your first instinct for a game might be to create a class hierarchy:

```cpp
class GameObject { virtual void update(float dt) = 0; };
class Player : public GameObject { ... };
class Enemy : public GameObject { ... };
```

This works for small programs, but it falls apart in a real game engine. You end up with:

- **The diamond problem** -- what happens when `FlyingSwimmingEnemy` needs to inherit from both `FlyingEnemy` and `SwimmingEnemy`?
- **Blob objects** -- a single base class that carries data for every possible feature, even when most objects do not use it
- **Terrible performance** -- iterating over a `vector<GameObject*>` means chasing pointers scattered all over memory, which is the worst thing you can do for CPU cache performance

An ECS solves all three problems by splitting the world into three simple concepts:

- **Entities** are just numbers (IDs). No data, no behavior.
- **Components** are plain data structs attached to entities. No inheritance. No virtual functions.
- **Systems** are functions that process all entities with a specific set of components.

Behavior emerges from composition, not inheritance. Want an entity that can move and take damage? Give it a `Position`, a `Velocity`, and a `Health` component. No class hierarchy needed.

!!! tip "Want the full explanation?"
    The [How the Entity Component System Works](../internals/ecs.md) page goes much deeper into why ECS exists and how FFE's production implementation works. Read it after finishing this tutorial.

---

## Step 1: Entity IDs

An entity is just an integer. That is it. No class, no struct, no inheritance. An entity is a number that acts as a key into component storage.

We need a way to create new entities and recycle destroyed ones. The simplest approach: keep a counter and a free list.

```cpp
#include <cstdint>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <cassert>

// An entity is just a 32-bit integer.
using Entity = uint32_t;
static constexpr Entity NULL_ENTITY = UINT32_MAX;
```

Why integers instead of objects? Because integers are tiny (4 bytes), trivially copyable, and can be used as array indices. When you have 10,000 entities, keeping them as plain numbers means zero overhead per entity.

---

## Step 2: Component Storage

Components are plain data. A `Position` component is just an `x` and a `y`. A `Health` component is just `current` and `max`. No methods, no virtual functions, no inheritance.

The key insight: **all components of the same type are stored together in a contiguous container.** When a system needs to process all `Position` components, it walks straight through one container instead of chasing pointers to scattered objects.

We need a way to store any component type. Since C++ templates let us work with arbitrary types, we will build a type-erased base class and a templated derived class:

```cpp
// Base class for type-erased component storage.
// We need this so we can store different ComponentArray<T> types
// in a single container.
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void remove(Entity entity) = 0;
    virtual bool has(Entity entity) const = 0;
};

// Stores all components of a single type T.
// Uses an unordered_map for simplicity. A production ECS would use
// a dense array with an entity-to-index mapping for cache performance.
template <typename T>
class ComponentArray : public IComponentArray {
public:
    void insert(Entity entity, T component) {
        m_data[entity] = component;
    }

    T& get(Entity entity) {
        auto it = m_data.find(entity);
        assert(it != m_data.end() && "Entity does not have this component");
        return it->second;
    }

    bool has(Entity entity) const override {
        return m_data.find(entity) != m_data.end();
    }

    void remove(Entity entity) override {
        m_data.erase(entity);
    }

    // Returns a reference to the internal map so systems can iterate.
    std::unordered_map<Entity, T>& all() { return m_data; }

private:
    std::unordered_map<Entity, T> m_data;
};
```

!!! note "Why unordered_map?"
    In a real engine, you would use a **dense packed array** with a sparse-to-dense index mapping. This gives you cache-friendly iteration (all components are contiguous in memory). We are using `unordered_map` here because it is simpler to understand and good enough to learn the concepts. The [internals page](../internals/ecs.md) explains why dense arrays are faster.

---

## Step 3: The World

The `World` is the central manager. It handles entity creation and destruction, and it holds all the component storage arrays.

```cpp
class World {
public:
    // Create a new entity. Recycles destroyed IDs if available.
    Entity createEntity() {
        Entity id;
        if (!m_freeList.empty()) {
            id = m_freeList.back();
            m_freeList.pop_back();
        } else {
            id = m_nextId++;
        }
        m_alive.push_back(id);
        return id;
    }

    // Destroy an entity and remove all its components.
    void destroyEntity(Entity entity) {
        for (auto& [typeIdx, array] : m_componentArrays) {
            array->remove(entity);
        }
        // Remove from alive list
        for (auto it = m_alive.begin(); it != m_alive.end(); ++it) {
            if (*it == entity) {
                m_alive.erase(it);
                break;
            }
        }
        m_freeList.push_back(entity);
    }

    // Get or create the ComponentArray for type T.
    template <typename T>
    ComponentArray<T>& getComponentArray() {
        std::type_index typeIdx(typeid(T));
        if (m_componentArrays.find(typeIdx) == m_componentArrays.end()) {
            m_componentArrays[typeIdx] = std::make_unique<ComponentArray<T>>();
        }
        return *static_cast<ComponentArray<T>*>(m_componentArrays[typeIdx].get());
    }

    // Add a component to an entity.
    template <typename T>
    T& addComponent(Entity entity, T component) {
        auto& array = getComponentArray<T>();
        array.insert(entity, component);
        return array.get(entity);
    }

    // Get a component from an entity.
    template <typename T>
    T& getComponent(Entity entity) {
        return getComponentArray<T>().get(entity);
    }

    // Check if an entity has a component.
    template <typename T>
    bool hasComponent(Entity entity) {
        return getComponentArray<T>().has(entity);
    }

    // Get all living entities.
    const std::vector<Entity>& allEntities() const { return m_alive; }

private:
    Entity m_nextId = 0;
    std::vector<Entity> m_freeList;
    std::vector<Entity> m_alive;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentArray>> m_componentArrays;
};
```

A few things to notice:

- **`createEntity`** hands out the next available integer, or recycles one from the free list. This is the same concept as FFE's entity ID recycling (though FFE adds a generation counter to catch stale IDs).
- **`getComponentArray<T>`** uses `typeid` to look up the storage for a given component type. This is a cold-path operation -- you call it during setup, not every frame.
- **`destroyEntity`** removes the entity from all component arrays. In a production ECS, this would be more efficient (bit masks, deferred destruction), but the concept is the same.

---

## Step 4: Adding Components

Now we can define some game data as plain structs and attach them to entities:

```cpp
// Components are plain data. No methods needed.
struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Name {
    const char* value = "unnamed";
};
```

Using them is straightforward:

```cpp
World world;

Entity player = world.createEntity();
world.addComponent<Position>(player, {100.0f, 200.0f});
world.addComponent<Velocity>(player, {50.0f, -30.0f});
world.addComponent<Name>(player, {"Hero"});
```

That is it. No inheritance. No registration step. Just create an entity and stick data on it.

---

## Step 5: Querying Components

Systems need to find all entities that have a specific set of components. The simplest approach: iterate all entities and check which ones have the components you need.

We will add a helper method to `World` that does this:

```cpp
// Add this inside the World class:

// Call a function for every entity that has ALL the specified components.
template <typename... Components, typename Func>
void each(Func func) {
    for (Entity entity : m_alive) {
        // Check if this entity has ALL required components.
        bool hasAll = (hasComponent<Components>(entity) && ...);
        if (hasAll) {
            func(entity, getComponent<Components>(entity)...);
        }
    }
}
```

That `(hasComponent<Components>(entity) && ...)` is a **fold expression** -- a C++17 feature that expands the parameter pack and ANDs all the results together. If we call `world.each<Position, Velocity>(...)`, it checks that the entity has *both* `Position` *and* `Velocity`.

Now a system can iterate only the entities it cares about:

```cpp
world.each<Position, Velocity>([](Entity entity, Position& pos, Velocity& vel) {
    // This runs for every entity that has both Position and Velocity
    std::cout << "Entity " << entity << " is at ("
              << pos.x << ", " << pos.y << ")\n";
});
```

!!! info "How real engines do it faster"
    Our `each` function checks every entity in the world and filters. When you have 10,000 entities but only 100 have `Velocity`, you are wasting time checking 9,900 entities. Production ECS implementations like EnTT use **sparse sets** and **archetype tables** to skip directly to the entities that match, making iteration nearly zero-overhead. The concept is the same -- the data structures are just smarter.

---

## Step 6: Systems

A system is just a function that processes components. There is no `System` base class, no interface to implement. A system is a function that takes a `World` reference and a time delta, and does its job.

```cpp
// Move everything that has both Position and Velocity.
void movementSystem(World& world, float dt) {
    world.each<Position, Velocity>(
        [dt](Entity /*entity*/, Position& pos, Velocity& vel) {
            pos.x += vel.dx * dt;
            pos.y += vel.dy * dt;
        }
    );
}

// Print the position of everything that has both Position and Name.
void debugPrintSystem(World& world, float /*dt*/) {
    world.each<Position, Name>(
        [](Entity entity, Position& pos, Name& name) {
            std::cout << "  [" << name.value << "] (entity "
                      << entity << ") at ("
                      << pos.x << ", " << pos.y << ")\n";
        }
    );
}
```

Notice: `movementSystem` does not care whether an entity is a player, an enemy, or a bullet. If it has `Position` and `Velocity`, it moves. This is the power of ECS -- behavior emerges from data composition.

---

## Step 7: Putting It All Together

Here is the complete program. Copy this into a file called `mini_ecs.cpp`, compile it, and run it.

```cpp
// mini_ecs.cpp -- A minimal ECS in ~150 lines
// Compile: g++ -std=c++20 -o mini_ecs mini_ecs.cpp
//     or:  clang++ -std=c++20 -o mini_ecs mini_ecs.cpp

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// Entity: just an integer
// -----------------------------------------------------------------------
using Entity = uint32_t;
static constexpr Entity NULL_ENTITY = UINT32_MAX;

// -----------------------------------------------------------------------
// Component storage (type-erased base + templated concrete class)
// -----------------------------------------------------------------------
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void remove(Entity entity) = 0;
    virtual bool has(Entity entity) const = 0;
};

template <typename T>
class ComponentArray : public IComponentArray {
public:
    void insert(Entity entity, T component) {
        m_data[entity] = component;
    }

    T& get(Entity entity) {
        auto it = m_data.find(entity);
        assert(it != m_data.end() && "Entity does not have this component");
        return it->second;
    }

    bool has(Entity entity) const override {
        return m_data.find(entity) != m_data.end();
    }

    void remove(Entity entity) override {
        m_data.erase(entity);
    }

private:
    std::unordered_map<Entity, T> m_data;
};

// -----------------------------------------------------------------------
// World: manages entities and component storage
// -----------------------------------------------------------------------
class World {
public:
    Entity createEntity() {
        Entity id;
        if (!m_freeList.empty()) {
            id = m_freeList.back();
            m_freeList.pop_back();
        } else {
            id = m_nextId++;
        }
        m_alive.push_back(id);
        return id;
    }

    void destroyEntity(Entity entity) {
        for (auto& [typeIdx, array] : m_componentArrays) {
            array->remove(entity);
        }
        for (auto it = m_alive.begin(); it != m_alive.end(); ++it) {
            if (*it == entity) {
                m_alive.erase(it);
                break;
            }
        }
        m_freeList.push_back(entity);
    }

    template <typename T>
    ComponentArray<T>& getComponentArray() {
        std::type_index typeIdx(typeid(T));
        if (m_componentArrays.find(typeIdx) == m_componentArrays.end()) {
            m_componentArrays[typeIdx] = std::make_unique<ComponentArray<T>>();
        }
        return *static_cast<ComponentArray<T>*>(m_componentArrays[typeIdx].get());
    }

    template <typename T>
    T& addComponent(Entity entity, T component) {
        auto& array = getComponentArray<T>();
        array.insert(entity, component);
        return array.get(entity);
    }

    template <typename T>
    T& getComponent(Entity entity) {
        return getComponentArray<T>().get(entity);
    }

    template <typename T>
    bool hasComponent(Entity entity) {
        return getComponentArray<T>().has(entity);
    }

    template <typename... Components, typename Func>
    void each(Func func) {
        for (Entity entity : m_alive) {
            bool hasAll = (hasComponent<Components>(entity) && ...);
            if (hasAll) {
                func(entity, getComponent<Components>(entity)...);
            }
        }
    }

    const std::vector<Entity>& allEntities() const { return m_alive; }

private:
    Entity m_nextId = 0;
    std::vector<Entity> m_freeList;
    std::vector<Entity> m_alive;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentArray>>
        m_componentArrays;
};

// -----------------------------------------------------------------------
// Components: plain data structs
// -----------------------------------------------------------------------
struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Name {
    const char* value = "unnamed";
};

// -----------------------------------------------------------------------
// Systems: plain functions
// -----------------------------------------------------------------------
void movementSystem(World& world, float dt) {
    world.each<Position, Velocity>(
        [dt](Entity /*entity*/, Position& pos, Velocity& vel) {
            pos.x += vel.dx * dt;
            pos.y += vel.dy * dt;
        }
    );
}

void debugPrintSystem(World& world, float /*dt*/) {
    world.each<Position, Name>(
        [](Entity /*entity*/, Position& pos, Name& name) {
            std::cout << "  [" << name.value << "] at ("
                      << pos.x << ", " << pos.y << ")\n";
        }
    );
}

// -----------------------------------------------------------------------
// Main: set up a world and simulate a few frames
// -----------------------------------------------------------------------
int main() {
    World world;

    // Create some entities with different component combinations.
    Entity player = world.createEntity();
    world.addComponent<Position>(player, {0.0f, 0.0f});
    world.addComponent<Velocity>(player, {60.0f, 30.0f});
    world.addComponent<Name>(player, {"Hero"});

    Entity enemy = world.createEntity();
    world.addComponent<Position>(enemy, {500.0f, 300.0f});
    world.addComponent<Velocity>(enemy, {-20.0f, 10.0f});
    world.addComponent<Name>(enemy, {"Goblin"});

    // This entity has Position and Name but no Velocity.
    // movementSystem will skip it. debugPrintSystem will show it.
    Entity tree = world.createEntity();
    world.addComponent<Position>(tree, {200.0f, 100.0f});
    world.addComponent<Name>(tree, {"Oak Tree"});

    // Simulate 5 frames at 60 fps (dt = 1/60 second).
    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 5; ++frame) {
        std::cout << "--- Frame " << frame << " ---\n";
        movementSystem(world, dt);
        debugPrintSystem(world, dt);
    }

    // Demonstrate entity destruction and recycling.
    std::cout << "\n--- Destroying Goblin (entity " << enemy << ") ---\n";
    world.destroyEntity(enemy);

    Entity bullet = world.createEntity();
    std::cout << "Created Bullet as entity " << bullet
              << " (recycled ID!)\n";
    world.addComponent<Position>(bullet, {0.0f, 0.0f});
    world.addComponent<Velocity>(bullet, {500.0f, 0.0f});
    world.addComponent<Name>(bullet, {"Bullet"});

    std::cout << "\n--- Frame 5 (after destruction + recycling) ---\n";
    movementSystem(world, dt);
    debugPrintSystem(world, dt);

    return 0;
}
```

To compile and run:

=== "GCC"

    ```bash
    g++ -std=c++20 -o mini_ecs mini_ecs.cpp && ./mini_ecs
    ```

=== "Clang"

    ```bash
    clang++ -std=c++20 -o mini_ecs mini_ecs.cpp && ./mini_ecs
    ```

=== "MSVC"

    ```bash
    cl /std:c++20 mini_ecs.cpp && mini_ecs.exe
    ```

You should see output like this:

```
--- Frame 0 ---
  [Hero] at (1, 0.5)
  [Goblin] at (499.667, 300.167)
  [Oak Tree] at (200, 100)
--- Frame 1 ---
  [Hero] at (2, 1)
  [Goblin] at (499.333, 300.333)
  [Oak Tree] at (200, 100)
...
--- Destroying Goblin (entity 1) ---
Created Bullet as entity 1 (recycled ID!)

--- Frame 5 (after destruction + recycling) ---
  [Hero] at (6, 3)
  [Oak Tree] at (200, 100)
  [Bullet] at (8.33333, 0)
```

Notice: the Oak Tree never moves (it has no `Velocity`), the Goblin disappears after destruction, and the Bullet gets entity ID 1 -- the recycled ID from the destroyed Goblin.

---

## How FFE Does It

Your mini-ECS and FFE's ECS are based on the same ideas, but the production version is significantly more optimized:

| Concept | Your Mini-ECS | FFE (EnTT) |
|---------|--------------|-------------|
| Entity ID | Plain `uint32_t` | 32-bit with 12-bit generation counter to detect stale IDs |
| Component storage | `unordered_map<Entity, T>` | Sparse set with dense packed array -- cache-friendly iteration |
| Querying | Iterate all entities, check each | Views that skip directly to matching entities -- O(n) in matches, not O(n) in total entities |
| Systems | Any callable (lambda, function pointer) | Function pointers only (`void(*)(World&, float)`) -- no `std::function`, no hidden heap allocation |
| Destruction | Immediate, removes from all maps | Deferred until end of frame to avoid iterator invalidation |

The core idea is identical: entities are IDs, components are data, systems are functions. The differences are all about performance -- making iteration faster, memory access more predictable, and allocation cheaper.

For the full production API, see the [Core API Reference](../api/core.md). For a deep dive into why the memory layout matters, read [How the Entity Component System Works](../internals/ecs.md).

---

## Challenges

Now that you have a working ECS, try extending it. These are roughly in order of difficulty.

### 1. Component Removal

Add a `removeComponent<T>(entity)` method to `World`. Make sure it does nothing gracefully if the entity does not have that component.

### 2. Entity Count and Has-Entity Check

Add `entityCount()` and `isAlive(Entity)` methods. The alive check should be O(1) -- hint: use a set or a boolean array instead of searching the vector.

### 3. Generation Counters

Right now, if you destroy entity 1 and create a new entity, you get entity 1 back. Code that saved the old entity 1 has no way to know it is gone. Fix this by splitting the 32-bit ID into an index (lower bits) and a generation counter (upper bits). Increment the generation when a slot is recycled. `isAlive` checks the generation.

### 4. Dense Packed Storage

Replace the `unordered_map` in `ComponentArray` with a dense array and a sparse-to-dense index map. This is how real ECS implementations store components for cache-friendly iteration. You will need:

- A dense `std::vector<T>` where components are packed with no gaps
- A dense `std::vector<Entity>` that maps dense index back to entity
- A sparse `std::vector<uint32_t>` where `sparse[entity]` gives you the dense index

When you remove a component, swap the last element into the gap to keep the dense array packed.

### 5. Multi-Component Views

Build a `View<A, B>` object that holds references to the `ComponentArray<A>` and `ComponentArray<B>` and iterates the smaller one, checking the other for matches. This is how EnTT makes iteration fast: always iterate the smallest set.

---

Congratulations -- you just built a game engine subsystem from scratch. The next installment, **Build a Sprite Renderer from Scratch**, will teach you how to turn this entity data into pixels on screen. Stay tuned.
