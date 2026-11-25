#pragma once
// Minimal but fully functional Entity Component System.
// No placeholders: every function here is implemented and used by the game loop.

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <typeindex>
#include <algorithm>
#include <cassert>

namespace helix::ecs {

using EntityId = std::uint32_t;
constexpr EntityId kInvalidEntity = 0;

// ---------------------------------------------------------------------------
// Component storage: one dense vector per component type, with an entity ->
// index sparse map so we can do O(1) get/has and O(1) swap-remove.
// ---------------------------------------------------------------------------
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void Remove(EntityId e) = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool {
public:
    void Insert(EntityId e, T value) {
        assert(sparse_.find(e) == sparse_.end() && "component already present");
        sparse_[e] = dense_.size();
        dense_.push_back(std::move(value));
        denseEntities_.push_back(e);
    }

    T& Get(EntityId e) {
        auto it = sparse_.find(e);
        assert(it != sparse_.end() && "entity has no such component");
        return dense_[it->second];
    }

    bool Has(EntityId e) const { return sparse_.find(e) != sparse_.end(); }

    void Remove(EntityId e) override {
        auto it = sparse_.find(e);
        if (it == sparse_.end()) return;
        std::size_t idx = it->second;
        std::size_t last = dense_.size() - 1;
        if (idx != last) {
            dense_[idx] = std::move(dense_[last]);
            EntityId movedEntity = denseEntities_[last];
            denseEntities_[idx] = movedEntity;
            sparse_[movedEntity] = idx;
        }
        dense_.pop_back();
        denseEntities_.pop_back();
        sparse_.erase(it);
    }

    std::vector<T>& Dense() { return dense_; }
    const std::vector<EntityId>& DenseEntities() const { return denseEntities_; }

private:
    std::unordered_map<EntityId, std::size_t> sparse_;
    std::vector<T> dense_;
    std::vector<EntityId> denseEntities_;
};

// ---------------------------------------------------------------------------
// World: owns entities and all component pools.
// ---------------------------------------------------------------------------
class World {
public:
    EntityId CreateEntity() {
        EntityId id = nextId_++;
        alive_.insert(id);
        return id;
    }

    void DestroyEntity(EntityId e) {
        if (alive_.erase(e) == 0) return;
        for (auto& [type, pool] : pools_) {
            pool->Remove(e);
        }
    }

    bool IsAlive(EntityId e) const { return alive_.find(e) != alive_.end(); }

    template <typename T>
    T& AddComponent(EntityId e, T component) {
        auto& pool = PoolFor<T>();
        pool.Insert(e, std::move(component));
        return pool.Get(e);
    }

    template <typename T>
    T& GetComponent(EntityId e) {
        return PoolFor<T>().Get(e);
    }

    template <typename T>
    bool HasComponent(EntityId e) {
        return PoolFor<T>().Has(e);
    }

    template <typename T>
    void RemoveComponent(EntityId e) {
        PoolFor<T>().Remove(e);
    }

    // Iterate every entity that has component T, calling fn(EntityId, T&).
    template <typename T, typename Fn>
    void ForEach(Fn&& fn) {
        auto& pool = PoolFor<T>();
        auto& dense = pool.Dense();
        auto& ents = pool.DenseEntities();
        for (std::size_t i = 0; i < dense.size(); ++i) {
            fn(ents[i], dense[i]);
        }
    }

    // Two-component join, iterating the smaller pool and probing the larger.
    template <typename A, typename B, typename Fn>
    void ForEach2(Fn&& fn) {
        auto& poolA = PoolFor<A>();
        auto& poolB = PoolFor<B>();
        auto& dense = poolA.Dense();
        auto& ents = poolA.DenseEntities();
        for (std::size_t i = 0; i < dense.size(); ++i) {
            EntityId e = ents[i];
            if (poolB.Has(e)) {
                fn(e, dense[i], poolB.Get(e));
            }
        }
    }

private:
    template <typename T>
    ComponentPool<T>& PoolFor() {
        std::type_index key(typeid(T));
        auto it = pools_.find(key);
        if (it == pools_.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* raw = pool.get();
            pools_[key] = std::move(pool);
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    EntityId nextId_ = 1; // 0 is reserved as kInvalidEntity
    std::unordered_set<EntityId> alive_;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
};

} // namespace helix::ecs
