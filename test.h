#include "fake.h"
#include <boost/container/flat_map.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

//__________________________________________________________________________________________________
// This in general (as in STL) is a bad idea, but here it is safe to inherit from an allocator since we
// have no additional data and only override some methods so we don't get into slicing and other problems.
template <typename T>
class SpectatorAllocator : public boost::container::pmr::polymorphic_allocator<T> {
public:
  // inherit ctors
  using boost::container::pmr::polymorphic_allocator<T>::polymorphic_allocator;

  // skip default construction of empty elements
  // this is important for two reasons: one: it allows us to adopt an existing buffer (e.g. incoming message) and
  // quickly construct large vectors skipping the element initialization.
  template <typename... U>
  void construct(U*...){};

  // dont try to call destructors, makes no sense since resource is managed externally AND allowed
  // types cannot have side effects
  template <typename... U>
  void destroy(U*...){};
};

using FastSpectatorAllocator = SpectatorAllocator<byte>;
// using FastSpectatorAllocator = boost::container::pmr::polymorphic_allocator<byte>;

//__________________________________________________________________________________________________
class O2MemoryResource : public boost::container::pmr::memory_resource {
public:
  // return the message containing data associated with the pointer (to start of buffer),
  // e.g. pointer returned by std::vector::data()
  // return nullptr if returning a message does not make sense!
  virtual FairMQMessagePtr getMessage(void* p) = 0;
};

//__________________________________________________________________________________________________
class SpectatorMessageResource : public O2MemoryResource {
protected:
  const FairMQMessage* message;

public:
  SpectatorMessageResource(const FairMQMessage* _message) : message(_message)
  {
    printf("ctor SpectatorMessageResource %p, message data %p\n", this, message->GetData());
  };
  ~SpectatorMessageResource() { printf("dtor SpectatorMessageResource %p\n", this); }
  FairMQMessagePtr getMessage(void* p) override { return nullptr; }

protected:
  virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
  {
    printf("SpectatorMessageResource %p do_allocate bytes:%li, align:%li\n", this, bytes, alignment);
    if (message) {
      if (bytes > message->GetSize()) {
        throw std::bad_alloc();
      }
      printf("have message: requested: %li, available: %li\n", bytes, message->GetSize());
      return message->GetData();
    }
    else {
      printf("no message: requested: %li, available: %li\n", bytes, message->GetSize());
      return nullptr;
    }
  };
  virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
  {
    printf("SpectatorMessageResource %p do_deallocate pointer:%p, bytes:%li align:%li, message %p\n", this, p, bytes,
           alignment, message);
    message = nullptr;
    return;
  };
  virtual bool do_is_equal(const memory_resource& other) const noexcept override
  {
    const SpectatorMessageResource* that = dynamic_cast<const SpectatorMessageResource*>(&other);
    if (!that) {
      return false;
    }
    printf("SpectatorMessageResource do_is_equal: factories: %p %p\n", that->message, message);
    if (that->message == message) {
      return true;
    }
    return false;
  };
};

//__________________________________________________________________________________________________
class ChannelResource : public O2MemoryResource {
protected:
  FairMQTransportFactory* factory{ nullptr };
  boost::container::flat_map<void*, FairMQMessagePtr> messageMap;

public:
  ChannelResource() { printf("ChannelResource default ctor, at: %p\n", this); }
  ChannelResource(FairMQTransportFactory* _factory) : O2MemoryResource(), factory(_factory), messageMap()
  {
    printf("ChannelResource ctor, at: %p\n", this);
  };
  FairMQMessagePtr getMessage(void* p) { return std::move(messageMap[p]); };

protected:
  virtual void* do_allocate(std::size_t bytes, std::size_t alignment)
  {
    FairMQMessagePtr message;
    printf("ChannelResource do_allocate bytes:%li, align:%li\n  ", bytes, alignment);
    if (!factory) {
      throw std::bad_alloc();
    }
    else {
      message = factory->CreateMessage(bytes);
    }
    void* addr = message->GetData();
    printf("ChannelResource data at: %p\n", addr);
    messageMap[addr] = std::move(message);
    return addr;
  };

  virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment)
  {
    printf("ChannelResource do_deallocate ptr: %p, bytes: %li, align: %li, message %p\n", p, bytes, alignment,
           messageMap[p].get());
    if (!messageMap.erase(p)) {
      throw std::runtime_error("bad dealloc in ChannelResource");
    }
    return;
  };

  virtual bool do_is_equal(const memory_resource& other) const noexcept
  {
    const ChannelResource* that = dynamic_cast<const ChannelResource*>(&other);
    if (!that) {
      return false;
    }
    printf("ChannelResource do_is_equal: factories: %p %p\n", that->factory, factory);
    if (factory && that->factory == factory) {
      return true;
    }
    return false;
  };
};

//__________________________________________________________________________________________________
// This returns a **NON-CONST** vector which should not be touched! To have a guarantee of constness
// see the other adoptVector.
template <typename ElemT>
auto adoptVector(size_t nelem, SpectatorMessageResource& resource)
{
  return std::vector<ElemT, FastSpectatorAllocator>(nelem, FastSpectatorAllocator{ &resource });
};

//__________________________________________________________________________________________________
// This returns a unique_ptr of const vector, does not allow modifications at the cost of pointer
// semantics for access.
template <typename ElemT>
auto adoptVector(size_t nelem, FairMQMessage* message)
{
  using DataType = std::vector<ElemT, FastSpectatorAllocator>;

  struct doubleDeleter {
    // kids: don't do this at home!
    std::unique_ptr<SpectatorMessageResource> extra;
    void operator()(const DataType* ptr) { delete ptr; }
    doubleDeleter(std::unique_ptr<SpectatorMessageResource> in) : extra{std::move(in)} {printf("ctor doubleDeleter(resource) %p\n",this);}
    doubleDeleter() : extra{nullptr} {printf("ctor doubleDeleter %p\n",this);}
    doubleDeleter(doubleDeleter&& in) : extra{std::move(in.extra)} {printf("move ctor doubleDeleter %p->%p\n",&in,this);}
    doubleDeleter(const doubleDeleter& in) = delete;
    doubleDeleter& operator=(const doubleDeleter&) = delete;
    doubleDeleter& operator=(doubleDeleter&& in) {extra = std::move(in.extra); printf("move assignment doubleDeleter %p=%p\n",this,&in); return *this;}
    ~doubleDeleter() {printf("dtor doubleDeleter %p\n",this);}
  };

  using OutputType = std::unique_ptr<const DataType, doubleDeleter>;

  auto resource = std::make_unique<SpectatorMessageResource>(message);
  auto output = new DataType(nelem, FastSpectatorAllocator{ resource.get() });
  printf("adoptVector int vec size: %li data:@%p:, first elem @%p, %i %i %i\n", output->size(), output->data(),
         &((*output)[0]), (*output)[0].content, (*output)[1].content, (*output)[2].content);
  return OutputType(output, doubleDeleter{ std::move(resource) });
}

//__________________________________________________________________________________________________
// return the message associated with the container or nullptr if it does not make sense (e.g. when we are just
// watching an existing message or when the container is not using O2MemoryResource as backend).
template <typename ContainerT>
typename std::enable_if<std::is_base_of<boost::container::pmr::polymorphic_allocator<byte>,
                                        typename ContainerT::allocator_type>::value == true,
                        FairMQMessagePtr>::type
  getMessage(ContainerT&& container, boost::container::pmr::memory_resource* targetResource=nullptr)
{
  using namespace boost::container::pmr;
  auto alloc = container.get_allocator();
  printf("getMessage:");
  printf(" alloc: %p", &alloc);
  printf(" alloc.resource: %p", alloc.resource());

  auto resource = dynamic_cast<O2MemoryResource*>(alloc.resource());
  printf(" resource: %p\n", resource);
  //TODO: check if resource is same as target, if not, trugger copy to new message for target.
  //      ALWAYS return a valid message unless something crazy happens
  if (!resource) {
    return nullptr;
  }
  auto message = resource->getMessage(static_cast<void*>(container.data()));
  message->SetUsedSize(container.size() * sizeof(typename ContainerT::value_type));
  return std::move(message);
};

//__________________________________________________________________________________________________
template <typename ContainerT>
bool AddData(O2Message& parts, Stack&& inputStack, ContainerT&& inputData)
{
  FairMQMessagePtr dataMessage = getMessage(std::move(inputData));
  FairMQMessagePtr headerMessage = getMessage(std::move(inputStack));

  parts.AddPart(std::move(headerMessage));
  parts.AddPart(std::move(dataMessage));

  return true;
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
  hexDump(prefix,message->GetData(), message->GetSize());
}

//__________________________________________________________________________________________________
template<typename T>
T copyVector(T&& in) { return std::forward<T>(in); }
