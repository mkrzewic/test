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
  size_t usedBytes{ 0 };

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
  bool SetUsedSize(const size_t size)
  {
    if (size <= bytes) {
      usedBytes = size;
      return true;
    }
    else
      return false;
  }
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
  // int more;
  elem() noexcept : content{ 0 } { printf("default ctor elem: %i @%p\n", content, this); }
  elem(int i) noexcept : content{ i } { printf("ctor elem %i @%p\n", i, this); }
  ~elem() { printf("dtor elem %i @%p\n", content, this); }
  elem(const elem& in) noexcept : content{ in.content } { printf("copy ctor elem %i %p -> %p\n", content, &in, this); }
  elem(const elem&& in) noexcept : content{ in.content } { printf("move ctor elem %i %p -> %p\n", content, &in, this); }
  elem& operator=(elem& in) noexcept
  {
    content = in.content;
    printf("copy assign elem %i %p = %p\n", content, this, &in);
    return *this;
  }
  elem& operator=(elem&& in) noexcept
  {
    content = in.content;
    printf("move assign elem %i %p = %p\n", content, this, &in);
    return *this;
  }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
struct BaseHeader {
  bool flagsNextHeader{false};
  uint32_t headerSize{sizeof(BaseHeader)};

  uint32_t size() const noexcept {return headerSize;}
  const byte* data() const noexcept { return reinterpret_cast<const byte*>(this); }
  const BaseHeader* next() const noexcept
  {
    return (flagsNextHeader) ? reinterpret_cast<const BaseHeader*>(reinterpret_cast<const byte*>(this) + headerSize)
                             : nullptr;
  }
  BaseHeader() noexcept : flagsNextHeader{ 0 } { printf("default ctor BaseHeader: %i @%p\n", flagsNextHeader, this); }
  ~BaseHeader() { printf("dtor BaseHeader %i @%p\n", flagsNextHeader, this); }
  BaseHeader(const BaseHeader& in) noexcept : flagsNextHeader{ in.flagsNextHeader }
  {
    printf("copy ctor BaseHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  BaseHeader(const BaseHeader&& in) noexcept : flagsNextHeader{ in.flagsNextHeader }
  {
    printf("move ctor BaseHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  BaseHeader& operator=(BaseHeader& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("copy assign BaseHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
  BaseHeader& operator=(BaseHeader&& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("move assign BaseHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
};

template <typename ContentT>
struct InitWrapper {
};

struct Stack {

  // This is ugly and needs fixing BUT:
  // we need a static deleter for fairmq.
  // TODO: maybe use allocator_traits if custom allocation is desired
  //       the deallocate function is then static.
  //       In the case of special transports is is cheap to copy the header stack into a message, so no problem there.
  //       The copy can be avoided if we construct in place inside a FairMQMessage directly (instead of
  //       allocating a unique_ptr we would hold a FairMQMessage which for the large part has similar semantics).
  //
  using Buffer = std::unique_ptr<byte[]>;
  static std::default_delete<byte[]> sDeleter;
  static void freefn(void* /*data*/, void* hint) { Stack::sDeleter(static_cast<byte*>(hint)); }

  size_t bufferSize;
  Buffer buffer;

  byte* data() const { return buffer.get(); }
  size_t size() const { return bufferSize; }

  /// The magic constructor: takes arbitrary number of arguments and serialized them
  /// into the buffer.
  /// Intended use: produce a temporary via an initializer list.
  /// TODO: maybe add a static_assert requiring first arg to be DataHeader
  /// or maybe even require all these to be derived form BaseHeader
  template <typename... Headers>
  Stack(const Headers&... headers) : bufferSize{ size(headers...) }, buffer{ std::make_unique<byte[]>(bufferSize) }
  {
    printf("Stack ctor\n");
    inject(buffer.get(), headers...);
  }
  Stack() = default;
  Stack(Stack&&) = default;
  Stack(Stack&) = delete;
  Stack& operator=(Stack&) = delete;
  Stack& operator=(Stack&&) = default;

  template <typename T, typename... Args>
  static size_t size(const T& h, const Args... args) noexcept
  {
    return size(h) + size(args...);
  }

private:
  template <typename T>
  static size_t size(const T& h) noexcept
  {
    return h.size();
  }

  template <typename T>
  static byte* inject(byte* here, const T& h) noexcept
  {
    printf("  Stack: injecting from %p-> %p\n",&h, here);
    std::copy(h.data(), h.data() + h.size(), here);
    return here + h.size();
  }

  template <typename T, typename... Args>
  static byte* inject(byte* here, const T& h, const Args... args) noexcept
  {
    auto alsohere = inject(here, h);
    (reinterpret_cast<BaseHeader*>(here))->flagsNextHeader = true;
    return inject(alsohere, args...);
  }
};

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
  SpectatorMessageResource(const FairMQMessage* _message) : message(_message){printf("ctor SpectatorMessageResource %p, message data %p\n",this,message->GetData());};
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
};

//__________________________________________________________________________________________________
template <typename ElemT>
struct Adopt {
  template <typename FirstT, typename ExtraT>
  struct doubleDeleter {
    // kids: don't do this at home!
    std::unique_ptr<ExtraT> extra;
    void operator()(const FirstT* ptr) { delete ptr; }
  };

  using DataType = std::vector<ElemT, FastSpectatorAllocator>;
  using DeleterType = doubleDeleter<DataType, SpectatorMessageResource>;
  using OutputType = std::unique_ptr<const DataType, DeleterType>;

  //__________________________________________________________________________________________________
  // This returns a unique_ptr of const vector, does not allow modifications at the cost of pointer
  // semantics for access.
  static auto Vector(size_t nelem, FairMQMessage* message)
  {
    auto resource = std::make_unique<SpectatorMessageResource>(message);
    auto output = new DataType(nelem, FastSpectatorAllocator{resource.get()});
    return OutputType(
      output,
      DeleterType{ std::move(resource) });
  }
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
  };

  using OutputType = std::unique_ptr<const DataType, doubleDeleter>;

  auto resource = std::make_unique<SpectatorMessageResource>(message);
  auto output = new DataType(nelem, FastSpectatorAllocator{resource.get()});
  printf("adoptVector int vec size: %li data:@%p:, first elem @%p, %i %i %i\n", output->size(), output->data(), &((*output)[0]), (*output)[0].content, (*output)[1].content, (*output)[2].content );
  return OutputType(output,doubleDeleter{std::move(resource)});
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
  auto message = resource->getMessage(static_cast<void*>(container.data()));
  message->SetUsedSize(container.size() * sizeof(typename ContainerT::value_type));
  container.clear();
  return std::move(message);
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

  printf("\nmake vector with FastSpectatorAllocator\n");
  o2vector vector(3, FastSpectatorAllocator{ &channelResource });
  // vector.reserve(3);
  vector.emplace_back(1);
  vector.emplace_back(2);
  vector.emplace_back(3);
  vector.emplace_back(4);
  printf("vector: %i %i %i %i\n", vector[0].content, vector[1].content, vector[2].content, vector[3].content);
  auto mes = getMessage(vector);
  print(mes);
  printf("vector size: %li\n", vector.size());

  printf("\nmake vector with pmr\n");
  std::vector<elem, boost::container::pmr::polymorphic_allocator<byte>> vectorPol(
    3, boost::container::pmr::polymorphic_allocator<byte>{ &channelResource });
  // vectorPol.reserve(3);
  vectorPol.emplace_back(1);
  vectorPol.emplace_back(2);
  vectorPol.emplace_back(3);
  vectorPol.emplace_back(4);
  printf("vectorPol: %i %i %i %i\n", vectorPol[0].content, vectorPol[1].content, vectorPol[2].content,
         vectorPol[3].content);
  auto mesPol = getMessage(vectorPol);
  print(mesPol);
  printf("vectorPol size: %li\n", vectorPol.size());

  printf("\nswap test:\n");
  o2vector v(3, FastSpectatorAllocator{ &channelResource });
  v.swap(vector);
  printf("v: %i %i %i\n", v[0].content, v[1].content, v[2].content);
  printf("vector: %i %i %i\n", vector[0].content, vector[1].content, vector[2].content);

  printf("\nadopt\n");
  auto message = std::make_unique<FairMQMessage>(3 * sizeof(elem));
  SpectatorMessageResource messageResource(message.get());
  printf("address of resource3: %p\n", &messageResource);
  elem tmpBuf[3] = { 3, 2, 1 };
  std::memcpy(message->GetData(), tmpBuf, 3 * sizeof(elem));

  o2vector vv = o2vector(3, FastSpectatorAllocator{ &messageResource });
  printf("vv: %i %i %i\n", vv[0].content, vv[1].content, vv[2].content);

  o2vector* newvv = new o2vector(3, FastSpectatorAllocator{ &messageResource });
  printf("newvv: %i %i %i\n", (*newvv)[0].content, (*newvv)[1].content, (*newvv)[2].content);

  printf("\nget message from vv\n");
  boost::container::pmr::polymorphic_allocator<byte> alv = vv.get_allocator();
  printf("resource from allocator from vv: %p\n", alv.resource());

  printf("\nadoptVector()\n");
  auto vvv = adoptVector<elem>(3, messageResource);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  printf("\ncopy vvv\n");
  o2vector cvvv = copyVector(vvv);
  printf("cvvv: %i %i %i\n", cvvv[0].content, cvvv[1].content, cvvv[2].content);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  Adopt<elem>::OutputType outvec;
  {
    printf("\nadopt vector with temp resource\n");
    printf("1---\n");
    auto vvvv = Adopt<elem>::Vector(3, message.get());
    printf("vvvv@%p, %i %i %i\n", vvvv.get(), (*vvvv)[0].content, (*vvvv)[1].content, (*vvvv)[2].content);
    printf("2---\n");
    outvec=std::move(vvvv);
    auto vvvvv = Adopt<elem>::Vector(3, message.get());
    printf("3---\n");
    vvvvv.release();
    printf("4---\n");
  }
  printf("5---\n");
  printf("outvec@%p, %i %i %i\n", outvec.get(), (*outvec)[0].content, (*outvec)[1].content, (*outvec)[2].content);
  printf("outvec size: %li\n", outvec->size());

  decltype(adoptVector<elem>(3,message.get())) outvec2;
  {
    printf("\nadopt vector with temp resource 2\n");
    printf("1---\n");
    auto vvvv = adoptVector<elem>(3, message.get());
    printf("vvvv@%p, %i %i %i\n", vvvv.get(), (*vvvv)[0].content, (*vvvv)[1].content, (*vvvv)[2].content);
    printf("2---\n");
    outvec2=std::move(vvvv);
    auto vvvvv = adoptVector<elem>(3, message.get());
    printf("3---\n");
    vvvvv.release();
    printf("4---\n");
  }
  printf("5---\n");
  printf("outvec2@%p, %i %i %i\n", outvec2.get(), (*outvec2)[0].content, (*outvec2)[1].content, (*outvec2)[2].content);
  printf("outvec2 size: %li\n", outvec2->size());

  printf("cvvv: %i %i %i\n", cvvv[0].content, cvvv[1].content, cvvv[2].content);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  printf("\nstack ctor\n");
  auto ui = std::make_unique<int>(3);
  Stack stack{ BaseHeader{}, BaseHeader{} };

  printf("\nreturn\n");
  return 0;
}
