//
// Created by Maguire Krist on 8/6/25.
//

#ifndef SPARE_SET_H
#define SPARE_SET_H
#include <vector>

namespace dev_collections
{
    template <typename T>
    class sparse_set
    {
        struct Slot
        {
            int dense_index;
            int generation;
        };

        std::vector<T> m_data{};
        std::vector<Slot> m_sparseTable{};
        std::vector<int> m_freeList{};
        std::vector<int> dense_to_id{};

        [[nodiscard]] bool valid_id(int id) const noexcept
        {
            return id >= 0 && id < m_sparseTable.size();
        }
    public:
        struct Handle
        {
            int id;
            int generation;
        };

        T* get(Handle h);
        const T* get(Handle h) const;
        Handle insert(T ele);
        bool remove(Handle h);

        [[nodiscard]] bool live(Handle h) const noexcept
        {
            return valid_id(h.id) && m_sparseTable[h.id].generation == h.generation && m_sparseTable[h.id].dense_index != -1;
        };

        void clear();
        const std::vector<T>& data() const noexcept { return m_data; }
    };

    template <typename T>
    T* sparse_set<T>::get(Handle h)
    {
        auto slot = m_sparseTable[h.id];
        if (slot.generation != h.generation)
        {
            return nullptr;
        }

        return &m_data[slot.dense_index];
    }

    template <typename T>
    const T* sparse_set<T>::get(Handle h) const
    {
        auto slot = m_sparseTable[h.id];
        if (slot.generation != h.generation)
        {
            return nullptr;
        }

        return &m_data[slot.dense_index];
    }

    template <typename T>
    typename sparse_set<T>::Handle sparse_set<T>::insert(T ele)
    {
        int id{0};
        if (!m_freeList.empty())
        {
            id = m_freeList.back();
            m_freeList.pop_back();
        }
        else
        {
            id = static_cast<int>(m_sparseTable.size());
            m_sparseTable.push_back(Slot{ -1, 0 });
        }

        int dense_index = static_cast<int>(m_data.size());
        m_data.push_back(std::move(ele));
        dense_to_id.push_back(id);

        m_sparseTable[id].dense_index = dense_index;

        return Handle { .id = id, .generation = m_sparseTable[id].generation };
    }

    template <typename T>
    bool sparse_set<T>::remove(Handle h)
    {
        if (!live(h)) { return false; }

        auto& slot = m_sparseTable[h.id];
        auto dense_index = slot.dense_index;
        auto last = static_cast<int>(m_data.size() - 1);

        if (dense_index != last)
        {
            auto movedId = dense_to_id[last];
            m_data[dense_index] = std::move(m_data[last]);
            dense_to_id[dense_index] = movedId;
            m_sparseTable[movedId].dense_index = dense_index;
        }
        m_data.pop_back();
        dense_to_id.pop_back();

        slot.dense_index = -1;
        ++slot.generation;

        m_freeList.push_back(h.id);
        return true;
    }

    template <typename T>
    void sparse_set<T>::clear()
    {
        m_data.clear();
        m_sparseTable.clear();
        m_freeList.clear();
    }
}
#endif //SPARE_SET_H
