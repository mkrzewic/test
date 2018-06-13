#include "fake.h"
#include <boost/container/flat_map.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

//__________________________________________________________________________________________________
/// All FairMQ related memory resources need to inherit from this interface class for the getMessage() api.
class FairMQMemoryResource : public boost::container::pmr::memory_resource {
public:
  /// return the message containing data associated with the pointer (to start of buffer),
  /// e.g. pointer returned by std::vector::data()
  /// return nullptr if returning a message does not make sense!
  virtual FairMQMessagePtr getMessage(void* p) = 0;
  virtual const FairMQTransportFactory* getTransportFactory() const noexcept = 0;
  virtual size_t getNumberOfMessages() const noexcept = 0;
};

//__________________________________________________________________________________________________
/// This is the allocator that interfaces to FairMQ memory management. All allocations are delegated
/// to FairMQ so standard (e.g. STL) containers can construct their stuff in memory regions appropriate
/// for the data channel configuration.
class ChannelResource : public FairMQMemoryResource {
protected:
  const FairMQTransportFactory* factory{ nullptr };
  // TODO: for now a map to keep track of allocations, something else would probably be faster, but for now this does
  // not need to be fast.
  boost::container::flat_map<void*, FairMQMessagePtr> messageMap;

public:
  ChannelResource() = delete;
  ChannelResource(const FairMQTransportFactory* _factory) : FairMQMemoryResource(), factory(_factory), messageMap()
  {
    if (!factory) {
      throw std::runtime_error("Tried to construct from a nullptr FairMQTransportFactory");
    }
  };
  FairMQMessagePtr getMessage(void* p) override { return std::move(messageMap[p]); };
  const FairMQTransportFactory* getTransportFactory() const noexcept override { return factory; }

  size_t getNumberOfMessages() const noexcept override { return messageMap.size(); }

protected:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override
  {
    FairMQMessagePtr message;
    message = factory->CreateMessage(bytes);
    void* addr = message->GetData();
    messageMap[addr] = std::move(message);
    return addr;
  };

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
  {
    if (1 > messageMap.erase(p)) {
      // so destructors should not throw, but deallocate maybe should?
      throw std::runtime_error("bad dealloc: not my resource");
    }
    return;
  };

  bool do_is_equal(const boost::container::pmr::memory_resource& other) const noexcept override
  {
    return this == &other;
  };
};

//__________________________________________________________________________________________________
/// This memory resource only watches, does not allocate/deallocate anything.
/// In combination with the ByteSpectatorAllocator this is an alternative to using span, as raw memory
/// (e.g. an existing buffer message) will be accessible with appropriate container.
class SpectatorMessageResource : public FairMQMemoryResource {

public:
  SpectatorMessageResource() = default;
  SpectatorMessageResource(const FairMQMessage* _message) : message(_message){};
  FairMQMessagePtr getMessage(void* p) override { return nullptr; }
  const FairMQTransportFactory* getTransportFactory() const noexcept override { return nullptr; }
  size_t getNumberOfMessages() const noexcept override { return 0; }

protected:
  const FairMQMessage* message;

  virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
  {
    if (message) {
      if (bytes > message->GetSize()) {
        throw std::bad_alloc();
      }
      return message->GetData();
    }
    else {
      return nullptr;
    }
  };
  virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
  {
    message = nullptr;
    return;
  };
  virtual bool do_is_equal(const memory_resource& other) const noexcept override
  {
    const SpectatorMessageResource* that = dynamic_cast<const SpectatorMessageResource*>(&other);
    if (!that) {
      return false;
    }
    if (that->message == message) {
      return true;
    }
    return false;
  };
};

//__________________________________________________________________________________________________
/// This memory resource only watches, does not allocate/deallocate anything, owns the message.
/// In combination with the SpectatorAllocator this is an alternative to using span, as raw memory
/// (e.g. an existing buffer message) will be accessible with appropriate container.
class MessageResource : public FairMQMemoryResource {

public:
  MessageResource() noexcept = delete;
  MessageResource(FairMQMessagePtr _message) : message(std::shared_ptr<FairMQMessage>(std::move(_message))){};
  FairMQMessagePtr getMessage(void* p) override
  {
    if (message.use_count() == 1) {
      FairMQMessage* msgptr{ message.get() };
      message.reset();
      return std::unique_ptr<FairMQMessage>(msgptr);
    }
    else {
      throw std::runtime_error(std::string("MessageResource::getMessage() message.use_count() ") +
                               std::to_string(message.use_count()));
    }
  }
  const FairMQTransportFactory* getTransportFactory() const noexcept override { return nullptr; }
  size_t getNumberOfMessages() const noexcept override { return message ? 1 : 0; }

protected:
  std::shared_ptr<FairMQMessage> message{ nullptr };

  virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
  {
    if (message) {
      if (bytes > message->GetSize()) {
        throw std::bad_alloc();
      }
      return message->GetData();
    }
    else {
      return nullptr;
    }
  };
  virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
  {
    message = nullptr;
    return;
  };
  virtual bool do_is_equal(const memory_resource& other) const noexcept override
  {
    // since this uniquely owns the message it can never be equal to anybody else
    return false;
  };
};

//__________________________________________________________________________________________________
// This in general (as in STL) is a bad idea, but here it is safe to inherit from an allocator since we
// have no additional data and only override some methods so we don't get into slicing and other problems.
template <typename T>
class SpectatorAllocator : public boost::container::pmr::polymorphic_allocator<T> {
public:
  using boost::container::pmr::polymorphic_allocator<T>::polymorphic_allocator;

  // skip default construction of empty elements
  // this is important for two reasons: one: it allows us to adopt an existing buffer (e.g. incoming message) and
  // quickly construct large vectors while skipping the element initialization.
  template <class U>
  void construct(U*)
  {
  }

  // dont try to call destructors, makes no sense since resource is managed externally AND allowed
  // types cannot have side effects
  template <typename U>
  void destroy(U*)
  {
  }

  T* allocate(size_t size) { return reinterpret_cast<T*>(this->resource()->allocate(size * sizeof(T), 0)); }
  void deallocate(T* ptr, size_t size)
  {
    this->resource()->deallocate(const_cast<typename std::remove_cv<T>::type*>(ptr), size);
  }
};

//__________________________________________________________________________________________________
template <typename T>
class OwningMessageSpectatorAllocator {
public:
  using value_type = T;

  MessageResource mResource;

  OwningMessageSpectatorAllocator() noexcept = default;
  OwningMessageSpectatorAllocator(const OwningMessageSpectatorAllocator&) noexcept = default;
  OwningMessageSpectatorAllocator(OwningMessageSpectatorAllocator&&) noexcept = default;
  OwningMessageSpectatorAllocator(const MessageResource& resource) noexcept : mResource{ resource } {}

  template <class U>
  OwningMessageSpectatorAllocator(const OwningMessageSpectatorAllocator<U>& other) noexcept : mResource(other.mResource)
  {
  }

  OwningMessageSpectatorAllocator& operator=(const OwningMessageSpectatorAllocator& other)
  {
    mResource = other.mResource;
    return *this;
  }

  OwningMessageSpectatorAllocator select_on_container_copy_construction() const
  {
    return OwningMessageSpectatorAllocator();
  }

  boost::container::pmr::memory_resource* resource() { return &mResource; }

  // skip default construction of empty elements
  // this is important for two reasons: one: it allows us to adopt an existing buffer (e.g. incoming message) and
  // quickly construct large vectors while skipping the element initialization.
  template <class U>
  void construct(U*)
  {
  }

  // dont try to call destructors, makes no sense since resource is managed externally AND allowed
  // types cannot have side effects
  template <typename U>
  void destroy(U*)
  {
  }

  T* allocate(size_t size) { return reinterpret_cast<T*>(mResource.allocate(size * sizeof(T), 0)); }
  void deallocate(T* ptr, size_t size)
  {
    mResource.deallocate(const_cast<typename std::remove_cv<T>::type*>(ptr), size);
  }
};

using ByteSpectatorAllocator = SpectatorAllocator<byte>;
using BytePmrAllocator = boost::container::pmr::polymorphic_allocator<byte>;

//__________________________________________________________________________________________________
// return the message associated with the container or nullptr if it does not make sense (e.g. when we are just
// watching an existing message or when the container is not using FairMQMemoryResource as backend).
template <typename ContainerT>
// typename std::enable_if<std::is_base_of<boost::container::pmr::polymorphic_allocator<typename
// ContainerT::value_type>,
//                                        typename ContainerT::allocator_type>::value == true,
//                        FairMQMessagePtr>::type
FairMQMessagePtr getMessage(ContainerT&& container_, FairMQMemoryResource* targetResource = nullptr)
{
  auto container = std::move(container_);
  auto alloc = container.get_allocator();

  auto resource = dynamic_cast<FairMQMemoryResource*>(alloc.resource());
  if (!resource && !targetResource) {
    throw std::runtime_error("Neither the container or target resource specified");
  }
  size_t containerSizeBytes = container.size() * sizeof(typename ContainerT::value_type);
  if ((!targetResource && resource) || (resource && targetResource && resource->is_equal(*targetResource))) {
    auto message = resource->getMessage(static_cast<void*>(
      const_cast<typename std::remove_const<typename ContainerT::value_type>::type*>(container.data())));
    message->SetUsedSize(containerSizeBytes);
    return std::move(message);
  }
  else {
    auto message = targetResource->getTransportFactory()->CreateMessage(containerSizeBytes);
    std::memcpy(static_cast<byte*>(message->GetData()), container.data(), containerSizeBytes);
    return std::move(message);
  }
};

//__________________________________________________________________________________________________
template <typename ElemT>
auto adoptVector(size_t nelem, MessageResource& resource)
{
  return std::vector<const ElemT, SpectatorAllocator<ElemT>>(nelem, SpectatorAllocator<ElemT>(&resource));
};

//__________________________________________________________________________________________________
template <typename ElemT>
auto adoptVector(size_t nelem, FairMQMessagePtr message)
{
  return std::vector<const ElemT, OwningMessageSpectatorAllocator<ElemT>>(
    nelem, OwningMessageSpectatorAllocator<ElemT>(MessageResource{ std::move(message) }));
};

//__________________________________________________________________________________________________
// This returns a unique_ptr of const vector, does not allow modifications at the cost of pointer
// semantics for access.
// use auto or decltype to catch the return value (or use span)
template <typename ElemT>
auto adoptVector(size_t nelem, FairMQMessage* message)
{
  using DataType = std::vector<ElemT, ByteSpectatorAllocator>;

  struct doubleDeleter {
    // kids: don't do this at home! (but here it's OK)
    // this stateful deleter allows a single unique_ptr to manage 2 resources at the same time.
    std::unique_ptr<SpectatorMessageResource> extra;
    void operator()(const DataType* ptr) { delete ptr; }
  };

  using OutputType = std::unique_ptr<const DataType, doubleDeleter>;

  auto resource = std::make_unique<SpectatorMessageResource>(message);
  auto output = new DataType(nelem, ByteSpectatorAllocator{ resource.get() });
  return OutputType(output, doubleDeleter{ std::move(resource) });
}

namespace internal {
//__________________________________________________________________________________________________
/// A (thread local) singleton placeholder for the channel allocators. There will normally be 1-2 elements in the map.
// Ideally the transport class itself would hold (or be) the allocator, if that ever happens, this can go away.
class TransportAllocatorMap {
public:
  static TransportAllocatorMap& Instance()
  {
    static TransportAllocatorMap S;
    return S;
  }
  ChannelResource* operator[](const FairMQTransportFactory* factory)
  {
    return std::addressof(map.emplace(factory, factory).first->second);
  }

private:
  std::unordered_map<const FairMQTransportFactory*, ChannelResource> map{};
  TransportAllocatorMap(){};
};
}

//__________________________________________________________________________________________________
/// Get the allocator associated to a transport factory
inline static ChannelResource* getTransportAllocator(const FairMQTransportFactory* factory)
{
  return internal::TransportAllocatorMap::Instance()[factory];
}

//__________________________________________________________________________________________________
void print(FairMQMessage* message, const char* prefix = "")
{
  printf(prefix);
  if (!message) {
    printf("message is nullptr\n");
    return;
  }
  printf("message at %p, data at: %p,  size: %li\n", message, message->GetData(), message->GetSize());
  hexDump(prefix, message->GetData(), message->GetSize());
}

//__________________________________________________________________________________________________
template <typename T>
T copyVector(T&& in)
{
  return std::forward<T>(in);
}

//__________________________________________________________________________________________________
template <typename ElemT>
auto adoptVector(size_t nelem, SpectatorMessageResource& resource)
{
  return std::vector<const ElemT, SpectatorAllocator<ElemT>>(nelem, SpectatorAllocator<ElemT>{ &resource });
};
