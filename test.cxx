#include "memory"
#include <boost/container/flat_map.hpp>
#include <boost/container/pmr/memory_resource.hpp>
#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>
#include <utility>
#include <vector>

enum class byte : unsigned char {};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FakeMQ
////////////////////////////////////////////////////////////////////////////////////////////////////
//__________________________________________________________________________________________________
class FairMQMessage {
  size_t bytes{ 0 };
  byte* data{ nullptr };

public:
  FairMQMessage(size_t size) : bytes{ size }, data{ new byte[size] }
  {
    printf("ctor FairMQMessage(%li bytes) at %p, data: %p\n", bytes, this, data);
  }

  ~FairMQMessage()
  {
    std::memset(data, 0, bytes);
    delete[] data;
    printf("dtor FairMQMessage() %li bytes at: %p, data: %p\n", bytes, this, data);
  }

  size_t GetSize() const { return bytes; }
  void* GetData() const { return data; }
};

using FairMQMessagePtr = std::unique_ptr<FairMQMessage>;
using fairmq_free_fn = void(void* data, void* hint);

//__________________________________________________________________________________________________
class FairMQTransportFactory {
public:
  FairMQMessagePtr CreateMessage(void* data, size_t size, fairmq_free_fn* ffn, void* hint = nullptr) const
  {
    return std::make_unique<FairMQMessage>(size);
  };
  FairMQMessagePtr CreateMessage(const size_t size) const { return std::make_unique<FairMQMessage>(size); };
};

//__________________________________________________________________________________________________
template <typename ContentT>
struct uninitializedValue {
  union {
    ContentT content;
  };
};

struct elem {
  int content;
  elem() noexcept : content{ 0 } { printf("default ctor elem: %i\n", content); }
  elem(int i) noexcept : content{ i } { printf("ctor elem %i\n", i); }
  ~elem() { printf("dtor elem %i\n", content); }
  elem(const elem& in) noexcept : content{ in.content } { printf("copy ctor elem %i\n", content); }
  elem(const elem&& in) noexcept : content{ in.content } { printf("move ctor elem %i\n", content); }
  elem& operator=(elem& in) noexcept
  {
    content = in.content;
    printf("copy assign elem %i\n", content);
    return *this;
  }
  elem& operator=(elem&& in) noexcept
  {
    content = in.content;
    printf("move assign elem %i\n", content);
    return *this;
  }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

//__________________________________________________________________________________________________
// This in general is a bad idea, but here it is safe to inherit from an allocator since we
// have no data and only override some methods so we don't get into slicing and other problems.
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
  SpectatorMessageResource(const FairMQMessage* _message) : message(_message){};
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
    printf("ChannelResource do_deallocate(%p,%li,%li), %p\n", p, bytes, alignment, messageMap[p].get());
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
    if (that->factory == factory) {
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
}

//__________________________________________________________________________________________________
// This returns a unique_ptr of const vector, does not allow modifications at the cost of pointer
// semantics for access.
template <typename ElemT>
std::unique_ptr<const std::vector<ElemT, FastSpectatorAllocator>> adoptVector(size_t nelem, FairMQMessage* message)
{
  // auto resource = std::make_unique<SpectatorMessageResource>(message);
  auto resource = SpectatorMessageResource(message);
  return std::make_unique<std::vector<ElemT, FastSpectatorAllocator>>(nelem, FastSpectatorAllocator(&resource));
}

//__________________________________________________________________________________________________
// return the message associated with the container or nullptr if it does not make sense (e.g. when we are just
// watching an existing message or when the container is not using O2MemoryResource as backend).
template <typename ContainerT>
typename std::enable_if<std::is_base_of<boost::container::pmr::polymorphic_allocator<byte>,
                                        typename ContainerT::allocator_type>::value == true,
                        FairMQMessagePtr>::type
  getMessage(ContainerT& container)
{
  using namespace boost::container::pmr;
  auto alloc = container.get_allocator();
  printf("getMessage:");
  printf(" alloc: %p", &alloc);
  printf(" alloc.resource: %p", alloc.resource());
  auto resource = dynamic_cast<O2MemoryResource*>(alloc.resource());
  printf(" resource: %p\n", resource);
  if (!resource) {
    return nullptr;
  }
  return std::move(resource->getMessage(static_cast<void*>(container.data())));
};

//__________________________________________________________________________________________________
void print(FairMQMessagePtr& message, const char* prefix = "")
{
  printf(prefix);
  if (!message) {
    printf("message is nullptr\n");
    return;
  }
  printf("message at %p, data at: %p,  size: %li\n", message.get(), message->GetData(), message->GetSize());
}

//__________________________________________________________________________________________________
auto copyVector(auto in) { return in; }

//__________________________________________________________________________________________________
int main()
{
  FairMQTransportFactory factory;
  ChannelResource channelResource(&factory);
  using o2vector = std::vector<elem, FastSpectatorAllocator>;

  printf("\nmake vector\n");
  o2vector vector(FastSpectatorAllocator{ &channelResource });
  vector.reserve(3);
  vector.emplace_back(1);
  vector.emplace_back(2);
  vector.emplace_back(3);
  vector.emplace_back(4);
  printf("vector: %i %i %i %i\n", vector[0].content, vector[1].content, vector[2].content, vector[3].content);
  auto mes = getMessage(vector);
  print(mes);

  printf("\nswap test:\n");
  o2vector v(3, FastSpectatorAllocator{ &channelResource });
  v.swap(vector);
  printf("v: %i %i %i\n", v[0].content, v[1].content, v[2].content);
  printf("vector: %i %i %i\n", vector[0].content, vector[1].content, vector[2].content);

  printf("\nadopt\n");
  auto message = std::make_unique<FairMQMessage>(sizeof(int[3]));
  SpectatorMessageResource messageResource(message.get());
  printf("address of resource3: %p\n", &messageResource);
  int tmpBuf[3] = { 3, 2, 1 };
  std::memcpy(message->GetData(), tmpBuf, sizeof(int[3]));

  o2vector vv = o2vector(3, FastSpectatorAllocator{ &messageResource });
  printf("vv: %i %i %i\n", vv[0].content, vv[1].content, vv[2].content);

  printf("\nget message from vv\n");
  boost::container::pmr::polymorphic_allocator<byte> alv = vv.get_allocator();
  printf("resource from allocator from vv: %p\n", alv.resource());

  printf("\nadoptVector()\n");
  auto vvv = adoptVector<elem>(3, messageResource);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  printf("\ncopy vvv\n");
  o2vector cvvv = copyVector(vvv);
  printf("cvvv: %i %i %i\n", cvvv[0].content, cvvv[1].content, cvvv[2].content);

  printf("\nadopt vector via message with dropping the resource\n");
  auto vvvv = adoptVector<elem>(3, message.get());
  printf("vvvv: %i %i %i\n", (*vvvv)[0].content, (*vvvv)[1].content, (*vvvv)[2].content);

  printf("\nreturn\n");
  return 0;
}
