module;

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>

export module YT:BlockTable;

import :Types;

namespace YT
{
    /**
     * @brief A thread-safe reference counting mechanism that combines generation tracking with reference counting.
     * 
     * This class provides atomic operations for managing both a generation number and a reference count
     * in a single 64-bit atomic value. The generation number is used to detect stale handles, while
     * the reference count tracks the number of active references to an object.
     * 
     * Thread Safety:
     * - All operations are thread-safe and lock-free
     * - Memory ordering is carefully chosen to ensure proper synchronization
     * - Operations use acquire/release semantics where appropriate
     */
    export class BlockTableGenerationRefCount
    {
    public:

        static constexpr std::uint32_t Uint32_Max = std::numeric_limits<std::uint32_t>::max();

        BlockTableGenerationRefCount() noexcept = default;
        BlockTableGenerationRefCount(const BlockTableGenerationRefCount &) noexcept = delete;
        BlockTableGenerationRefCount(BlockTableGenerationRefCount &&) noexcept = delete;

        BlockTableGenerationRefCount &operator=(const BlockTableGenerationRefCount &) noexcept = delete;
        BlockTableGenerationRefCount &operator=(BlockTableGenerationRefCount &&) noexcept = delete;
        ~BlockTableGenerationRefCount() = default;

        void SetGenerationAndRefCount(std::uint32_t generation, std::uint32_t ref_count = 0) noexcept
        {
            m_Data.store(static_cast<std::uint64_t>(ref_count) << 32 | static_cast<std::uint64_t>(generation), std::memory_order_release);
        }

        [[nodiscard]] bool CheckGeneration(std::uint32_t generation) const noexcept
        {
            return (m_Data.load(std::memory_order_acquire) & Uint32_Max) == generation;
        }

        [[nodiscard]] std::uint32_t GetGeneration() const noexcept
        {
            return m_Data.load(std::memory_order_acquire) & Uint32_Max;
        }

        bool IncRef(std::uint32_t generation) noexcept
        {
            while (true)
            {
                std::uint64_t data = m_Data.load(std::memory_order_acquire);
                std::uint64_t cur_gen = (data & Uint32_Max);
                std::uint64_t cur_ref = (data >> 32) & Uint32_Max;

                if (cur_gen != generation)
                {
                    return false;
                }

                std::uint64_t new_data = (cur_ref + 1) << 32 | cur_gen;

                if (m_Data.compare_exchange_weak(data, new_data))
                {
                    return true;
                }
            }
        }

        void IncRefNoValidation() noexcept
        {
            m_Data.fetch_add(Uint32_Max + 1, std::memory_order_release);
        }

        bool DecRef(std::uint32_t generation, std::uint32_t & out_ref_count) noexcept
        {
            while (true)
            {
                std::uint64_t data = m_Data.load(std::memory_order_acquire);
                std::uint64_t cur_gen = (data & Uint32_Max);
                std::uint64_t cur_ref = (data >> 32) & Uint32_Max;

                if (cur_gen != generation)
                {
                    return false;
                }

                if (cur_ref == 0)
                {
                    return false;
                }

                cur_ref--;
                std::uint64_t new_data = (cur_ref << 32) | cur_gen;

                if (m_Data.compare_exchange_weak(data, new_data))
                {
                    out_ref_count = cur_ref;
                    return true;
                }
            }
        }

        bool DecRefNoValidation() noexcept
        {
            std::uint64_t result = m_Data.fetch_sub(Uint32_Max + 1, std::memory_order_release);

            std::uint64_t cur_ref = (result >> 32) & Uint32_Max;
            return cur_ref != 0;
        }

    private:
        std::atomic_uint64_t m_Data = 0;
    };

    /**
     * @brief A handle to an element in a BlockTable.
     * 
     * This struct represents a reference to an element stored in a BlockTable. It contains:
     * - m_BlockIndex: The index of the block containing the element
     * - m_ElemIndex: The index of the element within its block
     * - m_Generation: A generation number used to detect stale handles
     * 
     * The handle is designed to be passed by value and is 64 bits in size.
     */
    export struct BlockTableHandle
    {
        explicit operator bool () const noexcept { return *this != BlockTableHandle{}; }

        bool operator==(const BlockTableHandle &) const noexcept = default;
        bool operator!=(const BlockTableHandle &) const noexcept = default;

        std::uint16_t m_BlockIndex = 0;
        std::uint16_t m_ElemIndex = 0;
        std::uint32_t m_Generation = 0;
    };

    export constexpr BlockTableHandle InvalidBlockTableHandle = {};
    static_assert(sizeof(BlockTableHandle) == sizeof(std::uint64_t));

    export template <typename HandleType>
    constexpr HandleType MakeCustomBlockTableHandle(const BlockTableHandle & handle)
    {
        static_assert(sizeof(HandleType) == sizeof(BlockTableHandle));
        static_assert(std::is_base_of_v<BlockTableHandle, HandleType>);

        return HandleType{ handle };
    }

    /**
     * @brief A thread-safe, block-based container for storing objects with handle-based access.
     * 
     * This class provides a high-performance, thread-safe container for storing objects of type T.
     * It uses a block-based allocation strategy to minimize memory fragmentation and provide
     * efficient allocation/deallocation.
     * 
     * Features:
     * - Thread-safe operations
     * - Handle-based access to elements
     * - Generation tracking to detect stale handles
     * - Reference counting for elements
     * - Efficient block-based allocation
     * 
     * Thread Safety:
     * - All public methods are thread-safe
     * - Block allocation is protected by a mutex
     * - Element access uses atomic operations with appropriate memory ordering
     * 
     * Memory Ordering:
     * - Block allocation uses mutex for synchronization
     * - Element access uses acquire/release semantics
     * - Reference counting uses acquire/release semantics
     * 
     * @tparam T The type of object to store
     * @tparam BlockSize The number of elements per block (must be a multiple of 64)
     * @tparam BlockCount The maximum number of blocks
     */
    export template <typename T, int BlockSize = 65536, int BlockCount = 2048>
    class BlockTable final
    {
    private:

        static constexpr std::uint32_t Uint32_Max = std::numeric_limits<std::uint32_t>::max();
        static constexpr std::uint64_t Uint64_Max = std::numeric_limits<std::uint64_t>::max();

        static_assert(BlockSize % 64 == 0, "BlockSize must be a multiple of 64");
        static_assert(BlockSize > 0, "BlockSize must be greater than 0");
        static_assert(BlockSize <= 65536, "BlockSize must be less than 65536");
        static_assert(BlockCount > 0, "BlockCount must be greater than 0");
        static_assert(BlockCount <= 65536, "BlockCount must be less than 65536");

        /**
         * @brief Internal structure for storing an element and its metadata.
         */
        struct ElementStorage
        {
            BlockTableGenerationRefCount m_GenerationRefCount;
            std::byte m_Buffer[sizeof(T)];
        };

        /**
         * @brief Internal structure representing a block of elements.
         */
        struct Block
        {
            std::atomic_int m_LowestFreeIndexHeuristic = 0;
            std::atomic_int m_FreeCount = BlockSize;
            std::atomic_uint64_t m_BlockAlloc[(BlockSize + 63) / 64];
            ElementStorage m_BlockData[BlockSize];
        };

    public:

        static constexpr int BlockSizeValue = BlockSize;
        static constexpr int BlockCountValue = BlockCount;

        BlockTable() = default;
        BlockTable(const BlockTable &) = delete;
        BlockTable(BlockTable &&) = delete;

        BlockTable &operator=(const BlockTable &) = delete;
        BlockTable &operator=(BlockTable &&) = delete;

        ~BlockTable()
        {
            Clear();
        }

        /**
         * @brief Clears all elements from the table.
         * 
         * This method destroys all elements and frees all blocks. It is not thread-safe
         * with respect to other operations on the table. The caller must ensure that no
         * other threads are accessing the table while this method is executing.
         * 
         * The method is noexcept. If an element's destructor throws, the exception is caught
         * and the cleanup continues for remaining elements.
         * 
         * @note This method acquires a mutex to prevent concurrent access during cleanup.
         *       However, it does not prevent other threads from accessing the table
         *       before or after the cleanup operation.
         */
        void Clear() noexcept
        {
            std::lock_guard guard(m_BlockAllocMutex);
            for (UniquePtr<Block> & block : m_Blocks)
            {
                if (block)
                {
                    int element_index = 0;
                    for (int word_index = 0; word_index < BlockSize / 64; ++word_index, ++element_index)
                    {
                        std::uint64_t word = block->m_BlockAlloc[word_index].load(std::memory_order_acquire);
                        if (word == 0)
                        {
                            element_index += 64;
                            continue;
                        }

                        for (int bit_index = 0; bit_index < 64; ++bit_index, ++element_index)
                        {
                            if (word & (1ULL << bit_index))
                            {
                                ElementStorage & element = block->m_BlockData[element_index];

                                try
                                {
                                    T * t = reinterpret_cast<T *>(&element.m_Buffer);
                                    t->~T();
                                }
                                catch (...)
                                {
                                    
                                }
                            }
                        }
                    }
                }

                block.reset();
            }
        }

        /**
         * @brief Allocates a new element and returns a handle to it.
         * 
         * This method is thread-safe and noexcept. It will:
         * 1. Find or create a block with free space
         * 2. Allocate a slot in the block
         * 3. Construct the element
         * 4. Return a handle to the element
         * 
         * @param args Arguments to forward to T's constructor
         * @return A handle to the new element, or InvalidBlockTableHandle if allocation failed
         */
        template <typename... Args>
        [[nodiscard]] BlockTableHandle AllocateHandle(Args &&... args) noexcept
        {
            // Try to find a block with free space
            for (int block_index = 0; block_index < BlockCount; ++block_index)
            {
                UniquePtr<Block> & block = m_Blocks[block_index];
                // Make a new block
                if (!block)
                {
                    std::lock_guard guard(m_BlockAllocMutex);
                    // Check the block again in case another thread got in here
                    if (!block)
                    {
                        block = MakeUnique<Block>();
                    }
                }

                if (Optional<int> slot_index = ReserveSlotInBlock(block))
                {
                    return AssignSlot(block, block_index, slot_index.value(), std::forward<Args>(args)...);
                }
            }

            return InvalidBlockTableHandle;
        }

        /**
         * @brief Releases an element referenced by a handle.
         * 
         * This method is thread-safe and noexcept. It will:
         * 1. Validate the handle's generation
         * 2. Destroy the element if the generation matches
         * 3. Release the slot for reuse
         * 
         * @param handle The handle to release
         * @return true if the element was successfully released, false otherwise
         */
        bool ReleaseHandle(BlockTableHandle handle) noexcept
        {
            if (handle.m_Generation == 0)
            {
                return false;
            }

            if (handle.m_BlockIndex >= BlockCount)
            {
                return false;
            }

            if (!m_Blocks[handle.m_BlockIndex])
            {
                return false;
            }

            UniquePtr<Block> & block = m_Blocks[handle.m_BlockIndex];
            ElementStorage & element = block->m_BlockData[handle.m_ElemIndex];

            if (element.m_GenerationRefCount.CheckGeneration(handle.m_Generation))
            {
                element.m_GenerationRefCount.SetGenerationAndRefCount(0);

                try
                {
                    T * t = reinterpret_cast<T *>(&element.m_Buffer);
                    t->~T();
                }
                catch (...)
                {
                    
                }

                ReleaseSlot(block, handle.m_ElemIndex);
                block->m_LowestFreeIndexHeuristic = handle.m_ElemIndex;
                block->m_FreeCount.fetch_add(1, std::memory_order_seq_cst);
                return true;
            }

            return false;
        }

        /**
         * @brief Resolves a handle to a pointer to the element.
         * 
         * This method is thread-safe and noexcept. It will:
         * 1. Validate the handle's generation
         * 2. Return a pointer to the element if the generation matches
         * 
         * @param handle The handle to resolve
         * @return A pointer to the element, or nullptr if the handle is invalid
         */
        OptionalPtr<T> ResolveHandle(BlockTableHandle handle) noexcept
        {
            if (handle.m_Generation == 0)
            {
                return nullptr;
            }

            if (handle.m_BlockIndex >= BlockCount)
            {
                return nullptr;
            }

            if (!m_Blocks[handle.m_BlockIndex])
            {
                return nullptr;
            }

            ElementStorage & element = m_Blocks[handle.m_BlockIndex]->m_BlockData[handle.m_ElemIndex];
            if (element.m_GenerationRefCount.CheckGeneration(handle.m_Generation))
            {
                return reinterpret_cast<T *>(&element.m_Buffer);
            }

            return nullptr;
        }

        /**
         * @brief Gets a pointer to the generation reference count for a handle.
         * 
         * This method is thread-safe and noexcept. It will:
         * 1. Validate the handle's generation
         * 2. Return a pointer to the generation reference count if the generation matches
         * 
         * @param handle The handle to get the generation pointer for
         * @return A pointer to the generation reference count, or nullptr if the handle is invalid
         */
        OptionalPtr<BlockTableGenerationRefCount> GetGenerationPointer(BlockTableHandle handle) noexcept
        {
            if (handle.m_Generation == 0)
            {
                return nullptr;
            }

            ElementStorage & element = m_Blocks[handle.m_BlockIndex]->m_BlockData[handle.m_ElemIndex];
            if (element.m_GenerationRefCount.CheckGeneration(handle.m_Generation))
            {
                return &element.m_GenerationRefCount;
            }

            return nullptr;
        }

        /**
         * @brief Visits all valid handles in the table.
         * 
         * This method is thread-safe and noexcept. It will call the visitor
         * function for each valid handle in the table. The visitor should not
         * modify the table's structure.
         * 
         * @note The visitor function must not throw exceptions. If the visitor
         *       throws, the behavior is undefined.
         * 
         * @tparam Visitor A callable type that takes a BlockTableHandle
         * @param visitor The visitor function to call for each handle
         */
        template <typename Visitor>
        void VisitAllHandles(Visitor && visitor) noexcept
        {
            for (int block_index = 0; block_index < BlockCount; ++block_index)
            {
                if (m_Blocks[block_index])
                {
                    int element_index = 0;
                    for (int word_index = 0; word_index < BlockSize / 64; ++word_index, ++element_index)
                    {
                        std::uint64_t word = m_Blocks[block_index]->m_BlockAlloc[word_index].load(std::memory_order_acquire);
                        if (word == 0)
                        {
                            continue;
                        }

                        for (int bit_index = 0; bit_index < 64; ++bit_index, ++element_index)
                        {
                            if (word & (1ULL << bit_index))
                            {
                                ElementStorage & element = m_Blocks[block_index]->m_BlockData[element_index];
            
                                visitor(BlockTableHandle
                                {
                                    .m_BlockIndex = static_cast<std::uint16_t>(block_index),
                                    .m_ElemIndex = static_cast<std::uint16_t>(element_index),
                                    .m_Generation = static_cast<std::uint32_t>(element.m_GenerationRefCount.GetGeneration()),
                                });
                            }
                        }
                    }
                }
            }
        }
    private:

        bool AllocateSlot(UniquePtr<Block> & block, int element_index) noexcept
        {
            std::atomic_uint64_t & alloc_bits = block->m_BlockAlloc[element_index / 64];
            int bit_index = element_index % 64;

            while (true)
            {
                std::uint64_t current_alloc_bits = alloc_bits.load(std::memory_order_seq_cst);

                if (current_alloc_bits & (1ULL << bit_index))
                {
                    return false;
                }

                std::uint64_t new_alloc_bits = current_alloc_bits | (1ULL << bit_index);
                if (alloc_bits.compare_exchange_weak(current_alloc_bits, new_alloc_bits))
                {
                    return true;
                }
            }
        }

        void ReleaseSlot(UniquePtr<Block> & block, int element_index) noexcept
        {
            std::atomic_uint64_t & alloc_bits = block->m_BlockAlloc[element_index / 64];
            int bit_index = element_index % 64;

            while (true)
            {
                std::uint64_t current_alloc_bits = alloc_bits.load(std::memory_order_seq_cst);
                std::uint64_t new_alloc_bits = current_alloc_bits & ~(1ULL << bit_index);

                if (alloc_bits.compare_exchange_weak(current_alloc_bits, new_alloc_bits))
                {
                    return;
                }
            }
        }

        template <typename... Args>
        BlockTableHandle AssignSlot(UniquePtr<Block> & block, int block_index, int element_index, Args &&... args) noexcept
        {
            std::uint32_t generation = m_NextGeneration.fetch_add(1, std::memory_order_relaxed);

            ElementStorage & storage = block->m_BlockData[element_index];

            storage.m_GenerationRefCount.SetGenerationAndRefCount(generation, 0);

            void * storage_address = &storage.m_Buffer;
            new (storage_address) T(std::forward<Args>(args)...);

            return BlockTableHandle
            {
                .m_BlockIndex = static_cast<std::uint16_t>(block_index),
                .m_ElemIndex = static_cast<std::uint16_t>(element_index),
                .m_Generation = generation,
            };
        }

        Optional<int> FindSlotInBlock(UniquePtr<Block> & block) noexcept
        {
            auto CheckWord = [&](int word) -> Optional<int>
            {
                std::uint64_t alloc_bits = block->m_BlockAlloc[word];

                if (alloc_bits != Uint64_Max)
                {
                    int bit_index = std::countr_one(alloc_bits);

                    int element_index = bit_index + word * 64;
                    if (AllocateSlot(block, element_index))
                    {
                        return element_index;
                    }
                }

                return {};
            };

            int search_start = block->m_LowestFreeIndexHeuristic.load(std::memory_order_relaxed);
            int word_start = search_start / 64;

            for (int word = word_start; word < BlockSize / 64; ++word)
            {
                if (auto slot = CheckWord(word))
                {
                    return slot;
                }
            }

            for (int word = 0; word < word_start; ++word)
            {
                if (auto slot = CheckWord(word))
                {
                    return slot;
                }
            }

            return {};
        }

        Optional<int> ReserveSlotInBlock(UniquePtr<Block> & block) noexcept
        {
            if (block->m_FreeCount.fetch_sub(1, std::memory_order_acquire) > 0)
            {
                // Search for a free element
                if (Optional<int> element_index = FindSlotInBlock(block))
                {
                    block->m_LowestFreeIndexHeuristic = element_index.value() + 1;
                    return element_index;
                }

                // Somehow we had a free slot according to the free count, but it got snatched up?
                block->m_FreeCount.fetch_add(1, std::memory_order_release);
            }
            else
            {
                block->m_FreeCount.fetch_add(1, std::memory_order_relaxed);
            }

            return {};
        }


    private:

        Mutex m_BlockAllocMutex;
        std::array<UniquePtr<Block>, BlockCount> m_Blocks = { };
        std::atomic_uint32_t m_NextGeneration = 1;

    };
}

