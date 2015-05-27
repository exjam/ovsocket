#pragma once
#include "locklessqueue.h"

namespace ovs
{

template<typename Type>
class LocklessPool
{
   struct Item : public Queueable<Item>
   {
      Type value;
   };

   static const auto PointerOffset = reinterpret_cast<ptrdiff_t>(&reinterpret_cast<Item*>(0)->value);

public:
   LocklessPool()
   {
   }

   LocklessPool(size_t size)
   {
      resize(size);
   }

   void resize(size_t size)
   {
      mQueue.clear();
      mMemory.clear();
      mMemory.reserve(size);

      for (auto i = 0u; i < size; ++i) {
         mMemory.push_back(Item());
      }

      for (auto i = 0u; i < size; ++i) {
         mQueue.push(&mMemory[i]);
      }
   }

   Type *allocate()
   {
      return reinterpret_cast<Type*>(reinterpret_cast<size_t>(mQueue.pop()) + PointerOffset);
   }

   void free(Type *value)
   {
      Item *item = reinterpret_cast<Item*>(reinterpret_cast<size_t>(value) - PointerOffset);
      mQueue.push(item);
   }

private:
   std::vector<Item> mMemory;
   LocklessQueue<Item*> mQueue;
};

}
