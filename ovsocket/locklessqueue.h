#pragma once
#include <atomic>

namespace ovs
{

template<typename Type>
struct Queueable
{
   Type *nextQueueable = nullptr;
};

template<typename Item>
class LocklessQueue
{
public:
   LocklessQueue() :
      mHead(nullptr)
   {
   }

   // Non-atomic clear
   void clear()
   {
      mHead = nullptr;
   }

   void push(Item item)
   {
      item->nextQueueable = mHead;

      while (!std::atomic_compare_exchange_weak_explicit(&mHead, &item->nextQueueable, item, std::memory_order_release, std::memory_order_relaxed)) {
         item->nextQueueable = mHead;
      }
   }

   Item pop()
   {
      Item value = mHead;

      if (!value) {
         return nullptr;
      }

      while (!std::atomic_compare_exchange_weak_explicit(&mHead, &value, value->nextQueueable, std::memory_order_release, std::memory_order_relaxed)) {
         value = mHead;
      }

      return value;
   }

private:
   std::atomic<Item> mHead;
};

}
